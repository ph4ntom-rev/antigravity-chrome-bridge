#include "ext_bridge.h"
#include "config.h"
#include "ws_client.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace ag {

ExtensionBridge::ExtensionBridge(const std::string& host, int port)
    : host_(host), port_(port) {}

ExtensionBridge::~ExtensionBridge() {
    stop();
}

std::string ExtensionBridge::generate_id() {
    static int counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return "cmd_" + std::to_string(ms) + "_" + std::to_string(++counter);
}

void ExtensionBridge::start() {
    if (running_.load()) return;
    running_ = true;
    server_thread_ = std::thread(&ExtensionBridge::server_loop, this);
    std::cerr << "[Bridge] HTTP server starting on " << host_ << ":" << port_ << std::endl;
}

void ExtensionBridge::stop() {
    running_ = false;
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

bool ExtensionBridge::is_connected() const {
    int64_t last = last_poll_time_.load();
    if (last == 0) return false;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return (now_ms - last) < 3000;
}

json ExtensionBridge::submit(const std::string& cmd_type, const json& params, int timeout_ms) {
    auto pending = std::make_shared<PendingCommand>();
    pending->cmd.id = generate_id();
    pending->cmd.type = cmd_type;
    pending->cmd.params = params;
    pending->completed = false;

    {
        std::lock_guard<std::mutex> lock(queue_mtx_);
        pending_queue_.push(pending);
        pending_map_[pending->cmd.id] = pending;
    }

    // Wait for result
    {
        std::unique_lock<std::mutex> lock(pending->mtx);
        bool ok = pending->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                        [&] { return pending->completed; });
        if (!ok) {
            // Timeout — remove from map
            std::lock_guard<std::mutex> qlock(queue_mtx_);
            pending_map_.erase(pending->cmd.id);
            return {{"error", "Chrome extension timeout — is it installed and active?"}};
        }
    }

    commands_executed_++;

    // Check for extension-side error
    if (pending->result.is_object() && pending->result.contains("__error__")) {
        return {{"error", pending->result["__error__"]}};
    }
    return pending->result;
}

// ── HTTP Server ─────────────────────────────────────────────────────

std::string ExtensionBridge::handle_poll() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    last_poll_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::lock_guard<std::mutex> lock(queue_mtx_);
    while (!pending_queue_.empty()) {
        auto cmd = pending_queue_.front();
        pending_queue_.pop();

        // Skip if the command has already timed out (erased from map)
        if (pending_map_.find(cmd->cmd.id) == pending_map_.end()) {
            std::cerr << "[Bridge] Discarding timed-out command: " << cmd->cmd.type << std::endl;
            continue;
        }

        json j = {
            {"id", cmd->cmd.id},
            {"type", cmd->cmd.type}
        };
        // Merge params into command
        for (auto& [k, v] : cmd->cmd.params.items()) {
            j[k] = v;
        }
        return json({{"command", j}}).dump();
    }
    return json({{"command", nullptr}}).dump();
}

std::string ExtensionBridge::handle_result(const std::string& body) {
    try {
        json j = json::parse(body);
        std::string cmd_id = j.value("id", "");

        if (cmd_id.empty()) {
            return json({{"error", "id required"}}).dump();
        }

        // Build the result: if error is present, wrap it; otherwise use result
        json final_result;
        if (j.contains("error") && !j["error"].is_null()) {
            final_result = {{"__error__", j["error"]}};
        } else if (j.contains("result") && !j["result"].is_null()) {
            final_result = j["result"];
        } else {
            final_result = json::object();
        }

        std::lock_guard<std::mutex> lock(queue_mtx_);
        auto it = pending_map_.find(cmd_id);
        if (it != pending_map_.end()) {
            auto pending = it->second;
            {
                std::lock_guard<std::mutex> plock(pending->mtx);
                pending->result = final_result;
                pending->completed = true;
            }
            pending->cv.notify_one();
            pending_map_.erase(it);
        }
        return json({{"success", true}}).dump();
    } catch (const std::exception& e) {
        return json({{"error", e.what()}}).dump();
    }
}

std::string ExtensionBridge::handle_status() {
    return json({
        {"connected", is_connected()},
        {"commands_executed", commands_executed_.load()}
    }).dump();
}

// ── Minimal HTTP Server (raw sockets) ───────────────────────────────

void ExtensionBridge::server_loop() {
    socket_t srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) {
        std::cerr << "[Bridge] Failed to create socket." << std::endl;
        return;
    }

    // SO_REUSEADDR
    int opt = 1;
#ifdef _WIN32
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = inet_addr(host_.c_str());

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[Bridge] Bind failed on port " << port_ << std::endl;
        closesocket(srv);
        return;
    }

    if (listen(srv, 16) == SOCKET_ERROR) {
        std::cerr << "[Bridge] Listen failed." << std::endl;
        closesocket(srv);
        return;
    }

    std::cerr << "[Bridge] HTTP server ONLINE on http://" << host_ << ":" << port_ << std::endl;

    // Set non-blocking with timeout for accept
    while (running_.load()) {
        // Use select for accept timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(srv, &fds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms

#ifdef _WIN32
        int nfds = 0; // Ignored on Windows
#else
        int nfds = srv + 1;
#endif
        int sel = select(nfds, &fds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        socket_t client = accept(srv, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        // Read HTTP request (Headers first, then parse Content-Length, then read body completely)
        std::string req;
        char temp_buf[4096];
        size_t header_end = std::string::npos;

        while (header_end == std::string::npos) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(client, &rfds);
            struct timeval tv;
            tv.tv_sec = 5; // 5 seconds timeout
            tv.tv_usec = 0;
#ifdef _WIN32
            int select_nfds = 0;
#else
            int select_nfds = client + 1;
#endif
            int sel = select(select_nfds, &rfds, nullptr, nullptr, &tv);
            if (sel <= 0) {
                break;
            }

            int n = recv(client, temp_buf, sizeof(temp_buf), 0);
            if (n <= 0) {
                break;
            }
            req.append(temp_buf, n);
            if (req.size() > 65536) {
                break; // Header safety limit
            }
            header_end = req.find("\r\n\r\n");
        }

        if (header_end == std::string::npos) {
            closesocket(client);
            continue;
        }

        std::string headers = req.substr(0, header_end);
        std::string body = req.substr(header_end + 4);

        // Parse method and path
        std::string method, path;
        std::istringstream iss(headers);
        iss >> method >> path;

        // Extract Content-Length
        size_t content_length = 0;
        size_t cl_pos = headers.find("Content-Length:");
        if (cl_pos == std::string::npos) {
            cl_pos = headers.find("content-length:");
        }
        if (cl_pos != std::string::npos) {
            size_t val_start = cl_pos + 15;
            size_t val_end = headers.find("\r\n", val_start);
            if (val_end != std::string::npos) {
                std::string cl_str = headers.substr(val_start, val_end - val_start);
                // trim whitespace
                cl_str.erase(0, cl_str.find_first_not_of(" \t"));
                if (cl_str.find_last_not_of(" \t") != std::string::npos) {
                    cl_str.erase(cl_str.find_last_not_of(" \t") + 1);
                }
                try {
                    content_length = std::stoull(cl_str);
                } catch (...) {
                    content_length = 0;
                }
            }
        }

        // If Content-Length exceeds limit, return 413
        if (content_length > static_cast<size_t>(MAX_BODY_SIZE)) {
            std::string resp = "HTTP/1.1 413 Payload Too Large\r\n"
                               "Content-Type: application/json\r\n"
                               "Connection: close\r\n\r\n"
                               "{\"error\":\"Payload too large\"}";
            send(client, resp.c_str(), (int)resp.size(), 0);
            closesocket(client);
            continue;
        }

        // Read the rest of the body
        while (body.size() < content_length) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(client, &rfds);
            struct timeval tv;
            tv.tv_sec = 5;
            tv.tv_usec = 0;
#ifdef _WIN32
            int select_nfds = 0;
#else
            int select_nfds = client + 1;
#endif
            int sel = select(select_nfds, &rfds, nullptr, nullptr, &tv);
            if (sel <= 0) {
                break;
            }

            size_t to_read = std::min(sizeof(temp_buf), content_length - body.size());
            int n = recv(client, temp_buf, static_cast<int>(to_read), 0);
            if (n <= 0) {
                break;
            }
            body.append(temp_buf, n);
        }

        // Route
        std::string response_body;
        int status = 200;

        if (method == "OPTIONS") {
            // CORS preflight
            std::string resp = "HTTP/1.1 204 No Content\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                "Access-Control-Allow-Headers: Content-Type\r\n"
                "Content-Length: 0\r\n\r\n";
            send(client, resp.c_str(), (int)resp.size(), 0);
            closesocket(client);
            continue;
        } else if (method == "GET" && path == "/api/ext/poll") {
            response_body = handle_poll();
        } else if (method == "POST" && path == "/api/ext/result") {
            response_body = handle_result(body);
        } else if (method == "GET" && path == "/api/ext/status") {
            response_body = handle_status();
        } else {
            response_body = json({{"error", "Not found: " + path}}).dump();
            status = 404;
        }

        // Send HTTP response
        std::string status_text = (status == 200) ? "OK" : "Not Found";
        std::ostringstream resp;
        resp << "HTTP/1.1 " << status << " " << status_text << "\r\n"
             << "Content-Type: application/json; charset=utf-8\r\n"
             << "Content-Length: " << response_body.size() << "\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << response_body;

        std::string full_resp = resp.str();
        send(client, full_resp.c_str(), (int)full_resp.size(), 0);
        closesocket(client);
    }

    closesocket(srv);
    std::cerr << "[Bridge] HTTP server stopped." << std::endl;
}

} // namespace ag

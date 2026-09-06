#include "ext_bridge.h"
#include "config.h"
#include "security.h"
#include <httplib.h>
#include <algorithm>
#include <stdexcept>

namespace ag {
static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
ExtensionBridge::ExtensionBridge(const std::string& host, int port, const std::string& token, const std::string& origin)
    : host_(host), token_(token), origin_(origin), port_(port) {
    if (host != "127.0.0.1" || port < 0 || port > 65535) throw std::invalid_argument("Extension server must use IPv4 loopback");
}
ExtensionBridge::~ExtensionBridge() { stop(); }
void ExtensionBridge::start() {
    if (running_) return;
    if (!valid_token(token_)) throw std::invalid_argument("A private 64-character token is required for extension mode");
    const std::string prefix = "chrome-extension://";
    if (!origin_.empty() && (origin_.size() != prefix.size() + 32 || origin_.compare(0, prefix.size(), prefix) != 0 ||
        !std::all_of(origin_.begin() + prefix.size(), origin_.end(), [](char c) { return c >= 'a' && c <= 'p'; })))
        throw std::invalid_argument("Specify the exact chrome-extension:// origin");
    server_ = std::make_unique<httplib::Server>();
    server_->new_task_queue = [] { return new httplib::ThreadPool(4, 4, 16); };
    server_->set_payload_max_length(MAX_BODY_SIZE);
    server_->set_read_timeout(3);
    server_->set_write_timeout(3);
    server_->set_keep_alive_max_count(1);
    server_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        auto reject = [&](int status, const char* message) {
            res.status = status;
            res.set_content(json({{"error", message}}).dump(), "application/json");
            return httplib::Server::HandlerResponse::Handled;
        };
        auto host = req.get_header_value("Host");
        if (req.get_header_value_count("Host") != 1 || (host != "127.0.0.1:" + std::to_string(port_) && host != "localhost:" + std::to_string(port_)))
            return reject(403, "Invalid Host");
        if (req.get_header_value_count("Origin") > 1 || (req.has_header("Origin") && (origin_.empty() || req.get_header_value("Origin") != origin_)))
            return reject(403, "Origin not allowed");
        if (req.has_header("Origin")) {
            res.set_header("Access-Control-Allow-Origin", origin_);
            res.set_header("Vary", "Origin");
        }
        if (req.method == "OPTIONS") {
            if (!req.has_header("Origin")) return reject(403, "Origin required");
            res.status = 204;
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (req.get_header_value_count("Authorization") != 1 || !constant_time_equal(req.get_header_value("Authorization"), "Bearer " + token_))
            return reject(401, "Bearer authentication required");
        return httplib::Server::HandlerResponse::Unhandled;
    });
    server_->Get("/api/ext/poll", [this](const auto&, auto& res) { res.set_content(poll().dump(), "application/json"); });
    server_->Get("/api/ext/status", [this](const auto&, auto& res) {
        res.set_content(json({{"connected", is_connected()}, {"commands_executed", commands_executed()}}).dump(), "application/json");
    });
    server_->Post("/api/ext/result", [this](const auto& req, auto& res) {
        if (req.get_header_value("Content-Type").find("application/json") != 0) { res.status = 415; return; }
        try {
            auto result = resolve(json::parse(req.body));
            if (result.contains("error")) res.status = 409;
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception&) { res.status = 400; res.set_content("{\"error\":\"Invalid result object\"}", "application/json"); }
    });
    if (port_ == 0) port_ = server_->bind_to_any_port(host_);
    else if (!server_->bind_to_port(host_, port_)) throw std::runtime_error("Extension port is already in use");
    if (port_ <= 0) throw std::runtime_error("Cannot bind extension port");
    running_ = true;
    server_thread_ = std::thread([this] { server_->listen_after_bind(); });
    server_->wait_until_ready();
}
void ExtensionBridge::stop() {
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : pending_) {
            entry.second->result = {{"error", "Bridge stopped"}, {"delivery_state", entry.second->dispatched ? "uncertain" : "not_delivered"}};
            entry.second->completed = true;
            entry.second->cv.notify_all();
        }
        pending_.clear(); queue_.clear();
    }
    if (server_) server_->stop();
    if (server_thread_.joinable()) server_thread_.join();
    server_.reset(); last_poll_time_ = 0;
}
bool ExtensionBridge::is_connected() const { return running_ && last_poll_time_ != 0 && now_ms() - last_poll_time_ < 3000; }
json ExtensionBridge::submit(const std::string& type, const json& params, int timeout_ms) {
    if (!params.is_object() || timeout_ms <= 0 || timeout_ms > 60000) return {{"error", "Invalid command parameters"}};
    auto pending = std::make_shared<PendingCommand>();
    std::unique_lock<std::mutex> lock(mutex_);
    if (!running_) return {{"error", "Extension mode is disabled"}, {"delivery_state", "not_delivered"}};
    if (pending_.size() >= 64) return {{"error", "Extension queue is full"}, {"delivery_state", "not_delivered"}};
    const auto id = std::to_string(++next_id_);
    pending->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    pending->command = params;
    pending->command["id"] = id; pending->command["type"] = type;
    pending->command["deadline_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() + timeout_ms;
    pending_[id] = pending; queue_.push_back(id);
    bool ok = pending->cv.wait_until(lock, pending->deadline, [&] { return pending->completed; });
    pending_.erase(id);
    queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
    if (!ok) return {{"error", "Extension result timed out; inspect state before retrying mutations"}, {"delivery_state", pending->dispatched ? "uncertain" : "not_delivered"}};
    ++commands_executed_;
    return pending->result;
}
json ExtensionBridge::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    last_poll_time_ = now_ms();
    while (!queue_.empty()) {
        auto id = queue_.front(); queue_.pop_front();
        auto it = pending_.find(id);
        if (it == pending_.end() || std::chrono::steady_clock::now() >= it->second->deadline) continue;
        it->second->dispatched = true;
        return {{"command", it->second->command}};
    }
    return {{"command", nullptr}};
}
json ExtensionBridge::resolve(const json& data) {
    if (!data.is_object() || !data.contains("id") || !data["id"].is_string()) throw std::invalid_argument("Result ID required");
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_.find(data["id"].get<std::string>());
    if (it == pending_.end() || !it->second->dispatched || it->second->completed || std::chrono::steady_clock::now() >= it->second->deadline)
        return {{"error", "Unknown, expired or completed command"}};
    auto pending = it->second;
    pending->result = data.contains("error") && !data["error"].is_null() ? json({{"error", data["error"]}}) : data.value("result", json::object());
    if (pending->result.is_object() && pending->result.contains("__error__")) pending->result = {{"error", pending->result["__error__"]}};
    pending->completed = true;
    pending->cv.notify_all();
    return {{"success", true}};
}
}

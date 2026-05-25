#include "ws_client.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace ag {

// ---------------------------------------------------------------------------
// Network init / cleanup
// ---------------------------------------------------------------------------

void net_init() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[ws_client] WSAStartup failed: " << WSAGetLastError() << "\n";
    }
#endif
}

void net_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// WebSocketClient
// ---------------------------------------------------------------------------

WebSocketClient::~WebSocketClient() {
    close();
}

bool WebSocketClient::connect(const std::string& host, int port, const std::string& path) {
    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) {
        std::cerr << "[ws_client] socket() failed\n";
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "[ws_client] inet_addr failed for " << host << "\n";
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    if (::connect(sock_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[ws_client] connect() failed to " << host << ":" << port << "\n";
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    if (!do_handshake(host, port, path)) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    connected_ = true;
    return true;
}

bool WebSocketClient::do_handshake(const std::string& host, int port, const std::string& path) {
    std::string key = generate_ws_key();

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << ":" << port << "\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << key << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n";
    req << "\r\n";

    std::string request = req.str();
    if (!send_raw(request.data(), request.size())) {
        std::cerr << "[ws_client] Failed to send handshake\n";
        return false;
    }

    // Read response (look for \r\n\r\n)
    std::string response;
    char buf[1];
    int timeout_ms = 5000;
    while (response.find("\r\n\r\n") == std::string::npos) {
        if (!recv_raw(buf, 1, timeout_ms)) {
            std::cerr << "[ws_client] Handshake response timeout\n";
            return false;
        }
        response += buf[0];
        if (response.size() > 4096) {
            std::cerr << "[ws_client] Handshake response too large\n";
            return false;
        }
    }

    // Check for 101 Switching Protocols
    if (response.find("101") == std::string::npos) {
        std::cerr << "[ws_client] Handshake failed, no 101 response:\n" << response << "\n";
        return false;
    }

    return true;
}

bool WebSocketClient::send_text(const std::string& data) {
    if (!connected_) return false;

    std::vector<uint8_t> frame;

    // FIN=1, opcode=0x1 (text)
    frame.push_back(0x81);

    // Payload length with MASK bit set
    size_t len = data.size();
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len | 0x80));
    } else if (len <= 0xFFFF) {
        frame.push_back(0xFE); // 126 | 0x80
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(0xFF); // 127 | 0x80
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    // Generate 4-byte mask key
    uint8_t mask_key[4];
    uint32_t mask_val = rng_();
    std::memcpy(mask_key, &mask_val, 4);
    frame.insert(frame.end(), mask_key, mask_key + 4);

    // Masked payload
    for (size_t i = 0; i < len; ++i) {
        frame.push_back(static_cast<uint8_t>(data[i]) ^ mask_key[i % 4]);
    }

    return send_raw(frame.data(), frame.size());
}

std::string WebSocketClient::recv_text(int timeout_ms) {
    if (!connected_) return "";

    std::string result;
    bool fin = false;

    while (!fin) {
        // Read first 2 bytes of frame header
        uint8_t header[2];
        if (!recv_raw(header, 2, timeout_ms)) {
            std::cerr << "[ws_client] Failed to read frame header\n";
            return "";
        }

        fin = (header[0] & 0x80) != 0;
        uint8_t opcode = header[0] & 0x0F;
        bool masked = (header[1] & 0x80) != 0;
        uint64_t payload_len = header[1] & 0x7F;

        // Extended payload length
        if (payload_len == 126) {
            uint8_t ext[2];
            if (!recv_raw(ext, 2, timeout_ms)) return "";
            payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
        } else if (payload_len == 127) {
            uint8_t ext[8];
            if (!recv_raw(ext, 8, timeout_ms)) return "";
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | ext[i];
            }
        }

        // Read mask key if present
        uint8_t mask_key[4] = {0};
        if (masked) {
            if (!recv_raw(mask_key, 4, timeout_ms)) return "";
        }

        // Read payload
        if (payload_len > 0) {
            if (payload_len > 50 * 1024 * 1024) { // 50MB safety limit
                std::cerr << "[ws_client] Payload too large: " << payload_len << " bytes\n";
                return "";
            }
            std::vector<uint8_t> payload(static_cast<size_t>(payload_len));
            if (!recv_raw(payload.data(), static_cast<size_t>(payload_len), timeout_ms)) {
                std::cerr << "[ws_client] Failed to read payload (" << payload_len << " bytes)\n";
                return "";
            }

            // Unmask if needed
            if (masked) {
                for (size_t i = 0; i < payload.size(); ++i) {
                    payload[i] ^= mask_key[i % 4];
                }
            }

            // Handle opcode
            if (opcode == 0x8) {
                // Close frame
                connected_ = false;
                return "";
            } else if (opcode == 0x9) {
                // Ping - send pong
                // Build a simple pong frame (unmasked for simplicity, but clients should mask)
                std::vector<uint8_t> pong;
                pong.push_back(0x8A); // FIN + Pong opcode
                pong.push_back(static_cast<uint8_t>(payload.size() | 0x80));
                uint8_t pmask[4];
                uint32_t pmval = rng_();
                std::memcpy(pmask, &pmval, 4);
                pong.insert(pong.end(), pmask, pmask + 4);
                for (size_t i = 0; i < payload.size(); ++i) {
                    pong.push_back(payload[i] ^ pmask[i % 4]);
                }
                send_raw(pong.data(), pong.size());
                fin = false; // Continue reading
                continue;
            } else if (opcode == 0xA) {
                // Pong - ignore
                fin = false;
                continue;
            }

            result.append(reinterpret_cast<char*>(payload.data()), payload.size());
        }
    }

    return result;
}

void WebSocketClient::close() {
    if (sock_ != INVALID_SOCKET) {
        if (connected_) {
            // Send close frame (masked, as required by RFC 6455 for clients)
            uint8_t close_frame[6];
            close_frame[0] = 0x88; // FIN + Close opcode
            close_frame[1] = 0x80; // MASK bit set, 0 payload length
            uint32_t mask_val = rng_();
            std::memcpy(close_frame + 2, &mask_val, 4);
            send_raw(close_frame, 6);
        }
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        connected_ = false;
    }
}

bool WebSocketClient::send_raw(const void* data, size_t len) {
    const char* ptr = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(sock_, ptr + sent, static_cast<int>(len - sent), 0);
        if (n == SOCKET_ERROR) {
            std::cerr << "[ws_client] send_raw failed\n";
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool WebSocketClient::recv_raw(void* buf, size_t len, int timeout_ms) {
    char* ptr = static_cast<char*>(buf);
    size_t received = 0;

    while (received < len) {
        // Use select for timeout (works on both Windows and Linux)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock_, &readfds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int sel = select(static_cast<int>(sock_ + 1), &readfds, nullptr, nullptr, &tv);
        if (sel <= 0) {
            if (sel == 0) {
                std::cerr << "[ws_client] recv_raw timeout\n";
            } else {
                std::cerr << "[ws_client] recv_raw select error\n";
            }
            return false;
        }

        int n = ::recv(sock_, ptr + received, static_cast<int>(len - received), 0);
        if (n <= 0) {
            std::cerr << "[ws_client] recv_raw failed (n=" << n << ")\n";
            return false;
        }
        received += static_cast<size_t>(n);
    }

    return true;
}

std::string WebSocketClient::base64_encode(const uint8_t* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);

        result += table[(n >> 18) & 0x3F];
        result += table[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? table[n & 0x3F] : '=';
    }

    return result;
}

std::string WebSocketClient::generate_ws_key() {
    uint8_t bytes[16];
    for (int i = 0; i < 16; ++i) {
        bytes[i] = static_cast<uint8_t>(rng_() & 0xFF);
    }
    return base64_encode(bytes, 16);
}

// ---------------------------------------------------------------------------
// HTTP helper: create socket, send request, read full response, return body
// ---------------------------------------------------------------------------

static std::string http_request(const std::string& method, const std::string& host, int port,
                                const std::string& path, int timeout_ms) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[http] socket() failed\n";
        return "";
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "[http] inet_addr failed for " << host << "\n";
        closesocket(sock);
        return "";
    }

    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[http] connect() failed to " << host << ":" << port << "\n";
        closesocket(sock);
        return "";
    }

    // Build request
    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << ":" << port << "\r\n";
    req << "Connection: close\r\n";
    if (method == "PUT") {
        req << "Content-Length: 0\r\n";
    }
    req << "\r\n";

    std::string request = req.str();
    const char* ptr = request.data();
    size_t remaining = request.size();
    while (remaining > 0) {
        int n = ::send(sock, ptr, static_cast<int>(remaining), 0);
        if (n == SOCKET_ERROR) {
            std::cerr << "[http] send failed\n";
            closesocket(sock);
            return "";
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }

    // Read full response
    std::string response;
    char buf[4096];
    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int sel = select(static_cast<int>(sock + 1), &readfds, nullptr, nullptr, &tv);
        if (sel <= 0) {
            if (sel == 0 && response.empty()) {
                std::cerr << "[http] Response timeout\n";
            }
            break;
        }

        int n = ::recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, static_cast<size_t>(n));

        // Safety limit
        if (response.size() > 10 * 1024 * 1024) {
            std::cerr << "[http] Response too large, truncating\n";
            break;
        }
    }

    closesocket(sock);

    // Parse body (after \r\n\r\n)
    auto header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return response; // Return everything if we can't find header boundary
    }

    return response.substr(header_end + 4);
}

std::string http_get(const std::string& host, int port, const std::string& path, int timeout_ms) {
    return http_request("GET", host, port, path, timeout_ms);
}

std::string http_put(const std::string& host, int port, const std::string& path, int timeout_ms) {
    return http_request("PUT", host, port, path, timeout_ms);
}

} // namespace ag

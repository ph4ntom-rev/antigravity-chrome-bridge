#include "ws_client.h"
#include "config.h"
#include <httplib.h>
#include <chrono>
#include <stdexcept>

namespace ag {
// cpp-httplib owns the platform network initialization and RFC 6455 framing.
void net_init() {}
void net_cleanup() {}
WebSocketClient::WebSocketClient() = default;
WebSocketClient::~WebSocketClient() = default;
static bool valid_destination(const std::string& host, int port, const std::string& path) {
    return host == "127.0.0.1" && port > 0 && port <= 65535 && !path.empty() && path[0] == '/'
        && path.find_first_of("\r\n\t ") == std::string::npos;
}
bool WebSocketClient::connect(const std::string& host, int port, const std::string& path) {
    client_.reset();
    if (!valid_destination(host, port, path)) return false;
    client_ = std::make_unique<httplib::ws::WebSocketClient>("ws://" + host + ":" + std::to_string(port) + path);
    client_->set_connection_timeout(std::chrono::seconds(3));
    client_->set_read_timeout(std::chrono::seconds(10));
    client_->set_write_timeout(std::chrono::seconds(3));
    client_->set_websocket_ping_interval(0);
    return static_cast<bool>(client_->connect());
}
bool WebSocketClient::send_text(const std::string& data) {
    return client_ && data.size() <= MAX_BODY_SIZE && client_->send(data);
}
std::string WebSocketClient::recv_text(int /*timeout_ms*/) {
    // The underlying stream takes its ten-second read deadline at connect().
    std::string result;
    if (!client_ || client_->read(result) != httplib::ws::Text) return "";
    return result;
}
void WebSocketClient::close() { client_.reset(); }
bool WebSocketClient::is_connected() const { return client_ && client_->is_open(); }

static std::string http_request(const std::string& method, const std::string& host, int port, const std::string& path, int timeout_ms) {
    if (!valid_destination(host, port, path)) throw std::runtime_error("Invalid loopback HTTP target");
    httplib::Client client(host, port);
    client.set_connection_timeout(std::chrono::milliseconds(timeout_ms));
    client.set_read_timeout(std::chrono::milliseconds(timeout_ms));
    client.set_write_timeout(std::chrono::milliseconds(timeout_ms));
    client.set_max_timeout(std::chrono::milliseconds(timeout_ms));
    client.set_payload_max_length(MAX_BODY_SIZE);
    auto response = method == "GET" ? client.Get(path) : client.Put(path, "", "application/json");
    if (!response || response->status < 200 || response->status >= 300) throw std::runtime_error("CDP HTTP request failed");
    return response->body;
}
std::string http_get(const std::string& host, int port, const std::string& path, int timeout_ms) { return http_request("GET", host, port, path, timeout_ms); }
std::string http_put(const std::string& host, int port, const std::string& path, int timeout_ms) { return http_request("PUT", host, port, path, timeout_ms); }
}

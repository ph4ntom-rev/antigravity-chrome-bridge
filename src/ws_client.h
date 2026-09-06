#pragma once
#include <string>
#include <memory>
namespace httplib { namespace ws { class WebSocketClient; } }
namespace ag {
void net_init();
void net_cleanup();
class WebSocketClient {
public:
    WebSocketClient();
    ~WebSocketClient();
    bool connect(const std::string& host, int port, const std::string& path);
    bool send_text(const std::string& data);
    std::string recv_text(int timeout_ms = 10000);
    void close();
    bool is_connected() const;
private:
    std::unique_ptr<httplib::ws::WebSocketClient> client_;
};
std::string http_get(const std::string& host, int port, const std::string& path, int timeout_ms = 3000);
std::string http_put(const std::string& host, int port, const std::string& path, int timeout_ms = 3000);
}

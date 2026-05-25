#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <random>
#include <array>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET socket_t;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <poll.h>
  typedef int socket_t;
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR (-1)
  #define closesocket close
#endif

namespace ag {

void net_init();
void net_cleanup();

class WebSocketClient {
public:
    WebSocketClient() = default;
    ~WebSocketClient();

    bool connect(const std::string& host, int port, const std::string& path);
    bool send_text(const std::string& data);
    std::string recv_text(int timeout_ms = 5000);
    void close();
    bool is_connected() const { return connected_; }

private:
    socket_t sock_ = INVALID_SOCKET;
    bool connected_ = false;
    std::mt19937 rng_{std::random_device{}()};

    bool do_handshake(const std::string& host, int port, const std::string& path);
    bool send_raw(const void* data, size_t len);
    bool recv_raw(void* buf, size_t len, int timeout_ms);
    std::string base64_encode(const uint8_t* data, size_t len);
    std::string generate_ws_key();
};

// Simple synchronous HTTP GET returning body (for CDP /json endpoint)
std::string http_get(const std::string& host, int port, const std::string& path, int timeout_ms = 3000);

// Simple synchronous HTTP PUT returning body
std::string http_put(const std::string& host, int port, const std::string& path, int timeout_ms = 3000);

} // namespace ag

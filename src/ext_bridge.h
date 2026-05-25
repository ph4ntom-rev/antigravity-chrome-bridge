#pragma once
#include "ws_client.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <queue>
#include <unordered_map>

namespace ag {

using json = nlohmann::json;

struct ExtCommand {
    std::string id;
    std::string type;
    json params;
};

struct PendingCommand {
    ExtCommand cmd;
    json result;
    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
};

class ExtensionBridge {
public:
    ExtensionBridge(const std::string& host = "127.0.0.1", int port = 13371);
    ~ExtensionBridge();

    // Start HTTP server in background thread
    void start();
    void stop();

    // Submit command and wait for result (blocking)
    json submit(const std::string& cmd_type, const json& params = json::object(), int timeout_ms = 10000);

    // Connection status
    bool is_connected() const;
    int commands_executed() const { return commands_executed_.load(); }

private:
    std::string host_;
    int port_;
    std::atomic<bool> running_{false};
    std::atomic<int> commands_executed_{0};
    std::atomic<int64_t> last_poll_time_{0};
    std::thread server_thread_;

    // Command queue
    std::mutex queue_mtx_;
    std::queue<std::shared_ptr<PendingCommand>> pending_queue_;
    std::unordered_map<std::string, std::shared_ptr<PendingCommand>> pending_map_;

    void server_loop();

    // HTTP endpoint handlers (called from server thread)
    std::string handle_poll();
    std::string handle_result(const std::string& body);
    std::string handle_status();

    // Generate unique command ID
    std::string generate_id();
};

} // namespace ag

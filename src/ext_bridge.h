#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace httplib { class Server; }
namespace ag {
using json = nlohmann::json;
struct PendingCommand {
    json command, result;
    bool completed = false, dispatched = false;
    std::chrono::steady_clock::time_point deadline;
    std::condition_variable cv;
};
class ExtensionBridge {
public:
    ExtensionBridge(const std::string& host = "127.0.0.1", int port = 13371,
        const std::string& token = "", const std::string& origin = "");
    ~ExtensionBridge();
    void start();
    void stop();
    json submit(const std::string& type, const json& params = json::object(), int timeout_ms = 10000);
    bool is_connected() const;
    int commands_executed() const { return commands_executed_.load(); }
    int port() const { return port_; }
private:
    std::string host_, token_, origin_;
    int port_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> commands_executed_{0};
    std::atomic<int64_t> last_poll_time_{0};
    std::mutex mutex_;
    std::deque<std::string> queue_;
    std::unordered_map<std::string, std::shared_ptr<PendingCommand>> pending_;
    uint64_t next_id_ = 0;
    json poll();
    json resolve(const json& data);
};
}

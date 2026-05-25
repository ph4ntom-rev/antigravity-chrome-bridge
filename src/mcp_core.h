#pragma once
#include "mcp_tools.h"
#include "mcp_resources.h"
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <functional>

namespace ag {

using json = nlohmann::json;

class MCPServer {
public:
    MCPServer(ToolRegistry& tools, ResourceRegistry& resources);

    // Main stdio loop — blocks until EOF on stdin
    void run();

private:
    ToolRegistry& tools_;
    ResourceRegistry& resources_;
    bool initialized_ = false;

    // Read one JSON-RPC message from stdin (newline-delimited)
    json read_message();

    // Write one JSON-RPC message to stdout
    void write_message(const json& msg);

    // Process a single JSON-RPC request/notification
    void handle_message(const json& msg);

    // JSON-RPC response helpers
    json make_result(const json& id, const json& result);
    json make_error(const json& id, int code, const std::string& message);

    // MCP method handlers
    json handle_initialize(const json& params);
    json handle_tools_list(const json& params);
    json handle_tools_call(const json& params);
    json handle_resources_list(const json& params);
    json handle_resources_read(const json& params);
};

} // namespace ag

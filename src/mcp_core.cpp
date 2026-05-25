#include "mcp_core.h"
#include "config.h"
#include <sstream>
#include <cstdio>

namespace ag {

MCPServer::MCPServer(ToolRegistry& tools, ResourceRegistry& resources)
    : tools_(tools), resources_(resources) {}

void MCPServer::run() {
    std::cerr << "[MCP] Server starting, reading from stdin..." << std::endl;
    while (true) {
        json msg = read_message();
        if (msg.is_null()) {
            std::cerr << "[MCP] EOF on stdin, shutting down." << std::endl;
            break;
        }
        handle_message(msg);
    }
}

json MCPServer::read_message() {
    std::string line;
    while (std::getline(std::cin, line)) {
        // Skip empty lines
        if (line.empty() || (line.size() == 1 && line[0] == '\r')) continue;
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        try {
            return json::parse(line);
        } catch (const json::parse_error& e) {
            std::cerr << "[MCP] JSON parse error: " << e.what() << std::endl;
            std::cerr << "[MCP] Raw line: " << line.substr(0, 200) << std::endl;
            // Send parse error response
            json err = make_error(nullptr, -32700, "Parse error");
            write_message(err);
        }
    }
    return nullptr; // EOF
}

void MCPServer::write_message(const json& msg) {
    std::string out = msg.dump(-1, ' ', false, json::error_handler_t::replace);
    std::cout << out << "\n" << std::flush;
}

void MCPServer::handle_message(const json& msg) {
    // Validate JSON-RPC 2.0
    if (!msg.is_object()) {
        write_message(make_error(nullptr, -32600, "Invalid Request: not an object"));
        return;
    }

    std::string method = msg.value("method", "");
    json id = msg.contains("id") ? msg["id"] : json(nullptr);
    json params = msg.value("params", json::object());

    // Notifications (no id) — we just process and don't respond
    if (!msg.contains("id")) {
        if (method == "notifications/initialized") {
            std::cerr << "[MCP] Client confirmed initialization." << std::endl;
        } else if (method == "notifications/cancelled") {
            std::cerr << "[MCP] Client cancelled request." << std::endl;
        }
        // Don't send response for notifications
        return;
    }

    // Dispatch methods
    json result;
    if (method == "initialize") {
        result = handle_initialize(params);
    } else if (method == "tools/list") {
        result = handle_tools_list(params);
    } else if (method == "tools/call") {
        result = handle_tools_call(params);
    } else if (method == "resources/list") {
        result = handle_resources_list(params);
    } else if (method == "resources/read") {
        result = handle_resources_read(params);
    } else if (method == "ping") {
        result = json::object();
    } else {
        write_message(make_error(id, -32601, "Method not found: " + method));
        return;
    }

    write_message(make_result(id, result));
}

json MCPServer::make_result(const json& id, const json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

json MCPServer::make_error(const json& id, int code, const std::string& message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
}

json MCPServer::handle_initialize(const json& params) {
    initialized_ = true;
    std::string client_name = "unknown";
    if (params.contains("clientInfo") && params["clientInfo"].contains("name")) {
        client_name = params["clientInfo"]["name"].get<std::string>();
    }
    std::cerr << "[MCP] Initialized by client: " << client_name << std::endl;

    return {
        {"protocolVersion", MCP_PROTOCOL_VERSION},
        {"capabilities", {
            {"tools", json::object()},
            {"resources", json::object()}
        }},
        {"serverInfo", {
            {"name", SERVER_NAME},
            {"version", SERVER_VERSION}
        }}
    };
}

json MCPServer::handle_tools_list(const json& /*params*/) {
    return tools_.list_tools();
}

json MCPServer::handle_tools_call(const json& params) {
    std::string name = params.value("name", "");
    json arguments = params.value("arguments", json::object());

    if (name.empty()) {
        return {
            {"content", json::array({
                {{"type", "text"}, {"text", "Error: tool name is required"}}
            })},
            {"isError", true}
        };
    }

    try {
        return tools_.call_tool(name, arguments);
    } catch (const std::exception& e) {
        return {
            {"content", json::array({
                {{"type", "text"}, {"text", std::string("Internal error: ") + e.what()}}
            })},
            {"isError", true}
        };
    }
}

json MCPServer::handle_resources_list(const json& /*params*/) {
    return resources_.list_resources();
}

json MCPServer::handle_resources_read(const json& params) {
    std::string uri = params.value("uri", "");
    if (uri.empty()) {
        return {{"contents", json::array()}};
    }
    return resources_.read_resource(uri);
}

} // namespace ag

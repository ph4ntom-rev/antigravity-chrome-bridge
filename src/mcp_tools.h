#pragma once
#include "cdp_client.h"
#include "ext_bridge.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace ag {

using json = nlohmann::json;

// ── Tool schema definition ─────────────────────────────────────────
struct ToolParam {
    std::string name;
    std::string type;        // "string", "integer", "number", "boolean"
    std::string description;
    bool required = true;
    json default_value = nullptr; // null means no default
};

struct ToolDef {
    std::string name;
    std::string description;
    std::vector<ToolParam> params;
    std::function<json(const json& args, CDPClient& cdp, ExtensionBridge& ext)> handler;
};

// ── Tool Registry ───────────────────────────────────────────────────
class ToolRegistry {
public:
    ToolRegistry(CDPClient& cdp, ExtensionBridge& ext);

    // Get all registered tools as MCP-format JSON
    json list_tools() const;

    // Call a tool by name with arguments, returns MCP result content
    json call_tool(const std::string& name, const json& arguments);

    size_t count() const { return tools_.size(); }

private:
    CDPClient& cdp_;
    ExtensionBridge& ext_;
    std::vector<ToolDef> tools_;
    std::mutex mtx_;

    void register_all();
    void reg(ToolDef tool);

    // Generate JSON Schema for a tool's inputSchema
    json make_input_schema(const ToolDef& t) const;

    // Helper: determine best backend for current request
    // Returns "cdp" if CDP is available, "ext" if extension is connected, or "" if neither
    std::string detect_backend();
};

} // namespace ag

#include "mcp_resources.h"
#include "config.h"
#include <iostream>

namespace ag {

ResourceRegistry::ResourceRegistry(CDPClient& cdp, ExtensionBridge& ext)
    : cdp_(cdp), ext_(ext) {
    register_all();
}

void ResourceRegistry::register_all() {
    resources_.push_back({
        "chrome://tabs",
        "Open Chrome Tabs",
        "Live list of all currently open Chrome tabs with their IDs, titles, and URLs.",
        "application/json"
    });

    resources_.push_back({
        "chrome://status",
        "Bridge Status",
        "Current status of the Chrome Bridge — active mode, backend availability, extension health.",
        "application/json"
    });

    resources_.push_back({
        "chrome://active-tab",
        "Active Tab Info",
        "Details about the currently active/focused Chrome tab.",
        "application/json"
    });

    std::cerr << "[MCP] Registered " << resources_.size() << " resources." << std::endl;
}

json ResourceRegistry::list_resources() const {
    json arr = json::array();
    for (auto& r : resources_) {
        arr.push_back({
            {"uri", r.uri},
            {"name", r.name},
            {"description", r.description},
            {"mimeType", r.mime_type}
        });
    }
    return {{"resources", arr}};
}

json ResourceRegistry::read_resource(const std::string& uri) {
    json content;

    if (uri == "chrome://tabs") {
        json tabs_data = json::array();
        if (cdp_.is_available()) {
            auto tabs = cdp_.list_tabs();
            for (auto& t : tabs) tabs_data.push_back(to_json_obj(t));
        } else if (ext_.is_connected()) {
            auto result = ext_.submit("list_tabs");
            tabs_data = result.value("tabs", json::array());
        }
        content = {
            {"uri", uri},
            {"mimeType", "application/json"},
            {"text", tabs_data.dump(2)}
        };
    } else if (uri == "chrome://status") {
        bool cdp_ok = cdp_.is_available();
        bool ext_ok = ext_.is_connected();
        std::string mode = "none";
        if (cdp_ok && ext_ok) mode = "hybrid";
        else if (cdp_ok) mode = "cdp";
        else if (ext_ok) mode = "extension";

        json status = {
            {"cdp_available", cdp_ok},
            {"extension_connected", ext_ok},
            {"active_mode", mode},
            {"extension_commands_executed", ext_.commands_executed()},
            {"server", SERVER_NAME},
            {"version", SERVER_VERSION}
        };
        content = {
            {"uri", uri},
            {"mimeType", "application/json"},
            {"text", status.dump(2)}
        };
    } else if (uri == "chrome://active-tab") {
        json active = {{"error", "no active tab found"}};
        if (cdp_.is_available()) {
            auto tabs = cdp_.list_tabs();
            // CDP doesn't mark "active" — return first page tab
            if (!tabs.empty()) {
                active = to_json_obj(tabs[0]);
            }
        } else if (ext_.is_connected()) {
            auto result = ext_.submit("list_tabs");
            auto tabs = result.value("tabs", json::array());
            for (auto& t : tabs) {
                if (t.value("active", false)) { active = t; break; }
            }
        }
        content = {
            {"uri", uri},
            {"mimeType", "application/json"},
            {"text", active.dump(2)}
        };
    } else {
        content = {
            {"uri", uri},
            {"mimeType", "text/plain"},
            {"text", "Unknown resource: " + uri}
        };
    }

    return {{"contents", json::array({content})}};
}

} // namespace ag

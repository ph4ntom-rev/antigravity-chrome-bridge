#include "mcp_tools.h"
#include "config.h"
#include <iostream>
#include <sstream>

namespace ag {

// ── Helper: wrap text result ────────────────────────────────────────
static json text_result(const std::string& text) {
    return {
        {"content", json::array({{{"type", "text"}, {"text", text}}})},
        {"isError", false}
    };
}

static json json_result(const json& data) {
    auto response = text_result(data.dump(2));
    response["isError"] = data.is_object() && (data.contains("error") || (data.contains("success") && data["success"] == false));
    return response;
}

static json error_result(const std::string& msg) {
    return {
        {"content", json::array({{{"type", "text"}, {"text", "Error: " + msg}}})},
        {"isError", true}
    };
}

// ── ToolRegistry ────────────────────────────────────────────────────

ToolRegistry::ToolRegistry(CDPClient& cdp, ExtensionBridge& ext)
    : cdp_(cdp), ext_(ext) {
    register_all();
}

void ToolRegistry::reg(ToolDef tool) {
    tools_.push_back(std::move(tool));
}

std::string ToolRegistry::detect_backend() {
    if (cdp_.is_available()) return "cdp";
    if (ext_.is_connected()) return "ext";
    return "";
}

json ToolRegistry::make_input_schema(const ToolDef& t) const {
    json props = json::object();
    json required_arr = json::array();

    for (auto& p : t.params) {
        json prop = {{"type", p.type}, {"description", p.description}};
        if (!p.default_value.is_null()) {
            prop["default"] = p.default_value;
        }
        props[p.name] = prop;
        if (p.required) {
            required_arr.push_back(p.name);
        }
    }

    return {
        {"type", "object"},
        {"properties", props},
        {"required", required_arr}
    };
}

json ToolRegistry::list_tools() const {
    json tools_arr = json::array();
    for (auto& t : tools_) {
        tools_arr.push_back({
            {"name", t.name},
            {"description", t.description},
            {"inputSchema", make_input_schema(t)}
        });
    }
    return {{"tools", tools_arr}};
}

static std::string validate_arguments(const ToolDef& t, const json& arguments) {
    if (!arguments.is_object()) return "Arguments must be an object";
    for (const auto& param : t.params) {
        if (!arguments.contains(param.name)) {
            if (param.required) return "Missing required argument: " + param.name;
            continue;
        }
        const auto& value = arguments[param.name];
        const bool valid = (param.type == "string" && value.is_string()) ||
            (param.type == "integer" && value.is_number_integer()) ||
            (param.type == "number" && value.is_number()) ||
            (param.type == "boolean" && value.is_boolean()) ||
            (param.type == "array" && value.is_array()) ||
            (param.type == "object" && value.is_object());
        if (!valid) return "Invalid type for argument: " + param.name;
    }
    return "";
}

json ToolRegistry::call_tool(const std::string& name, const json& arguments) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto& t : tools_) {
        if (t.name == name) {
            try {
                const auto invalid = validate_arguments(t, arguments);
                if (!invalid.empty()) return error_result(invalid);
                return t.handler(arguments, cdp_, ext_);
            } catch (const std::exception& e) {
                return error_result(e.what());
            }
        }
    }
    return error_result("Unknown tool: " + name);
}

// ── Tool Registration ───────────────────────────────────────────────

void ToolRegistry::register_all() {

    // ════════════════════════════════════════════════════════════════
    // 1. chrome_list_tabs
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_list_tabs",
         "List all open Chrome tabs with their IDs, titles, URLs, and status. "
         "Automatically uses CDP if available, otherwise falls back to the Chrome Extension.",
         {},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            if (cdp.is_available()) {
                auto tabs = cdp.list_tabs();
                json arr = json::array();
                for (auto& t : tabs) arr.push_back(to_json_obj(t));
                return json_result({{"tabs", arr}, {"count", arr.size()}, {"mode", "cdp"}});
            }
            if (ext.is_connected()) {
                auto r = ext.submit("list_tabs");
                r["mode"] = "extension";
                return json_result(r);
            }
            return error_result("Chrome is not reachable. Start with --remote-debugging-port=9222 or install the extension.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 2. chrome_get_tab
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_get_tab",
         "Get detailed info for a specific tab by its ID.",
         {{"tab_id", "string", "Tab ID to retrieve", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            if (tid.empty()) return error_result("tab_id is required");
            if (cdp.is_available()) {
                auto tab = cdp.get_tab(tid);
                if (tab) return json_result(to_json_obj(*tab));
                return error_result("Tab not found: " + tid);
            }
            return error_result("CDP required for get_tab. Start Chrome with --remote-debugging-port=9222.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 3. chrome_create_tab
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_create_tab",
         "Open a new tab in Chrome with the specified URL.",
         {{"url", "string", "URL to open in the new tab", false, "about:blank"}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string url = args.value("url", "about:blank");
            if (cdp.is_available()) return json_result(cdp.create_tab(url));
            if (ext.is_connected()) return json_result(ext.submit("create_tab", {{"url", url}}));
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 4. chrome_close_tab
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_close_tab",
         "Close a specific tab by its ID.",
         {{"tab_id", "string", "Tab ID to close", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            if (tid.empty()) return error_result("tab_id required");
            if (cdp.is_available()) return json_result(cdp.close_tab(tid));
            if (ext.is_connected()) {
                int itid = 0;
                try { itid = std::stoi(tid); } catch (...) { return error_result("Invalid tab_id for extension mode"); }
                return json_result(ext.submit("close_tab", {{"tab_id", itid}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 5. chrome_navigate
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_navigate",
         "Navigate a specific tab to a new URL.",
         {{"tab_id", "string", "Tab ID to navigate", true},
          {"url", "string", "Target URL", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string url = args.value("url", "");
            if (tid.empty() || url.empty()) return error_result("tab_id and url are required");
            if (cdp.is_available()) return json_result(cdp.navigate(tid, url));
            if (ext.is_connected()) {
                int itid = 0;
                try { itid = std::stoi(tid); } catch (...) { return error_result("Invalid tab_id"); }
                return json_result(ext.submit("navigate", {{"tab_id", itid}, {"url", url}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 6. chrome_reload
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_reload",
         "Reload a tab, optionally bypassing the cache.",
         {{"tab_id", "string", "Tab ID to reload", true},
          {"ignore_cache", "boolean", "If true, bypass browser cache", false, false}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            bool ignore = args.value("ignore_cache", false);
            if (tid.empty()) return error_result("tab_id required");
            if (cdp.is_available()) return json_result(cdp.reload(tid, ignore));
            if (ext.is_connected()) {
                int itid = 0;
                try { itid = std::stoi(tid); } catch (...) { return error_result("Invalid tab_id"); }
                return json_result(ext.submit("reload", {{"tab_id", itid}, {"ignore_cache", ignore}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 7. chrome_evaluate_js
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_evaluate_js",
         "Execute JavaScript code inside a specific tab's page context and return the result. "
         "Supports any valid JS expression.",
         {{"tab_id", "string", "Tab ID to execute JS in", true},
          {"js_code", "string", "JavaScript code to evaluate", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string code = args.value("js_code", "");
            if (tid.empty() || code.empty()) return error_result("tab_id and js_code required");
            if (cdp.is_available()) return json_result(cdp.evaluate_js(tid, code));
            if (ext.is_connected()) {
                int itid = 0;
                try { itid = std::stoi(tid); } catch (...) { return error_result("Invalid tab_id"); }
                return json_result(ext.submit("eval_js", {{"tab_id", itid}, {"js_code", code}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 8. chrome_get_page_content
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_get_page_content",
         "Get the full page content as HTML or extracted plain text.",
         {{"tab_id", "string", "Tab ID", true},
          {"mode", "string", "Content mode: 'html' for raw HTML, 'text' for extracted text", false, "text"}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string mode = args.value("mode", "text");
            if (tid.empty()) return error_result("tab_id required");
            if (cdp.is_available()) {
                std::string expr = (mode == "html")
                    ? "document.documentElement.outerHTML"
                    : "document.body.innerText";
                return json_result(cdp.evaluate_js(tid, expr));
            }
            if (ext.is_connected()) {
                int itid = 0;
                try { itid = std::stoi(tid); } catch (...) { return error_result("Invalid tab_id"); }
                return json_result(ext.submit("get_page_content", {{"tab_id", itid}, {"mode", mode}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 9. chrome_screenshot
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_screenshot",
         "Capture a screenshot of the visible area of a tab. Returns base64-encoded image.",
         {{"tab_id", "string", "Tab ID (used for CDP mode, ignored in extension mode)", false, ""},
          {"format", "string", "Image format: 'jpeg' or 'png'", false, "jpeg"},
          {"quality", "integer", "JPEG quality 0-100", false, 85}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string fmt = args.value("format", "jpeg");
            int quality = args.value("quality", 85);
            if (cdp.is_available() && !tid.empty()) {
                return json_result(cdp.capture_screenshot(tid, fmt, quality));
            }
            if (ext.is_connected()) {
                json params = json::object();
                if (!tid.empty()) {
                    try {
                        params["tab_id"] = std::stoi(tid);
                    } catch (...) {}
                }
                return json_result(ext.submit("capture_tab", params, 15000));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 10. chrome_query_selector
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_query_selector",
         "Find elements on the page matching a CSS selector. Returns element details (tag, id, class, text, attributes).",
         {{"tab_id", "string", "Tab ID", true},
          {"selector", "string", "CSS selector to query", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string sel = args.value("selector", "");
            if (tid.empty() || sel.empty()) return error_result("tab_id and selector required");
            if (cdp.is_available()) return json_result(cdp.query_selector_all(tid, sel));
            if (ext.is_connected()) {
                int itid = 0;
                try { itid = std::stoi(tid); } catch (...) { return error_result("Invalid tab_id"); }
                return json_result(ext.submit("query_selector", {{"tab_id", itid}, {"selector", sel}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 11. chrome_click
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_click",
         "Click an element on the page identified by a CSS selector.",
         {{"tab_id", "string", "Tab ID", true},
          {"selector", "string", "CSS selector of the element to click", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string sel = args.value("selector", "");
            if (tid.empty() || sel.empty()) return error_result("tab_id and selector required");
            if (cdp.is_available()) return json_result(cdp.click_element(tid, sel));
            if (ext.is_connected()) {
                int itid = std::stoi(tid);
                return json_result(ext.submit("click_element", {{"tab_id", itid}, {"selector", sel}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 12. chrome_type_text
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_type_text",
         "Type text into a form element (input, textarea) identified by CSS selector.",
         {{"tab_id", "string", "Tab ID", true},
          {"selector", "string", "CSS selector of the input element", true},
          {"text", "string", "Text to type into the element", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string sel = args.value("selector", "");
            std::string text = args.value("text", "");
            if (tid.empty() || sel.empty()) return error_result("tab_id and selector required");
            if (cdp.is_available()) return json_result(cdp.type_text(tid, sel, text));
            if (ext.is_connected()) {
                int itid = 0;
                try { itid = std::stoi(tid); } catch (...) { return error_result("Invalid tab_id"); }
                return json_result(ext.submit("type_text", {{"tab_id", itid}, {"selector", sel}, {"text", text}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 13. chrome_get_cookies
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_get_cookies",
         "Get cookies for a specific URL or the current page.",
         {{"tab_id", "string", "Tab ID (for CDP mode)", false, ""},
          {"url", "string", "URL to get cookies for (for extension mode)", false, ""}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string url = args.value("url", "");
            if (cdp.is_available() && !tid.empty()) return json_result(cdp.get_cookies(tid));
            if (ext.is_connected()) {
                return json_result(ext.submit("get_cookies", {{"url", url}}));
            }
            return error_result("Chrome is not reachable. Start Chrome with remote debugging or connect the extension.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 14. chrome_set_cookie
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_set_cookie",
         "Set a cookie.",
         {{"name", "string", "Cookie name", true},
          {"value", "string", "Cookie value", true},
          {"domain", "string", "Cookie domain", true},
          {"path", "string", "Cookie path", false, "/"},
          {"url", "string", "URL (for extension mode)", false, ""}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string name = args.value("name", "");
            std::string value = args.value("value", "");
            std::string domain = args.value("domain", "");
            std::string path = args.value("path", "/");
            if (cdp.is_available()) return json_result(cdp.set_cookie(name, value, domain, path));
            if (ext.is_connected()) {
                std::string url = args.value("url", "https://" + domain);
                return json_result(ext.submit("set_cookie", {
                    {"url", url}, {"name", name}, {"value", value}, {"domain", domain}, {"path", path}
                }));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 15. chrome_delete_cookies
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_delete_cookies",
         "Delete cookies by name and optional domain.",
         {{"name", "string", "Cookie name to delete", true},
          {"domain", "string", "Cookie domain", false, ""},
          {"url", "string", "URL (for extension mode)", false, ""}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string name = args.value("name", "");
            std::string domain = args.value("domain", "");
            if (cdp.is_available()) return json_result(cdp.delete_cookies(name, domain));
            if (ext.is_connected()) {
                std::string url = args.value("url", "");
                return json_result(ext.submit("delete_cookie", {{"url", url}, {"name", name}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 16. chrome_inject_css
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_inject_css",
         "Inject custom CSS stylesheet into a page.",
         {{"tab_id", "string", "Tab ID", true},
          {"css", "string", "CSS code to inject", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string css = args.value("css", "");
            if (tid.empty() || css.empty()) return error_result("tab_id and css required");
            if (cdp.is_available()) return json_result(cdp.inject_css(tid, css));
            if (ext.is_connected()) {
                int itid = 0;
                try { itid = std::stoi(tid); } catch (...) { return error_result("Invalid tab_id"); }
                return json_result(ext.submit("inject_css", {{"tab_id", itid}, {"css", css}}));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 17. chrome_wait_for
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_wait_for",
         "Wait for an element matching a CSS selector to appear on the page.",
         {{"tab_id", "string", "Tab ID", true},
          {"selector", "string", "CSS selector to wait for", true},
          {"timeout_ms", "integer", "Maximum wait time in milliseconds", false, 5000}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            std::string sel = args.value("selector", "");
            int timeout = args.value("timeout_ms", 5000);
            if (tid.empty() || sel.empty()) return error_result("tab_id and selector required");
            if (ext.is_connected()) {
                int itid = std::stoi(tid);
                return json_result(ext.submit("wait_for", {{"tab_id", itid}, {"selector", sel}, {"timeout_ms", timeout}}, timeout + 2000));
            }
            if (cdp.is_available()) {
                // CDP fallback: poll with evaluate_js
                std::string js = "new Promise((resolve) => {"
                    "const start = Date.now();"
                    "const check = () => {"
                    "  if (document.querySelector('" + sel + "')) return resolve({found: true, elapsed: Date.now()-start});"
                    "  if (Date.now()-start > " + std::to_string(timeout) + ") return resolve({found: false, elapsed: Date.now()-start});"
                    "  setTimeout(check, 100);"
                    "}; check();})";
                return json_result(cdp.evaluate_js(tid, js));
            }
            return error_result("Chrome is not reachable.");
         }});

    // ════════════════════════════════════════════════════════════════
    // 18. chrome_pdf
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_pdf",
         "Save the current page as a PDF document. Returns base64-encoded PDF. Requires CDP mode.",
         {{"tab_id", "string", "Tab ID", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            if (tid.empty()) return error_result("tab_id required");
            if (!cdp.is_available()) return error_result("PDF export requires CDP mode. Start Chrome with --remote-debugging-port=9222.");
            return json_result(cdp.print_to_pdf(tid));
         }});

    // ════════════════════════════════════════════════════════════════
    // 19. chrome_emulate_device
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_emulate_device",
         "Emulate a mobile/tablet device by overriding viewport dimensions and user-agent. Requires CDP.",
         {{"tab_id", "string", "Tab ID", true},
          {"width", "integer", "Viewport width", true},
          {"height", "integer", "Viewport height", true},
          {"device_scale", "number", "Device scale factor", false, 1.0},
          {"user_agent", "string", "Custom user-agent string", false, ""}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            int w = args.value("width", 375);
            int h = args.value("height", 812);
            double scale = args.value("device_scale", 1.0);
            std::string ua = args.value("user_agent", "");
            if (tid.empty()) return error_result("tab_id required");
            if (!cdp.is_available()) return error_result("Device emulation requires CDP mode.");
            return json_result(cdp.emulate_device(tid, w, h, scale, ua));
         }});

    // ════════════════════════════════════════════════════════════════
    // 20. chrome_get_console_log
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_get_console_log",
         "Retrieve recent console.log output from a tab. Requires CDP.",
         {{"tab_id", "string", "Tab ID", true}},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string tid = args.value("tab_id", "");
            if (tid.empty()) return error_result("tab_id required");
            if (!cdp.is_available()) return error_result("Console log requires CDP mode.");
            return json_result(cdp.get_console_log(tid));
         }});

    // ════════════════════════════════════════════════════════════════
    // 21. chrome_status
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_status",
         "Get the current status of the Chrome Bridge — which backends are available, "
         "extension connectivity, and connection mode.",
         {},
         [](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            bool cdp_ok = cdp.is_available();
            bool ext_ok = ext.is_connected();
            std::string mode = "none";
            if (cdp_ok && ext_ok) mode = "hybrid";
            else if (cdp_ok) mode = "cdp";
            else if (ext_ok) mode = "extension";

            return json_result({
                {"cdp_available", cdp_ok},
                {"extension_connected", ext_ok},
                {"active_mode", mode},
                {"extension_commands_executed", ext.commands_executed()},
                {"server_name", SERVER_NAME},
                {"server_version", SERVER_VERSION}
            });
         }});

    // ════════════════════════════════════════════════════════════════
    // 22. chrome_batch
    // ════════════════════════════════════════════════════════════════
    reg({"chrome_batch",
         "Execute multiple Chrome commands in sequence. Each command is a {tool, arguments} object. "
         "Prevalidates up to 64 non-nested commands, then stops on the first error. Earlier effects are not rolled back.",
         {{"commands", "string", "JSON array of {tool, arguments} objects as a string", true}},
         [this](const json& args, CDPClient& cdp, ExtensionBridge& ext) -> json {
            std::string cmds_str = args.value("commands", "[]");
            json cmds;
            try { cmds = json::parse(cmds_str); } catch (...) {
                return error_result("Invalid JSON in commands parameter");
            }
            if (!cmds.is_array()) return error_result("commands must be a JSON array");
            if (cmds.size() > 64) return error_result("At most 64 commands are allowed");

            // Validate the entire batch before any browser operation takes place.
            for (const auto& cmd : cmds) {
                if (!cmd.is_object() || !cmd.contains("tool") || !cmd["tool"].is_string())
                    return error_result("Each command requires a string tool name");
                const std::string name = cmd["tool"];
                if (name == "chrome_batch") return error_result("Nested batches are not allowed");
                const ToolDef* definition = nullptr;
                for (const auto& t : tools_) if (t.name == name) definition = &t;
                if (!definition) return error_result("Unknown tool: " + name);
                const auto invalid = validate_arguments(*definition, cmd.value("arguments", json::object()));
                if (!invalid.empty()) return error_result(name + ": " + invalid);
            }

            json results = json::array();
            for (auto& cmd : cmds) {
                std::string tool_name = cmd.value("tool", "");
                json tool_args = cmd.value("arguments", json::object());
                // Call handler directly to avoid deadlock (call_tool mutex is already held)
                json tool_result = error_result("Unknown tool: " + tool_name);
                for (auto& t : tools_) {
                    if (t.name == tool_name) {
                        try {
                            tool_result = t.handler(tool_args, cdp_, ext_);
                        } catch (const std::exception& e) {
                            tool_result = error_result(e.what());
                        }
                        break;
                    }
                }
                results.push_back(tool_result);
                if (tool_result.value("isError", false))
                    return json_result({{"success", false}, {"results", results},
                        {"completed", results.size() - 1}, {"failed_index", results.size() - 1},
                        {"error", "Batch stopped; earlier effects were not rolled back"}});
            }
            return json_result({{"results", results}, {"count", results.size()}});
         }});

    std::cerr << "[MCP] Registered " << tools_.size() << " tools." << std::endl;
}

} // namespace ag

#include "cdp_client.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>

namespace ag {

// ---------------------------------------------------------------------------
// TabInfo JSON helpers
// ---------------------------------------------------------------------------

void from_json(const json& j, TabInfo& t) {
    t.id = j.value("id", "");
    t.title = j.value("title", "");
    t.url = j.value("url", "");
    t.type = j.value("type", "");
    t.ws_url = j.value("webSocketDebuggerUrl", "");
    t.favicon_url = j.value("faviconUrl", "");
    t.description = j.value("description", "");
}

json to_json_obj(const TabInfo& t) {
    return json{
        {"id", t.id},
        {"title", t.title},
        {"url", t.url},
        {"type", t.type},
        {"webSocketDebuggerUrl", t.ws_url},
        {"faviconUrl", t.favicon_url},
        {"description", t.description}
    };
}

// ---------------------------------------------------------------------------
// CDPClient
// ---------------------------------------------------------------------------

CDPClient::CDPClient(const std::string& host, int port)
    : host_(host), port_(port) {}

json CDPClient::http_json_get(const std::string& path) {
    std::string body = http_get(host_, port_, path, 3000);
    if (body.empty()) {
        return json{{"error", "Empty response from CDP"}};
    }
    try {
        return json::parse(body);
    } catch (const std::exception& e) {
        return json{{"error", std::string("JSON parse error: ") + e.what()}, {"raw", body}};
    }
}

bool CDPClient::is_available() {
    try {
        json result = http_json_get("/json/version");
        return !result.contains("error");
    } catch (...) {
        return false;
    }
}

std::vector<TabInfo> CDPClient::list_tabs() {
    std::vector<TabInfo> tabs;
    try {
        json result = http_json_get("/json");
        if (result.is_array()) {
            for (const auto& item : result) {
                TabInfo tab;
                from_json(item, tab);
                if (tab.type == "page") {
                    tabs.push_back(tab);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[cdp] list_tabs error: " << e.what() << "\n";
    }
    return tabs;
}

std::optional<TabInfo> CDPClient::get_tab(const std::string& tab_id) {
    auto tabs = list_tabs();
    for (const auto& tab : tabs) {
        if (tab.id == tab_id) {
            return tab;
        }
    }
    return std::nullopt;
}

std::string CDPClient::find_ws_url(const std::string& tab_id) {
    auto tab = get_tab(tab_id);
    if (tab.has_value()) {
        return tab->ws_url;
    }
    std::cerr << "[cdp] Tab not found: " << tab_id << "\n";
    return "";
}

json CDPClient::create_tab(const std::string& url) {
    try {
        // URL-encode the target URL for the query parameter
        std::ostringstream encoded;
        for (unsigned char c : url) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') encoded << c;
            else encoded << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
        std::string path = "/json/new?" + encoded.str();
        std::string body = http_put(host_, port_, path, 3000);
        if (body.empty()) {
            return json{{"error", "Empty response when creating tab"}};
        }
        json result = json::parse(body);
        TabInfo tab;
        from_json(result, tab);
        return json{{"success", true}, {"tab", to_json_obj(tab)}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("create_tab failed: ") + e.what()}};
    }
}

json CDPClient::close_tab(const std::string& tab_id) {
    try {
        std::string path = "/json/close/" + tab_id;
        std::string body = http_get(host_, port_, path, 3000);
        return json{{"success", true}, {"message", body}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("close_tab failed: ") + e.what()}};
    }
}

// ---------------------------------------------------------------------------
// send_cdp_command: connect via WebSocket, send command, receive response
// ---------------------------------------------------------------------------

json CDPClient::send_cdp_command(const std::string& tab_id, const std::string& method, const json& params) {
    std::lock_guard<std::mutex> lock(mtx_);

    std::string ws_url = find_ws_url(tab_id);
    if (ws_url.empty()) {
        return json{{"error", "Could not find WebSocket URL for tab: " + tab_id}};
    }

    // Parse ws_url: ws://host:port/devtools/page/ID
    // Extract host, port, and path
    std::string ws_host = host_;
    int ws_port = port_;
    std::string ws_path = "/";

    // Strip "ws://" prefix
    std::string url = ws_url;
    if (url.substr(0, 5) == "ws://") {
        url = url.substr(5);
    }

    // Split host:port/path
    auto slash_pos = url.find('/');
    if (slash_pos != std::string::npos) {
        ws_path = url.substr(slash_pos);
        url = url.substr(0, slash_pos);
    }

    auto colon_pos = url.find(':');
    if (colon_pos != std::string::npos) {
        ws_host = url.substr(0, colon_pos);
        try {
            ws_port = std::stoi(url.substr(colon_pos + 1));
        } catch (...) {}
    }

    if (ws_host == "localhost") ws_host = "127.0.0.1";
    if (ws_host != host_ || ws_port != port_ || ws_url.rfind("ws://", 0) != 0)
        return json{{"error", "CDP advertised an unexpected WebSocket destination"}};
    WebSocketClient ws;
    if (!ws.connect(ws_host, ws_port, ws_path)) {
        return json{{"error", "Failed to connect WebSocket to " + ws_url}};
    }

    int cmd_id = next_id_++;
    json command = {
        {"id", cmd_id},
        {"method", method},
        {"params", params}
    };

    if (!ws.send_text(command.dump())) {
        return json{{"error", "Failed to send CDP command"}};
    }

    // Read responses until we get our matching id
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (int attempts = 0; attempts < 50 && std::chrono::steady_clock::now() < deadline; ++attempts) {
        std::string response_text = ws.recv_text(10000);
        if (response_text.empty()) {
            return json{{"error", "CDP result unavailable; inspect state before retrying mutations"}};
        }

        try {
            json response = json::parse(response_text);
            // Check if this is our response (matching id)
            if (response.contains("id") && response["id"].get<int>() == cmd_id) {
                if (response.contains("error")) {
                    return json{{"error", response["error"]["message"].get<std::string>()}};
                }
                return response.value("result", json::object());
            }
            // Otherwise it's an event, keep reading
        } catch (const std::exception& e) {
            return json{{"error", std::string("Failed to parse CDP response: ") + e.what()}};
        }
    }

    return json{{"error", "Timeout waiting for CDP response"}};
}

// ---------------------------------------------------------------------------
// Page operations
// ---------------------------------------------------------------------------

json CDPClient::navigate(const std::string& tab_id, const std::string& url) {
    try {
        json result = send_cdp_command(tab_id, "Page.navigate", {{"url", url}});
        if (result.contains("error")) return result;
        return json{{"success", true}, {"frameId", result.value("frameId", "")}, {"loaderId", result.value("loaderId", "")}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("navigate failed: ") + e.what()}};
    }
}

json CDPClient::reload(const std::string& tab_id, bool ignore_cache) {
    try {
        json result = send_cdp_command(tab_id, "Page.reload", {{"ignoreCache", ignore_cache}});
        if (result.contains("error")) return result;
        return json{{"success", true}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("reload failed: ") + e.what()}};
    }
}

json CDPClient::evaluate_js(const std::string& tab_id, const std::string& expression) {
    try {
        json result = send_cdp_command(tab_id, "Runtime.evaluate", {
            {"expression", expression},
            {"returnByValue", true}
        });
        if (result.contains("error")) return result;
        if (result.contains("exceptionDetails")) {
            auto& exc = result["exceptionDetails"];
            std::string msg = exc.value("text", "Unknown JS error");
            if (exc.contains("exception") && exc["exception"].contains("description")) {
                msg = exc["exception"]["description"].get<std::string>();
            }
            return json{{"error", msg}};
        }
        json ret = {{"success", true}};
        if (result.contains("result")) {
            ret["type"] = result["result"].value("type", "undefined");
            if (result["result"].contains("value")) {
                ret["value"] = result["result"]["value"];
            }
        }
        return ret;
    } catch (const std::exception& e) {
        return json{{"error", std::string("evaluate_js failed: ") + e.what()}};
    }
}

json CDPClient::capture_screenshot(const std::string& tab_id, const std::string& format, int quality) {
    try {
        json params = {{"format", format}};
        if (format == "jpeg" || format == "webp") {
            params["quality"] = quality;
        }
        json result = send_cdp_command(tab_id, "Page.captureScreenshot", params);
        if (result.contains("error")) return result;
        return json{{"success", true}, {"data", result.value("data", "")}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("capture_screenshot failed: ") + e.what()}};
    }
}

json CDPClient::print_to_pdf(const std::string& tab_id) {
    try {
        json result = send_cdp_command(tab_id, "Page.printToPDF", json::object());
        if (result.contains("error")) return result;
        return json{{"success", true}, {"data", result.value("data", "")}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("print_to_pdf failed: ") + e.what()}};
    }
}

// ---------------------------------------------------------------------------
// DOM operations
// ---------------------------------------------------------------------------

json CDPClient::query_selector(const std::string& tab_id, const std::string& selector) {
    try {
        // Enable DOM domain
        send_cdp_command(tab_id, "DOM.enable", json::object());

        // Get document root
        json doc_result = send_cdp_command(tab_id, "DOM.getDocument", json::object());
        if (doc_result.contains("error")) return doc_result;

        int root_node_id = doc_result["root"]["nodeId"].get<int>();

        // Query selector
        json qs_result = send_cdp_command(tab_id, "DOM.querySelector", {
            {"nodeId", root_node_id},
            {"selector", selector}
        });
        if (qs_result.contains("error")) return qs_result;

        int node_id = qs_result.value("nodeId", 0);
        if (node_id == 0) {
            return json{{"error", "Element not found: " + selector}};
        }

        // Get outer HTML of the found node
        json html_result = send_cdp_command(tab_id, "DOM.getOuterHTML", {{"nodeId", node_id}});
        if (html_result.contains("error")) return html_result;

        return json{{"success", true}, {"nodeId", node_id}, {"outerHTML", html_result.value("outerHTML", "")}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("query_selector failed: ") + e.what()}};
    }
}

json CDPClient::query_selector_all(const std::string& tab_id, const std::string& selector) {
    try {
        send_cdp_command(tab_id, "DOM.enable", json::object());

        json doc_result = send_cdp_command(tab_id, "DOM.getDocument", json::object());
        if (doc_result.contains("error")) return doc_result;

        int root_node_id = doc_result["root"]["nodeId"].get<int>();

        json qs_result = send_cdp_command(tab_id, "DOM.querySelectorAll", {
            {"nodeId", root_node_id},
            {"selector", selector}
        });
        if (qs_result.contains("error")) return qs_result;

        json node_ids = qs_result.value("nodeIds", json::array());
        json nodes = json::array();

        for (const auto& nid : node_ids) {
            json html_result = send_cdp_command(tab_id, "DOM.getOuterHTML", {{"nodeId", nid.get<int>()}});
            nodes.push_back({
                {"nodeId", nid.get<int>()},
                {"outerHTML", html_result.value("outerHTML", "")}
            });
        }

        return json{{"success", true}, {"count", nodes.size()}, {"nodes", nodes}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("query_selector_all failed: ") + e.what()}};
    }
}

json CDPClient::get_page_content(const std::string& tab_id) {
    try {
        json result = send_cdp_command(tab_id, "Runtime.evaluate", {
            {"expression", "document.documentElement.outerHTML"},
            {"returnByValue", true}
        });
        if (result.contains("error")) return result;
        if (result.contains("result") && result["result"].contains("value")) {
            return json{{"success", true}, {"content", result["result"]["value"]}};
        }
        return json{{"error", "Failed to get page content"}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("get_page_content failed: ") + e.what()}};
    }
}

json CDPClient::click_element(const std::string& tab_id, const std::string& selector) {
    try {
        // Escape selector for JS
        std::string escaped = selector;
        // Simple escape of single quotes
        size_t pos = 0;
        while ((pos = escaped.find('\'', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "\\'");
            pos += 2;
        }

        std::string js = "(() => { const el = document.querySelector('" + escaped + "'); "
                         "if (!el) return {error: 'Element not found'}; "
                         "el.click(); return {clicked: true}; })()";

        json result = send_cdp_command(tab_id, "Runtime.evaluate", {
            {"expression", js},
            {"returnByValue", true}
        });
        if (result.contains("error")) return result;
        if (result.contains("result") && result["result"].contains("value")) {
            json val = result["result"]["value"];
            if (val.contains("error")) {
                return json{{"error", val["error"]}};
            }
            return json{{"success", true}};
        }
        return json{{"success", true}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("click_element failed: ") + e.what()}};
    }
}

json CDPClient::type_text(const std::string& tab_id, const std::string& selector, const std::string& text) {
    try {
        // Escape for JS strings
        std::string esc_sel = selector;
        std::string esc_text = text;
        auto js_escape = [](std::string& s) {
            size_t p = 0;
            while ((p = s.find('\\', p)) != std::string::npos) { s.replace(p, 1, "\\\\"); p += 2; }
            p = 0;
            while ((p = s.find('\'', p)) != std::string::npos) { s.replace(p, 1, "\\'"); p += 2; }
            p = 0;
            while ((p = s.find('\n', p)) != std::string::npos) { s.replace(p, 1, "\\n"); p += 2; }
            p = 0;
            while ((p = s.find('\r', p)) != std::string::npos) { s.replace(p, 1, "\\r"); p += 2; }
        };
        js_escape(esc_sel);
        js_escape(esc_text);

        std::string js = "(() => { "
                         "const el = document.querySelector('" + esc_sel + "'); "
                         "if (!el) return {error: 'Element not found'}; "
                         "el.focus(); "
                         "el.value = '" + esc_text + "'; "
                         "el.dispatchEvent(new Event('input', {bubbles: true})); "
                         "el.dispatchEvent(new Event('change', {bubbles: true})); "
                         "return {typed: true}; })()";

        json result = send_cdp_command(tab_id, "Runtime.evaluate", {
            {"expression", js},
            {"returnByValue", true}
        });
        if (result.contains("error")) return result;
        if (result.contains("result") && result["result"].contains("value")) {
            json val = result["result"]["value"];
            if (val.contains("error")) {
                return json{{"error", val["error"]}};
            }
        }
        return json{{"success", true}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("type_text failed: ") + e.what()}};
    }
}

json CDPClient::inject_css(const std::string& tab_id, const std::string& css) {
    try {
        // Escape CSS for JS template literal
        std::string escaped = css;
        size_t pos = 0;
        while ((pos = escaped.find('`', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "\\`");
            pos += 2;
        }
        pos = 0;
        while ((pos = escaped.find("${", pos)) != std::string::npos) {
            escaped.replace(pos, 2, "\\${");
            pos += 3;
        }

        std::string js = "(() => { "
                         "const style = document.createElement('style'); "
                         "style.textContent = `" + escaped + "`; "
                         "document.head.appendChild(style); "
                         "return {injected: true}; })()";

        json result = send_cdp_command(tab_id, "Runtime.evaluate", {
            {"expression", js},
            {"returnByValue", true}
        });
        if (result.contains("error")) return result;
        return json{{"success", true}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("inject_css failed: ") + e.what()}};
    }
}

// ---------------------------------------------------------------------------
// Cookies
// ---------------------------------------------------------------------------

json CDPClient::get_cookies(const std::string& tab_id) {
    try {
        json result = send_cdp_command(tab_id, "Network.getCookies", json::object());
        if (result.contains("error")) return result;
        return json{{"success", true}, {"cookies", result.value("cookies", json::array())}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("get_cookies failed: ") + e.what()}};
    }
}

json CDPClient::set_cookie(const std::string& name, const std::string& value,
                           const std::string& domain, const std::string& path) {
    try {
        // set_cookie doesn't need a specific tab, but we need a WS connection.
        // Use any available tab.
        auto tabs = list_tabs();
        if (tabs.empty()) {
            return json{{"error", "No tabs available to set cookie"}};
        }

        json result = send_cdp_command(tabs[0].id, "Network.setCookie", {
            {"name", name},
            {"value", value},
            {"domain", domain},
            {"path", path}
        });
        if (result.contains("error")) return result;
        return json{{"success", result.value("success", false)}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("set_cookie failed: ") + e.what()}};
    }
}

json CDPClient::delete_cookies(const std::string& name, const std::string& domain) {
    try {
        auto tabs = list_tabs();
        if (tabs.empty()) {
            return json{{"error", "No tabs available to delete cookies"}};
        }

        json params = {{"name", name}};
        if (!domain.empty()) {
            params["domain"] = domain;
        }

        json result = send_cdp_command(tabs[0].id, "Network.deleteCookies", params);
        if (result.contains("error")) return result;
        return json{{"success", true}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("delete_cookies failed: ") + e.what()}};
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

json CDPClient::get_console_log(const std::string& tab_id) {
    try {
        // Note: This requires that a console log buffer has been previously injected
        // into the page via JavaScript (e.g., overriding console.log to push to
        // console._log_buffer). Without prior injection, this will return undefined.
        json result = send_cdp_command(tab_id, "Runtime.evaluate", {
            {"expression", "console._log_buffer || []"},
            {"returnByValue", true}
        });
        if (result.contains("error")) return result;
        if (result.contains("result") && result["result"].contains("value")) {
            return json{{"success", true}, {"logs", result["result"]["value"]}};
        }
        return json{{"success", true}, {"logs", json::array()}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("get_console_log failed: ") + e.what()}};
    }
}

// ---------------------------------------------------------------------------
// Emulation
// ---------------------------------------------------------------------------

json CDPClient::emulate_device(const std::string& tab_id, int width, int height,
                               double device_scale, const std::string& user_agent) {
    try {
        json result = send_cdp_command(tab_id, "Emulation.setDeviceMetricsOverride", {
            {"width", width},
            {"height", height},
            {"deviceScaleFactor", device_scale},
            {"mobile", true}
        });
        if (result.contains("error")) return result;

        if (!user_agent.empty()) {
            json ua_result = send_cdp_command(tab_id, "Emulation.setUserAgentOverride", {
                {"userAgent", user_agent}
            });
            if (ua_result.contains("error")) return ua_result;
        }

        return json{{"success", true}};
    } catch (const std::exception& e) {
        return json{{"error", std::string("emulate_device failed: ") + e.what()}};
    }
}

} // namespace ag

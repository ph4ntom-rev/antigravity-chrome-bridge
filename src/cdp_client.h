#pragma once
#include "ws_client.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <optional>

namespace ag {

using json = nlohmann::json;

struct TabInfo {
    std::string id;
    std::string title;
    std::string url;
    std::string type;
    std::string ws_url;
    std::string favicon_url;
    std::string description;
};

void from_json(const json& j, TabInfo& t);
json to_json_obj(const TabInfo& t);

class CDPClient {
public:
    CDPClient(const std::string& host = "127.0.0.1", int port = 9222);

    bool is_available();

    // Tab management
    std::vector<TabInfo> list_tabs();
    std::optional<TabInfo> get_tab(const std::string& tab_id);
    json create_tab(const std::string& url = "about:blank");
    json close_tab(const std::string& tab_id);

    // Page operations
    json navigate(const std::string& tab_id, const std::string& url);
    json reload(const std::string& tab_id, bool ignore_cache = false);
    json evaluate_js(const std::string& tab_id, const std::string& expression);
    json capture_screenshot(const std::string& tab_id, const std::string& format = "jpeg", int quality = 85);
    json print_to_pdf(const std::string& tab_id);

    // DOM operations
    json query_selector(const std::string& tab_id, const std::string& selector);
    json query_selector_all(const std::string& tab_id, const std::string& selector);
    json get_page_content(const std::string& tab_id);
    json click_element(const std::string& tab_id, const std::string& selector);
    json type_text(const std::string& tab_id, const std::string& selector, const std::string& text);
    json inject_css(const std::string& tab_id, const std::string& css);

    // Cookies
    json get_cookies(const std::string& tab_id);
    json set_cookie(const std::string& name, const std::string& value, const std::string& domain, const std::string& path = "/");
    json delete_cookies(const std::string& name, const std::string& domain = "");

    // Network & Console
    json get_console_log(const std::string& tab_id);

    // Emulation
    json emulate_device(const std::string& tab_id, int width, int height, double device_scale = 1.0, const std::string& user_agent = "");

private:
    std::string host_;
    int port_;
    int next_id_ = 1;
    std::mutex mtx_;

    json send_cdp_command(const std::string& tab_id, const std::string& method, const json& params = json::object());
    std::string find_ws_url(const std::string& tab_id);
    json http_json_get(const std::string& path);
};

} // namespace ag

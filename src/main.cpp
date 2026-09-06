#include "config.h"
#include "ws_client.h"
#include "cdp_client.h"
#include "ext_bridge.h"
#include "mcp_tools.h"
#include "mcp_resources.h"
#include "mcp_core.h"
#include "security.h"
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        std::string token_file, extension_id;
        int bridge_port = ag::BRIDGE_PORT, cdp_port = ag::CDP_PORT;
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--help") {
                std::cout << "Usage: antigravity-chrome-bridge [--token-file PATH --extension-id ID] [--cdp-port PORT] [--bridge-port PORT]\n"
                    "Create a private pairing file: antigravity-chrome-bridge --init-token PATH\n"
                    "Without --token-file, only CDP is enabled and no extension HTTP port is opened.\n";
                return 0;
            }
            if (++i >= argc) throw std::invalid_argument("Missing option value");
            std::string value = argv[i];
            if (option == "--init-token") {
                if (argc != 3) throw std::invalid_argument("Use --init-token on its own");
                ag::create_token_file(value);
                std::cerr << "Created private pairing file. Its contents are not printed.\n";
                return 0;
            } else if (option == "--token-file") token_file = value;
            else if (option == "--extension-id") extension_id = value;
            else if (option == "--bridge-port" || option == "--cdp-port") {
                size_t consumed = 0;
                int port = std::stoi(value, &consumed);
                if (consumed != value.size() || port < 1 || port > 65535) throw std::invalid_argument("Invalid port");
                (option == "--bridge-port" ? bridge_port : cdp_port) = port;
            } else throw std::invalid_argument("Unknown option");
        }
        if (!extension_id.empty() && token_file.empty()) throw std::invalid_argument("Extension ID requires --token-file");
        ag::net_init();
        ag::CDPClient cdp("127.0.0.1", cdp_port);
        ag::ExtensionBridge bridge("127.0.0.1", bridge_port,
            token_file.empty() ? "" : ag::read_token_file(token_file),
            extension_id.empty() ? "" : "chrome-extension://" + extension_id);
        if (!token_file.empty()) {
            bridge.start();
            std::cerr << "[Bridge] Authenticated extension channel on 127.0.0.1:" << bridge.port() << "\n";
        }
        ag::ToolRegistry tools(cdp, bridge);
        ag::ResourceRegistry resources(cdp, bridge);
        ag::MCPServer mcp(tools, resources);
        mcp.run();
        bridge.stop();
        ag::net_cleanup();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[Bridge] " << error.what() << "\n";
        return 1;
    }
}

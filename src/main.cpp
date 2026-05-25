#include "config.h"
#include "ws_client.h"
#include "cdp_client.h"
#include "ext_bridge.h"
#include "mcp_tools.h"
#include "mcp_resources.h"
#include "mcp_core.h"
#include <iostream>
#include <thread>
#include <csignal>

static ag::ExtensionBridge* g_bridge = nullptr;

void signal_handler(int sig) {
    std::cerr << "[Main] Signal " << sig << " received, shutting down..." << std::endl;
    if (g_bridge) g_bridge->stop();
    ag::net_cleanup();
    std::_Exit(0);
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize networking (Winsock on Windows)
    ag::net_init();

    std::cerr << "╔══════════════════════════════════════════════════╗" << std::endl;
    std::cerr << "║   Antigravity Chrome Bridge v2.0  [C++ Native]  ║" << std::endl;
    std::cerr << "╠══════════════════════════════════════════════════╣" << std::endl;
    std::cerr << "║  MCP:  stdio (JSON-RPC 2.0)                    ║" << std::endl;
    std::cerr << "║  HTTP: 127.0.0.1:13371 (Extension Bridge)      ║" << std::endl;
    std::cerr << "║  CDP:  127.0.0.1:9222  (Chrome DevTools)       ║" << std::endl;
    std::cerr << "╚══════════════════════════════════════════════════╝" << std::endl;

    // Create backends
    ag::CDPClient cdp("127.0.0.1", ag::CDP_PORT);
    ag::ExtensionBridge bridge("127.0.0.1", ag::BRIDGE_PORT);
    g_bridge = &bridge;

    // Start extension bridge HTTP server in background thread
    bridge.start();

    // Probe backends
    if (cdp.is_available()) {
        std::cerr << "[Main] CDP backend: ONLINE (Chrome debug port detected)" << std::endl;
    } else {
        std::cerr << "[Main] CDP backend: OFFLINE (start Chrome with --remote-debugging-port=9222)" << std::endl;
    }
    std::cerr << "[Main] Extension bridge: LISTENING (waiting for extension to connect)" << std::endl;

    // Create MCP tool and resource registries
    ag::ToolRegistry tools(cdp, bridge);
    ag::ResourceRegistry resources(cdp, bridge);

    // Run MCP server on main thread (reads stdin, writes stdout)
    ag::MCPServer mcp(tools, resources);
    mcp.run();

    // Cleanup
    bridge.stop();
    ag::net_cleanup();

    std::cerr << "[Main] Shutdown complete." << std::endl;
    return 0;
}

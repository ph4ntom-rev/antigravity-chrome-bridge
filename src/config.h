#pragma once
#include <string>

namespace ag {

constexpr int CDP_PORT = 9222;
constexpr int BRIDGE_PORT = 13371;
constexpr const char* BRIDGE_HOST = "127.0.0.1";
constexpr int CDP_TIMEOUT_MS = 3000;
constexpr int WS_TIMEOUT_MS = 5000;
constexpr int EXT_CMD_TIMEOUT_MS = 10000;
constexpr int MAX_BODY_SIZE = 10 * 1024 * 1024;
constexpr const char* SERVER_NAME = "Antigravity Chrome Bridge";
constexpr const char* SERVER_VERSION = "2.1.0";
constexpr const char* MCP_PROTOCOL_VERSION = "2024-11-05";

} // namespace ag

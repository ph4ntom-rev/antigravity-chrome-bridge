# Antigravity Chrome Bridge

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![MCP](https://img.shields.io/badge/MCP-2024--11--05-purple.svg)](https://modelcontextprotocol.io)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)]()

**Native C++ MCP server for Chrome automation — zero dependencies, instant startup, 22 tools.**

The only browser automation MCP server written in native C++. Single binary, no Node.js, no Python, no runtime dependencies. Works with any MCP client (Claude Desktop, Cursor, Antigravity, etc.)

```
┌─────────────────────────────────────────────────────────────┐
│  antigravity-chrome-bridge.exe (~1.4MB)                     │
│                                                             │
│  ┌──── MCP Server (stdio JSON-RPC 2.0) ──── PRIMARY ────┐  │
│  │  22 tools · 3 resources · auto-detection              │  │
│  └───────────────────┬───────────────────┬───────────────┘  │
│                      │                   │                  │
│  ┌─── CDP Client ────┘  ┌── Ext Bridge ──┘                 │
│  │ WebSocket → :9222    │ HTTP → :13371                     │
│  └──────────────────┘   └──────────────────┘                │
│                                                             │
│  Language: C++17 · Libs: nlohmann/json (header-only)        │
│  Build: CMake 3.20+ · Cross-platform                       │
└─────────────────────────────────────────────────────────────┘
```

---

## Why This Project?

Every existing Chrome MCP server is written in TypeScript or Python. This one is different:

| | TypeScript MCP servers | **Antigravity Chrome Bridge** |
|---|---|---|
| **Startup** | ~800ms | **~5ms** |
| **Memory** | ~40–80MB | **~5MB** |
| **Tool call overhead** | ~15ms | **<1ms** |
| **Binary size** | ~50MB (node_modules) | **~1.4MB** |
| **Dependencies** | Node.js 18+ | **None** |
| **Browser profile** | Isolated (Puppeteer) | **Real profile** (Extension mode) |

---

## 🛠 22 MCP Tools

All tools auto-detect the best backend (CDP → Extension fallback):

| Tool | Description |
|------|-------------|
| `chrome_list_tabs` | List all open tabs |
| `chrome_get_tab` | Get detailed tab info by ID |
| `chrome_create_tab` | Open a new tab |
| `chrome_close_tab` | Close a tab |
| `chrome_navigate` | Navigate tab to URL |
| `chrome_reload` | Reload with optional cache bypass |
| `chrome_evaluate_js` | Execute JavaScript in tab |
| `chrome_get_page_content` | Full page HTML or text |
| `chrome_screenshot` | Capture visible tab as JPEG/PNG |
| `chrome_query_selector` | Find elements by CSS selector |
| `chrome_click` | Click element by selector |
| `chrome_type_text` | Type text into input fields |
| `chrome_get_cookies` | Retrieve cookies |
| `chrome_set_cookie` | Set a cookie |
| `chrome_delete_cookies` | Delete cookies |
| `chrome_inject_css` | Inject custom CSS |
| `chrome_wait_for` | Wait for element to appear |
| `chrome_pdf` | Export page as PDF (CDP only) |
| `chrome_emulate_device` | Mobile/tablet emulation (CDP only) |
| `chrome_get_console_log` | Console output (CDP only) |
| `chrome_status` | Bridge status & diagnostics |
| `chrome_batch` | Execute multiple commands in sequence |

---

## 🚀 Quick Start

### Option A: Extension Bridge Mode (recommended)

Works on any Chrome profile without restarting Chrome.

**1. Start the bridge:**
```
antigravity-chrome-bridge.exe
```

**2. Install the extension:**
1. Open `chrome://extensions/`
2. Enable "Developer mode"
3. Click "Load unpacked" → select `chrome_extension/` folder
4. The popup should show "Connected" ✅

### Option B: CDP Direct Mode

For headless automation and full CDP access.

**1. Start Chrome with debug port:**
```powershell
& "C:\Program Files\Google\Chrome\Application\chrome.exe" --remote-debugging-port=9222
```

**2. Start the bridge:**
```
antigravity-chrome-bridge.exe
```
The bridge auto-detects CDP and connects directly.

---

## 🔌 MCP Configuration

Add to your MCP client configuration:

```json
{
  "mcpServers": {
    "chrome_bridge": {
      "command": "/path/to/antigravity-chrome-bridge",
      "args": []
    }
  }
}
```

---

## 🏗 Building from Source

### Prerequisites
- CMake 3.20+
- C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+)

### Linux / macOS
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Windows (MSVC)
```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Cross-compile for Windows (WSL + MinGW)
```bash
bash build.sh
```

Output: `build-win64/antigravity-chrome-bridge.exe`

---

## 🔧 Architecture

The MCP protocol is the **native core** — not a wrapper around REST APIs.

```
AI Agent ←─ stdio JSON-RPC ─→ MCPServer
                                  │
                    ┌─────────────┼─────────────┐
                    ▼                             ▼
              CDPClient                    ExtensionBridge
           (WebSocket:9222)              (HTTP Server:13371)
                    │                             │
                    ▼                             ▼
              Chrome CDP                  Chrome Extension
           (debug port)               (background.js polling)
```

**Two backends, one interface:**
- **CDP mode** connects via WebSocket to Chrome's DevTools debug port. Supports PDF, device emulation, console logs.
- **Extension mode** uses a Chrome Extension that polls an embedded HTTP server. Works with any Chrome profile without restart.

Tools operate **directly** on CDP/Extension engines — zero intermediate HTTP hops within the process.

---

## 📂 Project Structure

```
antigravity-chrome-bridge/
├── src/
│   ├── main.cpp           # Entry point: MCP stdio + HTTP bridge thread
│   ├── mcp_core.h/cpp     # JSON-RPC 2.0 protocol engine
│   ├── mcp_tools.h/cpp    # 22 tool definitions & handlers
│   ├── mcp_resources.h/cpp # Live MCP resources
│   ├── cdp_client.h/cpp   # Chrome DevTools Protocol WebSocket client
│   ├── ext_bridge.h/cpp   # Extension HTTP bridge + command queue
│   ├── ws_client.h/cpp    # Minimal RFC 6455 WebSocket client
│   └── config.h           # Constants & configuration
├── chrome_extension/
│   ├── manifest.json      # Manifest V3
│   ├── background.js      # Service worker (16 command handlers)
│   ├── popup.html         # Glassmorphism status popup
│   └── popup.js           # Popup state manager
├── legacy/                # Archived Python v1
├── CMakeLists.txt         # Build system
├── build.sh               # WSL cross-compile script
├── LICENSE                # Apache 2.0
└── README.md
```

---

## 🤝 Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## 🔒 Security

For security concerns, please see [SECURITY.md](SECURITY.md).

## 📄 License

This project is licensed under the Apache License 2.0 — see the [LICENSE](LICENSE) file for details.

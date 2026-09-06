# Antigravity Chrome Bridge

A C++17 MCP server with 22 Chrome automation tools over stdio. It connects to a dedicated Chrome debugging profile, or to a paired Manifest V3 extension. No Python or Node interpreter is needed to run the compiled server; builds use pinned nlohmann/json and cpp-httplib libraries.

## Start with a dedicated browser profile

Launch Chrome with a separate profile and loopback remote debugging:

```text
chrome --remote-debugging-address=127.0.0.1 --remote-debugging-port=9222 --user-data-dir=/absolute/path/to/bridge-profile
```

Configure your MCP client to launch the binary using stdio:

```json
{"mcpServers":{"chrome":{"command":"/absolute/path/to/antigravity-chrome-bridge","args":[]}}}
```

Use an absolute executable path (with `.exe` and escaped backslashes on Windows). The default launch opens no extension HTTP listener. CDP is a privileged browser interface without authentication; use a disposable profile and keep its port local.

## Pair the extension

1. Load `chrome_extension/` as an unpacked extension in Chrome's extensions page. Copy its extension ID.
2. Create a pairing file in a private directory outside the repository:

   ```text
   antigravity-chrome-bridge --init-token /absolute/private/path/bridge-token
   ```

   The file is created with owner-only permissions and is never overwritten. The secret is not printed.
3. Open that file locally and paste its contents into the extension popup's pairing field. Save it. The extension stores the token in local storage restricted to trusted extension contexts.
4. Add these arguments to the MCP client's server configuration:

   ```json
   ["--token-file","/absolute/private/path/bridge-token","--extension-id","your32characterextensionid"]
   ```

The server binds `127.0.0.1:13371`. Every operational HTTP route requires the bearer token; browser origins must match the configured extension exactly. The extension uses port 13371; the server's `--bridge-port` override is intended for custom clients and tests. `--cdp-port` changes the CDP destination.

**Migration:** earlier releases accepted unauthenticated extension requests. Existing installations must pair the updated extension and configure the new arguments. To revoke a pairing, stop the server, create a new token file and pair again. Never share tokens, commit them or paste them into an issue.

## Tools

Backends prefer CDP when available, then the connected extension. Some operations require CDP.

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


`chrome_batch` prevalidates all commands (maximum 64, no nested batches), then stops at the first runtime error. Earlier browser changes remain; it is not a transaction. Tool failures set MCP `isError`, including failures returned by the extension.

Extension commands expire after 10 seconds by default. `delivery_state: not_delivered` means a queued command was removed before dispatch. `uncertain` means dispatch happened but confirmation was lost: inspect the page before repeating a mutation. There is no automatic retry of a dispatched command. Expiration prevents a new execution from starting; it cannot undo or cancel browser work already in progress.

## Build and verify

Requires CMake 3.20+, a C++17 compiler and network access for the two hash-pinned source archives. Windows builds target Windows 10+. Linux, Windows and macOS builds run in CI. Compiler/runtime libraries remain platform-specific; MinGW release builds link their runtime statically.

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j4
ctest --test-dir build -C Release --output-on-failure
node tests/test_extension.js
```

Python 3 is needed for tests and benchmarks; Node is needed only for extension tests. Use `-DBUILD_TESTING=OFF` for a build without the Python test dependency. For Ninja on Windows add `-G Ninja`. CTest checks authentication, protocol recovery, argument validation, extension result handling and queued/dispatched timeouts. If Chrome is installed, it also creates a temporary headless profile and exercises real DOM typing and clicking. Set `CHROME_BINARY` to its executable when discovery is unavailable. No existing profile is used. The extension tests simulate browser APIs; loading and pairing the extension in the Chrome UI remains a manual check.

Transport limits include a 1 MiB stdio message, 10 MiB HTTP/WebSocket payload, bounded extension queue and HTTP workers, and finite network timeouts. HTTP/WebSocket parsing uses cpp-httplib rather than project-specific socket framing.

See [benchmark methodology and local measurements](benchmarks/README.md), [security boundaries](SECURITY.md) and [third-party notices](THIRD_PARTY_NOTICES.md). The project is licensed under [Apache 2.0](LICENSE).

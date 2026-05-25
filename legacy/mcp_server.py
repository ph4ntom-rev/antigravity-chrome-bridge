import json
import urllib.request
import urllib.error
from mcp.server.fastmcp import FastMCP

# Define the FastMCP server
mcp = FastMCP("Antigravity Chrome Bridge")

CHROME_DEBUG_URL = "http://127.0.0.1:9222"
BRIDGE_SERVER_URL = "http://127.0.0.1:13371"

# Helper for bridge REST calls
def _bridge_request(endpoint: str, method: str = "GET", data: dict = None) -> dict:
    url = f"{BRIDGE_SERVER_URL}{endpoint}"
    try:
        req_data = None
        headers = {}
        if data is not None:
            req_data = json.dumps(data).encode("utf-8")
            headers = {"Content-Type": "application/json"}
            
        req = urllib.request.Request(url, data=req_data, headers=headers, method=method)
        with urllib.request.urlopen(req, timeout=12) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            err_body = json.loads(e.read().decode("utf-8"))
            return {"error": err_body.get("error", f"HTTP Error {e.code}")}
        except Exception:
            return {"error": f"HTTP Error {e.code}: {e.reason}"}
    except urllib.error.URLError as e:
        return {"error": f"Cannot connect to Bridge server at {BRIDGE_SERVER_URL}. Is it running? Error: {e.reason}"}
    except Exception as e:
        return {"error": f"Unexpected error: {str(e)}"}


# ==========================================
# 1. CDP-BASED CHROME AUTOMATION
# (Requires Chrome started with --remote-debugging-port=9222)
# ==========================================

@mcp.tool()
def chrome_cdp_list_tabs() -> dict:
    """Retrieve all open Chrome tabs via the Chrome DevTools Protocol (CDP).
    
    Note: Requires Chrome to be running with --remote-debugging-port=9222.
    """
    try:
        req_obj = urllib.request.Request(f"{CHROME_DEBUG_URL}/json")
        with urllib.request.urlopen(req_obj, timeout=2) as response:
            tabs = json.loads(response.read().decode())
            return {"tabs": tabs, "count": len(tabs)}
    except urllib.error.URLError:
        return {
            "error": "Cannot connect to Chrome debug port.",
            "instructions": "Make sure Chrome is running and started with remote debugging enabled. "
                            "Example: chrome.exe --remote-debugging-port=9222"
        }

@mcp.tool()
def chrome_cdp_evaluate_js(tab_id: str, js_code: str = "document.body.innerText") -> dict:
    """Evaluate JavaScript in a specific tab using Chrome DevTools Protocol (CDP) WebSocket connection.
    
    Parameters:
    - tab_id: The ID of the tab to evaluate JS in (get this from chrome_cdp_list_tabs).
    - js_code: The JavaScript code to execute.
    """
    try:
        import websocket
    except ImportError:
        return {
            "error": "websocket-client library is not installed in the bridge's Python environment.",
            "instructions": "Please run: pip install websocket-client"
        }
        
    # Get tabs to retrieve WebSocket URL
    try:
        req_obj = urllib.request.Request(f"{CHROME_DEBUG_URL}/json")
        with urllib.request.urlopen(req_obj, timeout=2) as response:
            tabs = json.loads(response.read().decode())
    except urllib.error.URLError:
        return {"error": "Cannot connect to Chrome debug port."}
        
    ws_url = next((t.get("webSocketDebuggerUrl") for t in tabs if t.get("id") == tab_id), None)
    if not ws_url:
        return {"error": f"Tab with ID {tab_id} not found or has no debugger URL."}
        
    try:
        ws = websocket.create_connection(ws_url, timeout=5)
        cmd = {
            "id": 1,
            "method": "Runtime.evaluate",
            "params": {
                "expression": js_code,
                "returnByValue": True
            }
        }
        ws.send(json.dumps(cmd))
        result = json.loads(ws.recv())
        ws.close()
        return result
    except Exception as e:
        return {"error": f"WebSocket communication failure: {str(e)}"}


# ==========================================
# 2. EXTENSION-BASED CHROME AUTOMATION
# (Requires local bridge server running + extension loaded in Chrome)
# ==========================================

@mcp.tool()
def chrome_ext_status() -> dict:
    """Check the connection status between the Chrome Extension and the local Bridge Server.
    
    Returns:
    - connected: boolean indicating if the Chrome extension is polling and active.
    """
    return _bridge_request("/api/ext/status")

@mcp.tool()
def chrome_ext_list_tabs() -> dict:
    """Retrieve all open tabs inside Chrome via the loaded Chrome Extension.
    
    This works on normal Chrome profiles without needing --remote-debugging-port.
    """
    return _bridge_request("/api/ext/tabs")

@mcp.tool()
def chrome_ext_evaluate_js(tab_id: int, js_code: str = "document.title") -> dict:
    """Evaluate JavaScript inside a specific tab via the Chrome Extension.
    
    Parameters:
    - tab_id: The integer tab ID to target (get this from chrome_ext_list_tabs).
    - js_code: The JavaScript code block to run.
    """
    return _bridge_request("/api/ext/eval", method="POST", data={"tab_id": tab_id, "js_code": js_code})

@mcp.tool()
def chrome_ext_navigate(tab_id: int, url: str) -> dict:
    """Navigate a specific tab to a new URL.
    
    Parameters:
    - tab_id: The integer tab ID to navigate.
    - url: The target web URL (e.g. 'https://google.com').
    """
    return _bridge_request("/api/ext/navigate", method="POST", data={"tab_id": tab_id, "url": url})

@mcp.tool()
def chrome_ext_capture() -> dict:
    """Take a high-quality visual screenshot (screen capture) of the current active Chrome tab.
    
    Returns:
    - screenshot: Base64-encoded JPEG image URL.
    """
    return _bridge_request("/api/ext/capture", method="POST")

@mcp.tool()
def chrome_ext_create_tab(url: str = "about:blank") -> dict:
    """Open a new tab in Chrome with the specified URL.
    
    Parameters:
    - url: The web URL to open in the new tab.
    """
    return _bridge_request("/api/ext/create_tab", method="POST", data={"url": url})

@mcp.tool()
def chrome_ext_close_tab(tab_id: int) -> dict:
    """Close a specific tab in Chrome.
    
    Parameters:
    - tab_id: The integer tab ID to close.
    """
    return _bridge_request("/api/ext/close_tab", method="POST", data={"tab_id": tab_id})


if __name__ == "__main__":
    mcp.run()

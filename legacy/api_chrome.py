import json
import urllib.request
import urllib.error
from server import router, APIError

try:
    import websocket
    HAS_WS = True
except ImportError:
    HAS_WS = False

CHROME_DEBUG_URL = "http://127.0.0.1:9222"

@router.get(r"/api/chrome/tabs")
def get_chrome_tabs(req, **kwargs):
    """Retrieve all open Chrome tabs via CDP."""
    try:
        req_obj = urllib.request.Request(f"{CHROME_DEBUG_URL}/json")
        with urllib.request.urlopen(req_obj, timeout=2) as response:
            tabs = json.loads(response.read().decode())
            return {"tabs": tabs, "count": len(tabs)}
    except urllib.error.URLError:
        raise APIError("Cannot connect to Chrome. Is it running with --remote-debugging-port=9222 ?", 503)

@router.post(r"/api/chrome/evaluate")
def evaluate_chrome_js(req, **kwargs):
    """Evaluate JS in a specific tab. Requires websocket-client."""
    if not HAS_WS:
        raise APIError("websocket-client library is not installed. Please pip install websocket-client", 501)
        
    tab_id = req.get("tab_id")
    js_code = req.get("js_code", "document.body.innerText")
    
    if not tab_id:
        raise APIError("tab_id is required")
        
    # Get WebSocket debugger URL for the tab
    try:
        req_obj = urllib.request.Request(f"{CHROME_DEBUG_URL}/json")
        with urllib.request.urlopen(req_obj, timeout=2) as response:
            tabs = json.loads(response.read().decode())
    except urllib.error.URLError:
        raise APIError("Cannot connect to Chrome debug port.", 503)
        
    ws_url = None
    for tab in tabs:
        if tab.get("id") == tab_id:
            ws_url = tab.get("webSocketDebuggerUrl")
            break
            
    if not ws_url:
        raise APIError(f"Tab {tab_id} not found or has no debugger URL.")
        
    try:
        ws = websocket.create_connection(ws_url, timeout=5)
        # Send Runtime.evaluate command
        cmd = {
            "id": 1,
            "method": "Runtime.evaluate",
            "params": {
                "expression": js_code,
                "returnByValue": True
            }
        }
        ws.send(json.dumps(cmd))
        result_str = ws.recv()
        ws.close()
        
        result = json.loads(result_str)
        if "error" in result:
             return {"success": False, "error": result["error"]}
        
        eval_result = result.get("result", {}).get("result", {})
        if eval_result.get("type") == "string":
             return {"success": True, "type": "string", "value": eval_result.get("value")}
        else:
             return {"success": True, "raw": eval_result}
             
    except Exception as e:
        raise APIError(str(e))

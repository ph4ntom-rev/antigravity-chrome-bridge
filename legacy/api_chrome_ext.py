import threading
import uuid
import time
from server import router, APIError


class ExtensionBridge:
    """Thread-safe command queue bridging REST API calls to the Chrome extension."""

    def __init__(self):
        self._lock = threading.Lock()
        self._queue = []
        self._results = {}
        self._events = {}
        self._last_poll = 0

    def submit(self, cmd_type, timeout=10, **params):
        """Submit a command and block until the extension returns a result."""
        cmd_id = str(uuid.uuid4())
        event = threading.Event()
        cmd = {"id": cmd_id, "type": cmd_type, **params}

        with self._lock:
            self._queue.append(cmd)
            self._events[cmd_id] = event

        if event.wait(timeout=timeout):
            with self._lock:
                result = self._results.pop(cmd_id, None)
                self._events.pop(cmd_id, None)
            if isinstance(result, dict) and result.get("__error__"):
                raise APIError(result["__error__"])
            return result
        else:
            with self._lock:
                self._queue = [c for c in self._queue if c["id"] != cmd_id]
                self._events.pop(cmd_id, None)
            raise APIError("Chrome extension timeout - is it installed and active?", 504)

    def poll(self):
        """Called by the extension to pick up a pending command."""
        with self._lock:
            self._last_poll = time.time()
            if self._queue:
                return self._queue.pop(0)
        return None

    def resolve(self, cmd_id, result):
        """Called by the extension to deliver a command result."""
        with self._lock:
            self._results[cmd_id] = result
            event = self._events.get(cmd_id)
            if event:
                event.set()

    @property
    def is_connected(self):
        with self._lock:
            return (time.time() - self._last_poll) < 3 if self._last_poll else False


ext = ExtensionBridge()


# --- Extension polling endpoints (called BY the extension) ---

@router.get(r"/api/ext/poll")
def ext_poll(req, **kwargs):
    cmd = ext.poll()
    return {"command": cmd}


@router.post(r"/api/ext/result")
def ext_result(req, **kwargs):
    cmd_id = req.get("id")
    result = req.get("result")
    if not cmd_id:
        raise APIError("id required")
    ext.resolve(cmd_id, result)
    return {"success": True}


@router.get(r"/api/ext/status")
def ext_status(req, **kwargs):
    return {"connected": ext.is_connected}


# --- High-level Chrome endpoints (called BY the user/AI) ---

@router.get(r"/api/ext/tabs")
def ext_list_tabs(req, **kwargs):
    return ext.submit("list_tabs")


@router.post(r"/api/ext/eval")
def ext_eval_js(req, **kwargs):
    tab_id = req.get("tab_id")
    js_code = req.get("js_code", "document.title")
    if not tab_id:
        raise APIError("tab_id required")
    return ext.submit("eval_js", tab_id=int(tab_id), js_code=js_code)


@router.post(r"/api/ext/navigate")
def ext_navigate(req, **kwargs):
    tab_id = req.get("tab_id")
    url = req.get("url")
    if not tab_id or not url:
        raise APIError("tab_id and url required")
    return ext.submit("navigate", tab_id=int(tab_id), url=url)


@router.post(r"/api/ext/capture")
def ext_capture(req, **kwargs):
    return ext.submit("capture_tab", timeout=15)


@router.post(r"/api/ext/create_tab")
def ext_create_tab(req, **kwargs):
    url = req.get("url", "about:blank")
    return ext.submit("create_tab", url=url)


@router.post(r"/api/ext/close_tab")
def ext_close_tab(req, **kwargs):
    tab_id = req.get("tab_id")
    if not tab_id:
        raise APIError("tab_id required")
    return ext.submit("close_tab", tab_id=int(tab_id))

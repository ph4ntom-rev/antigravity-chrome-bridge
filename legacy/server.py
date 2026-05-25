import json
import threading
import traceback
import re
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HOST = "127.0.0.1"
PORT = 13371
MAX_BODY_SIZE = 10 * 1024 * 1024  # 10 MB

class APIError(Exception):
    def __init__(self, message, status=400):
        self.message = message
        self.status = status

class APIRouter:
    def __init__(self):
        self.routes = {'GET': [], 'POST': []}

    def get(self, path):
        def decorator(func):
            self.routes['GET'].append((re.compile(f"^{path}$"), func))
            return func
        return decorator

    def post(self, path):
        def decorator(func):
            self.routes['POST'].append((re.compile(f"^{path}$"), func))
            return func
        return decorator

    def dispatch(self, method: str, path: str, req_data: dict):
        for pattern, handler in self.routes.get(method.upper(), []):
            match = pattern.match(path)
            if match:
                return handler(req_data, **match.groupdict())
        available = [p.pattern for p, h in self.routes.get(method.upper(), [])]
        raise APIError(f"Endpoint not found: {path}. Available {method} routes: {available}", 404)

router = APIRouter()

class BridgeHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def _send_cors_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

    def _send_response(self, data, status=200):
        try:
            payload = json.dumps(data, ensure_ascii=False).encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(payload)))
            self._send_cors_headers()
            self.end_headers()
            self.wfile.write(payload)
        except OSError:
            pass

    def handle_request(self, method):
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")
        req_data = parse_qs(parsed.query) if method == "GET" else {}
        
        if method == "POST":
            content_length = int(self.headers.get("Content-Length", 0))
            if content_length > MAX_BODY_SIZE:
                return self._send_response({"error": "Payload Too Large"}, 413)
            if content_length > 0:
                try:
                    req_data = json.loads(self.rfile.read(content_length).decode("utf-8"))
                except json.JSONDecodeError:
                     return self._send_response({"error": "Invalid JSON"}, 400)

        # Basic query parameter processing for GET requests (converting lists from parse_qs to single strings)
        if method == "GET" and isinstance(req_data, dict):
            processed_data = {}
            for k, v in req_data.items():
                if isinstance(v, list) and len(v) == 1:
                    processed_data[k] = v[0]
                else:
                    processed_data[k] = v
            req_data = processed_data

        try:
            result = router.dispatch(method, path, req_data)
            self._send_response(result, 200)
        except APIError as api_err:
            self._send_response({"error": api_err.message}, api_err.status)
        except Exception as e:
            self._send_response({"error": "Internal Server Error", "details": str(e), "trace": traceback.format_exc()}, 500)

    def do_GET(self): self.handle_request("GET")
    def do_POST(self): self.handle_request("POST")
    def do_OPTIONS(self):
        self.send_response(204)
        self._send_cors_headers()
        self.end_headers()

def start_server():
    server = ThreadingHTTPServer((HOST, PORT), BridgeHandler)
    print(f"[Chrome Bridge] Server ONLINE on http://{HOST}:{PORT}")
    server.serve_forever()

if __name__ == "__main__":
    import api_chrome
    import api_chrome_ext
    start_server()

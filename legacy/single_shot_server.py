from http.server import BaseHTTPRequestHandler, HTTPServer
import json

state = "NAVIGATE"
tab_id = 900646936
lens_results = []
server_running = True

class RequestHandler(BaseHTTPRequestHandler):
    def _send_cors(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

    def do_OPTIONS(self):
        self.send_response(204)
        self._send_cors()
        self.end_headers()

    def do_GET(self):
        global state, tab_id
        if self.path == '/api/ext/poll':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self._send_cors()
            self.end_headers()
            
            cmd = None
            if state == "NAVIGATE":
                cmd = {"id": "c1", "type": "navigate", "tab_id": tab_id, "url": "https://lens.google.com/uploadbyurl?url=https://picsum.photos/seed/picsum/800/600"}
                state = "WAIT_NAV"
            elif state == "WAIT_RESULTS":
                cmd = {"id": "c2", "type": "wait_for", "tab_id": tab_id, "selector": ".Vd9M6, a h3", "timeout_ms": 10000}
                state = "WAIT_RESULTS_ACK"
            elif state == "EXTRACT":
                cmd = {"id": "c3", "type": "query_selector", "tab_id": tab_id, "selector": ".Vd9M6, a h3"}
                state = "WAIT_EXTRACT"
                
            self.wfile.write(json.dumps({"command": cmd}).encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        global state, lens_results, server_running
        if self.path == '/api/ext/result':
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            data = json.loads(body.decode('utf-8'))
            cid = data.get('id')
            res = data.get('result')
            err = data.get('error')
            
            if state == "WAIT_NAV" and cid == "c1":
                print("Navigated to Lens URL search.")
                state = "WAIT_RESULTS"
            elif state == "WAIT_RESULTS_ACK" and cid == "c2":
                if res and res.get('found'):
                    print("Results found on page!")
                    state = "EXTRACT"
                else:
                    print("Still waiting for results...")
                    state = "WAIT_RESULTS"
            elif state == "WAIT_EXTRACT" and cid == "c3":
                if res:
                    for r in res:
                        t = r.get('textContent', '').strip()
                        if t and t not in lens_results:
                            lens_results.append(t)
                print("=== GOOGLE LENS RESULTS ===")
                for i, text in enumerate(lens_results):
                    print(f"{i+1}. {text}")
                print("===========================")
                state = "DONE"
                server_running = False
            
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self._send_cors()
            self.end_headers()
            self.wfile.write(b'{"success":true}')
        else:
            self.send_response(404)
            self.end_headers()

def run():
    server_address = ('127.0.0.1', 13371)
    httpd = HTTPServer(server_address, RequestHandler)
    print("Listening on 13371...")
    while server_running:
        httpd.handle_request()

if __name__ == '__main__':
    run()

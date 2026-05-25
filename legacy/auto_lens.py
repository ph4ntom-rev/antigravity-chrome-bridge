from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import time

# Steps:
# 1. create_tab: https://lens.google.com
# 2. check tabs to find new tab ID
# 3. wait_for: input[type='file']
# 4. eval_js: fetch image and trigger upload
# 5. wait_for: .Vd9M6 or Lens results
# 6. query_selector: extract results

upload_js = """
(async function() {
    try {
        const res = await fetch('https://picsum.photos/seed/picsum/800/600');
        const blob = await res.blob();
        const file = new File([blob], 'photo.jpg', {type: 'image/jpeg'});
        const dt = new DataTransfer();
        dt.items.add(file);
        
        // Find the file input on Google Lens (usually inside .P8ozr or something)
        // Actually, just find any input[type='file']
        const inputs = document.querySelectorAll('input[type="file"]');
        if (inputs.length === 0) return 'No file input found';
        
        inputs[0].files = dt.files;
        inputs[0].dispatchEvent(new Event('change', { bubbles: true }));
        
        return 'Upload triggered';
    } catch(e) {
        return 'Error: ' + e.message;
    }
})()
"""

state = "CREATE_TAB"
tab_id = None
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
            if state == "CREATE_TAB":
                cmd = {"id": "c1", "type": "create_tab", "url": "https://lens.google.com"}
                state = "WAIT_CREATE"
            elif state == "NAVIGATED":
                # Wait for the input field to appear
                cmd = {"id": "c2", "type": "wait_for", "tab_id": tab_id, "selector": "input[type='file']", "timeout_ms": 10000}
                state = "WAIT_INPUT"
            elif state == "UPLOAD":
                cmd = {"id": "c3", "type": "eval_js", "tab_id": tab_id, "js_code": upload_js}
                state = "WAIT_UPLOAD"
            elif state == "WAIT_RESULTS":
                cmd = {"id": "c4", "type": "wait_for", "tab_id": tab_id, "selector": ".Vd9M6, a h3", "timeout_ms": 15000}
                state = "WAIT_RESULTS_ACK"
            elif state == "EXTRACT":
                cmd = {"id": "c5", "type": "query_selector", "tab_id": tab_id, "selector": ".Vd9M6, a h3"}
                state = "WAIT_EXTRACT"
            elif state == "DONE":
                pass
                
            self.wfile.write(json.dumps({"command": cmd}).encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        global state, tab_id, lens_results, server_running
        if self.path == '/api/ext/result':
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            data = json.loads(body.decode('utf-8'))
            cid = data.get('id')
            res = data.get('result')
            err = data.get('error')
            
            if err:
                print(f"[{cid}] Error: {err}")
            
            if state == "WAIT_CREATE" and cid == "c1":
                tab_id = res.get('id')
                state = "NAVIGATED"
                print(f"Tab created: {tab_id}")
            elif state == "WAIT_INPUT" and cid == "c2":
                if res and res.get('found'):
                    state = "UPLOAD"
                    print("Input found. Uploading...")
                else:
                    print("Input not found! Retrying wait...")
                    state = "NAVIGATED"
            elif state == "WAIT_UPLOAD" and cid == "c3":
                print(f"Upload result: {res}")
                state = "WAIT_RESULTS"
            elif state == "WAIT_RESULTS_ACK" and cid == "c4":
                if res and res.get('found'):
                    state = "EXTRACT"
                    print("Results loaded!")
                else:
                    print("Results not found! Retrying wait...")
                    state = "WAIT_RESULTS"
            elif state == "WAIT_EXTRACT" and cid == "c5":
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
    print("Listening on 13371. Processing Google Lens flow...")
    while server_running:
        httpd.handle_request()
    print("Flow complete.")

if __name__ == '__main__':
    run()

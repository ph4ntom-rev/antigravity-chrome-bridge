import urllib.request
import json
import time

BRIDGE = "http://127.0.0.1:13371"

def call(endpoint, method="POST", data=None):
    req_data = json.dumps(data).encode() if data else None
    req = urllib.request.Request(BRIDGE + endpoint, data=req_data, method=method)
    if data:
        req.add_header('Content-Type', 'application/json')
    try:
        with urllib.request.urlopen(req, timeout=15) as res:
            return json.loads(res.read())
    except Exception as e:
        print(f"Error calling {endpoint}: {e}")
        return None

print("Checking extension status...")
print(call("/api/ext/status", "GET"))

print("Creating Lens tab...")
res = call("/api/ext/create_tab", data={"url": "https://lens.google.com/uploadbyurl?url=https://picsum.photos/seed/picsum/800/600"})
print(res)

tab_id = res.get("id") if res else None
if not tab_id:
    # try getting active tab
    tabs = call("/api/ext/tabs", "GET")
    for t in tabs.get('tabs', []):
        if 'lens.google' in t.get('url', ''):
            tab_id = t['id']
            break

if tab_id:
    print(f"Using tab {tab_id}. Waiting 5 seconds for page load...")
    time.sleep(5)
    
    # Try getting title first
    print("Tab title:", call("/api/ext/eval", data={"tab_id": tab_id, "js_code": "document.title"}))
    
    # query selector for lens results
    print("Querying results...")
    js = """
    (function() {
        var results = [];
        document.querySelectorAll('.Vd9M6, a h3, a[aria-label]').forEach(e => {
            var t = e.innerText || e.getAttribute('aria-label');
            if (t && t.length > 5 && t.indexOf('Google')===-1) results.push(t.trim());
        });
        return results;
    })();
    """
    res = call("/api/ext/eval", data={"tab_id": tab_id, "js_code": js})
    print("RESULTS:")
    print(res)
else:
    print("No tab ID found.")

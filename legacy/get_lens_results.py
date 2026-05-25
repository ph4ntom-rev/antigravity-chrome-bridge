import requests
import json
import time

def get_tabs():
    resp = requests.get('http://127.0.0.1:13371/api/ext/tabs')
    return resp.json()

def eval_js(tab_id, code):
    resp = requests.post('http://127.0.0.1:13371/api/ext/eval', json={'tab_id': tab_id, 'js_code': code})
    return resp.json()

def main():
    try:
        tabs = get_tabs()
        print("Tabs response:", type(tabs), tabs)
        lens_tab = None
        if isinstance(tabs, dict) and 'error' in tabs:
            print("API Error:", tabs['error'])
            return
        for t in tabs:
            if isinstance(t, dict):
                if 'lens.google.com' in t.get('url', '') or 'google.com/search' in t.get('url', '') and 'vsrid=' in t.get('url', ''):
                    lens_tab = t
                    break
            else:
                print("Unexpected tab format:", t)
        
        if not lens_tab:
            print("Lens tab not found. Tabs:")
            for t in tabs:
                print(f" - {t.get('title')} ({t.get('url')})")
            return

        print(f"Found Lens tab: {lens_tab['title']} ({lens_tab['id']})")
        
        # Extract titles from Google Lens results.
        js_code = """
        (function() {
            var items = document.querySelectorAll('.Vd9M6'); // Class name for visual matches or general links
            if (items.length === 0) {
                // Try alternate selector
                items = document.querySelectorAll('a h3, div.ksb, a[aria-label]');
            }
            var results = [];
            for (var i = 0; i < items.length && i < 10; i++) {
                results.push(items[i].innerText || items[i].getAttribute('aria-label') || items[i].textContent);
            }
            return results;
        })();
        """
        res = eval_js(lens_tab['id'], js_code)
        print("Results:")
        print(json.dumps(res, indent=2, ensure_ascii=False))

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()

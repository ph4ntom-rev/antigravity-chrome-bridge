import urllib.request
import re

url = "https://lens.google.com/uploadbyurl?url=https://picsum.photos/seed/picsum/800/600"
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36'})
try:
    response = urllib.request.urlopen(req)
    html = response.read().decode('utf-8')
    print("Page loaded! Length:", len(html))
    
    # Try to find common Google Lens result classes or titles
    # Titles are often inside "text" keys in inline script data or within standard HTML tags
    import sys
    sys.stdout.reconfigure(encoding='utf-8')
    with open('lens_result.html', 'w', encoding='utf-8') as f:
        f.write(html)
    print("Saved to lens_result.html")
    
    h3_matches = re.findall(r'<h3[^>]*>(.*?)</h3>', html)
    aria_matches = re.findall(r'aria-label="([^"]+)"', html)
    
    print("=== RESULTS ===")
    for h in h3_matches[:10]:
        print("-", h)
    for a in aria_matches[:15]:
        if "Google" not in a and "Lens" not in a and "Search" not in a and len(a) > 5:
            print("?", a)
    print("===============")
except Exception as e:
    print("Error:", e)

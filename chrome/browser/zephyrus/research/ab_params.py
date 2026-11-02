"""Live check of tracking-parameter removal.

A local server records exactly which URLs reach it. Zephyrus opens a page whose
address carries tracking parameters; the page then fetches and loads more URLs that
carry them. With the feature on, the server must only ever see the cleaned URLs;
with it off (kill switch) it sees the originals. Also checked: the address bar's
final URL, the fragment, the parameters that must stay, and that history has one
entry (a redirect, not a second navigation).
"""
import http.server, json, sys, threading, time, urllib.request

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile

PORT, CDP = 8792, 9334
seen = []

PAGE = """<!doctype html><title>landing</title><body>
<img src="/pix.png?utm_campaign=c&a=1">
<script>
fetch('/api?x=1&gclid=ABC&utm_source=mail').then(r => r.text()).then(() => {
  fetch('/report?len=' + history.length + '&href=' + encodeURIComponent(location.href));
});
fetch('/keep?id=7&state=abc&code=9&ref=home&source=nav');
</script></body>"""


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        seen.append(self.path)
        body = PAGE.encode() if self.path.startswith("/landing") else b"ok"
        self.send_response(200)
        self.send_header("Content-Type", "text/html" if self.path.startswith("/landing") else "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()

DIRTY = f"http://127.0.0.1:{PORT}/landing?id=7&utm_source=mail&utm_medium=email&fbclid=ZZ9&keep=1#frag"


def run(label, extra):
    seen.clear()
    prof = r"D:\zeph_perf\params_prof"
    seed_profile(prof, workspaces=False, bookmarks=False)
    b = Browser(prof, args=[f"--remote-debugging-port={CDP}"] + extra, urls=(DIRTY,), wait=25)
    time.sleep(8)
    tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{CDP}/json/list"))
    final = [t["url"] for t in tabs if t.get("type") == "page" and "127.0.0.1" in t["url"]]
    b.close()
    print(f"== {label}")
    print("   address bar ends on :", final)
    for p in seen:
        print("   server saw          :", p)
    return final, list(seen)


on = run("feature ON (default)", [])
off = run("feature OFF (--disable-features=ZephyrusAdblockStripTrackingParams)",
          ["--disable-features=ZephyrusAdblockStripTrackingParams"])

ok = True
def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok = ok and cond

final_on, seen_on = on
final_off, seen_off = off
check("ON: the page was requested without tracking parameters",
      any(p == "/landing?id=7&keep=1" for p in seen_on) and not any("utm_" in p or "fbclid" in p for p in seen_on if p.startswith("/landing")))
check("ON: the address bar shows the cleaned URL and keeps the fragment",
      any(u == f"http://127.0.0.1:{PORT}/landing?id=7&keep=1#frag" for u in final_on))
check("ON: a fetch lost gclid and utm_source but kept x=1", "/api?x=1" in seen_on)
check("ON: an image lost utm_campaign but kept a=1", "/pix.png?a=1" in seen_on)
check("ON: state-carrying parameters were left alone",
      "/keep?id=7&state=abc&code=9&ref=home&source=nav" in seen_on)
check("ON: no tracking parameter reached the server at all",
      not any(k in p for p in seen_on for k in ("utm_", "fbclid", "gclid")))
check("ON: one history entry (a redirect, not an extra navigation)",
      any(p.startswith("/report?len=1&") for p in seen_on))
check("OFF: the kill switch sends the original URL",
      any(p == "/landing?id=7&utm_source=mail&utm_medium=email&fbclid=ZZ9&keep=1" for p in seen_off))
print("\nALL PASSED" if ok else "\nSOME FAILED")

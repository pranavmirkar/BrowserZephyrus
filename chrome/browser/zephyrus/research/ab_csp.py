"""Live check of $csp= rules.

A rule adds a Content-Security-Policy header to a page or frame load. The pages here use `img-src 'none'`
as the policy because a page can still report what happened to its image (script is not restricted):
  * csp.zephtest.com  -- a page load a rule names: its image must be BLOCKED by the policy, and the page
                         sees a `securitypolicyviolation` event for img-src;
  * top.zephtest.com  -- no rule; it embeds frm.zephtest.com, a frame a rule names: the FRAME's image is
                         blocked, the top page's own image is not;
  * plain.zephtest.com-- no rule: image loads (a rule must not leak onto other sites).
Runs: default; kill switch (--disable-features=ZephyrusAdblockCsp); exception `@@||frm.zephtest.com^$csp`.
ZEPH_EXE selects the browser.
"""
import http.server, json, os, shutil, sys, threading, time, urllib.parse

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile

PORT = 8915
HOSTS = ["csp", "top", "frm", "plain"]
PIX = bytes.fromhex("47494638396101000100800000000000ffffff21f90401000000002c00000000010001000002024401003b")

PAGE_IMG = """<!doctype html><title>%(who)s</title><body>
<img id=i src="/lib/photo.gif?%(who)s" onload="r('loaded')" onerror="r('blocked')">
<script>
var viol=[]; document.addEventListener('securitypolicyviolation',function(e){viol.push(e.effectiveDirective)});
var state=null; function r(s){state=s}
setTimeout(function(){fetch('/report?who=%(who)s',{method:'POST',body:JSON.stringify({img:state,viol:viol})})},2500);
</script>%(extra)s</body>"""

FRAME_PAGE = """<!doctype html><title>frame</title><body>
<img src="/lib/photo.gif?frm" onload="parent.postMessage('loaded','*')" onerror="parent.postMessage('blocked','*')">
</body>"""

TOP_EXTRA = """<iframe src="http://frm.zephtest.com:%d/"></iframe>
<script>var fr=null;addEventListener('message',function(e){fr=e.data});
setTimeout(function(){fetch('/report?who=top_frame',{method:'POST',body:JSON.stringify({img:fr})})},2800);</script>""" % PORT


class H(http.server.BaseHTTPRequestHandler):
    reports = {}

    def do_GET(self):
        host = self.headers.get("Host", "").split(".")[0]
        path = urllib.parse.urlparse(self.path).path
        if path == "/lib/photo.gif":
            body, ctype = PIX, "image/gif"
        elif host == "frm":
            body, ctype = FRAME_PAGE.encode(), "text/html"
        else:
            extra = TOP_EXTRA if host == "top" else ""
            body, ctype = (PAGE_IMG % {"who": host, "extra": extra}).encode(), "text/html"
        self.send_response(200); self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        who = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query).get("who", ["?"])[0]
        H.reports[who] = json.loads(self.rfile.read(n))
        self.send_response(200); self.send_header("Content-Length", "0"); self.end_headers()

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()


def run_pages(label, extra_args, extra_rules=()):
    H.reports = {}
    prof = r"D:\zeph_perf\csp_prof"
    seed_profile(prof, workspaces=False, bookmarks=False)
    dst = os.path.join(prof, "ZephyrusAdBlock")
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(r"D:\zeph_perf\abc_prof_on\ZephyrusAdBlock", dst)
    z = os.path.join(dst, "filters.zidx")
    if os.path.exists(z):
        os.remove(z)
    with open(os.path.join(dst, "filters.txt"), "a", encoding="utf-8", newline="\n") as f:
        f.write("\n! ===== https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/test.txt =====\n")
        f.write("||csp.zephtest.com^$csp=img-src 'none'\n")
        f.write("||frm.zephtest.com^$csp=img-src 'none'\n")
        for r in extra_rules:
            f.write(r + "\n")
    rules = ",".join("MAP %s.zephtest.com 127.0.0.1" % h for h in HOSTS)
    urls = tuple("http://%s.zephtest.com:%d/" % (h, PORT) for h in ("csp", "top", "plain"))
    b = Browser(prof, args=["--host-resolver-rules=" + rules, "--enable-logging",
                            "--log-file=" + r"D:\zeph_perf\csp.log", "--v=1"] + extra_args,
                urls=urls, wait=30)
    deadline = time.time() + 40
    while len(H.reports) < 4 and time.time() < deadline:
        time.sleep(1)
    b.close()
    print("==", label, sorted(H.reports))
    return dict(H.reports)


def check(label, rep, want):
    got = {
        "csp": (rep.get("csp") or {}).get("img"),
        "csp_viol": "img-src" in ((rep.get("csp") or {}).get("viol") or []),
        "top": (rep.get("top") or {}).get("img"),
        "top_frame": (rep.get("top_frame") or {}).get("img"),
        "plain": (rep.get("plain") or {}).get("img"),
    }
    ok = all(got[k] == v for k, v in want.items())
    print("%-30s %s  %s" % (label, "PASS" if ok else "FAIL", "" if ok else "got %s want %s" % (got, want)))
    return ok


import urllib.request
on = run_pages("default", [])
off = run_pages("kill switch", ["--disable-features=ZephyrusAdblockCsp"])
exc = run_pages("exception", [], ["@@||frm.zephtest.com^$csp"])
res = [
    check("default: rules apply", on, {"csp": "blocked", "csp_viol": True, "top": "loaded",
                                       "top_frame": "blocked", "plain": "loaded"}),
    check("kill switch: nothing added", off, {"csp": "loaded", "csp_viol": False, "top_frame": "loaded"}),
    check("exception: frame exempt", exc, {"csp": "blocked", "top_frame": "loaded", "plain": "loaded"}),
]
print("PASS" if all(res) else "FAIL")

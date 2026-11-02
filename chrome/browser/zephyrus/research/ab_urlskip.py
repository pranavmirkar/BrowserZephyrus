"""Live check of $urlskip.

A local server records every request it gets, with the headers that say HOW the navigation
was made. Zephyrus navigates (browser-initiated, then by clicking a link on a page) to
real tracker URLs from the shipped list -- safelinks.protection.outlook.com and storz.ma,
hosts that are never contacted: the redirect happens inside the browser -- that point at
the local server. For each, the request the server gets must be the one a DIRECT navigation
to the destination makes (same Sec-Fetch-* and Referer, and the same cookie), the tracker
host must never be asked, history must grow by one entry, and
--disable-features=ZephyrusAdblockUrlSkip must leave the navigation alone.
"""
import base64, http.server, json, os, shutil, sys, threading, time, urllib.parse, urllib.request

import websocket

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile

PORT, CDP = 8794, 9336
HOME = f"http://127.0.0.1:{PORT}"
seen = []

LANDING = "<!doctype html><title>landed</title><body>landed</body>"
HOME_PAGE = """<!doctype html><title>home</title><body>
<a id="t" href="https://safelinks.protection.outlook.com/?url=%(enc)s%%2Flanded%%3Fn%%3Dclick">tracker link</a>
<a id="d" href="/landed?n=click">direct link</a>
<a id="t2" href="https://safelinks.protection.outlook.com/?url=%(xenc)s">cross-site tracker link</a>
<a id="d2" href="http://localhost:8794/landed?n=xsite">cross-site direct link</a></body>""" % {
    "enc": urllib.parse.quote(HOME, safe=""),
    "xenc": urllib.parse.quote("http://localhost:8794/landed?n=xsite", safe="")}


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        seen.append({"path": self.path,
                     "site": self.headers.get("Sec-Fetch-Site"), "mode": self.headers.get("Sec-Fetch-Mode"),
                     "dest": self.headers.get("Sec-Fetch-Dest"), "user": self.headers.get("Sec-Fetch-User"),
                     "referer": self.headers.get("Referer"), "cookie": self.headers.get("Cookie")})
        body = (HOME_PAGE if self.path.startswith("/home") else LANDING).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        if self.path.startswith("/home"):
            self.send_header("Set-Cookie", "c=1; SameSite=Lax; Path=/")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()


class Tab:
    def __init__(self, ws_url):
        self.ws = websocket.create_connection(ws_url, timeout=20, suppress_origin=True)
        self.n = 0

    def call(self, method, **params):
        self.n += 1
        self.ws.send(json.dumps({"id": self.n, "method": method, "params": params}))
        while True:
            m = json.loads(self.ws.recv())
            if m.get("id") == self.n:
                return m

    def eval(self, expr):
        return self.call("Runtime.evaluate", expression=expr, returnByValue=True).get("result", {}).get("result", {}).get("value")

    def go(self, url, settle=3):
        self.call("Page.navigate", url=url)
        time.sleep(settle)


def b64(s):
    return base64.b64encode(s.encode()).decode()


def run(label, extra):
    seen.clear()
    prof = r"D:\zeph_perf\urlskip_prof"
    seed_profile(prof, workspaces=False, bookmarks=False)
    shutil.rmtree(os.path.join(prof, "ZephyrusAdBlock"), ignore_errors=True)
    shutil.copytree(os.path.join("D:" + os.sep, "zeph_perf", "abc_prof_on", "ZephyrusAdBlock"),
                    os.path.join(prof, "ZephyrusAdBlock"))
    b = Browser(prof, args=[f"--remote-debugging-port={CDP}"] + extra, urls=("about:blank",), wait=25)
    time.sleep(12)
    tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{CDP}/json/list"))
    tab = Tab([t for t in tabs if t.get("type") == "page"][0]["webSocketDebuggerUrl"])
    tab.call("Page.enable")
    out = {}

    tab.go(f"{HOME}/home")                                   # sets the cookie, offers two links
    mark = len(seen)
    # 1. browser-initiated navigation to a tracker URL that points at the server
    tab.go("https://safelinks.protection.outlook.com/?url=" + urllib.parse.quote(f"{HOME}/landed?n=1", safe=""))
    out["typed_skip"] = [r for r in seen[mark:] if r["path"].startswith("/landed")]
    out["typed_final"] = tab.eval("location.href")
    mark = len(seen)
    tab.go(f"{HOME}/landed?n=1")                             # the same destination, directly
    out["typed_direct"] = [r for r in seen[mark:] if r["path"].startswith("/landed")]
    # 2. a base64 tracker (storz.ma): the destination is base64 in a query parameter
    mark = len(seen)
    tab.go("https://storz.ma/redigoto?to=" + b64(f"{HOME}/landed?n=b64"))
    out["b64"] = [r for r in seen[mark:] if r["path"].startswith("/landed")]
    # 3. links on a page: tracker link vs direct link
    tab.go(f"{HOME}/home")
    hist0 = tab.eval("history.length")
    mark = len(seen)
    tab.eval("document.getElementById('t').click()")
    time.sleep(3)
    out["click_skip"] = [r for r in seen[mark:] if r["path"].startswith("/landed")]
    out["click_final"] = tab.eval("location.href")
    out["click_history_growth"] = (tab.eval("history.length") or 0) - (hist0 or 0)
    tab.go(f"{HOME}/home")
    mark = len(seen)
    tab.eval("document.getElementById('d').click()")
    time.sleep(3)
    out["click_direct"] = [r for r in seen[mark:] if r["path"].startswith("/landed")]
    # 3b. the same, to a DIFFERENT site (the realistic case: the destination is not the page's own site)
    tab.go(f"{HOME}/home")
    mark = len(seen)
    tab.eval("document.getElementById('t2').click()")
    time.sleep(3)
    out["xsite_skip"] = [r for r in seen[mark:] if r["path"].startswith("/landed")]
    out["xsite_final"] = tab.eval("location.href")
    tab.go(f"{HOME}/home")
    mark = len(seen)
    tab.eval("document.getElementById('d2').click()")
    time.sleep(3)
    out["xsite_direct"] = [r for r in seen[mark:] if r["path"].startswith("/landed")]
    # 4. a tracker URL with nothing to extract, and the tracker host is never contacted
    mark = len(seen)
    tab.go("https://safelinks.protection.outlook.com/?nothing=1", settle=4)
    out["nothing_hits"] = len(seen) - mark
    out["nothing_final"] = tab.eval("location.href")
    b.close()
    print(f"== {label}")
    for k, v in out.items():
        print(f"   {k:22}: {v}")
    return out


on = run("feature ON (default)", [])
off = run("feature OFF (--disable-features=ZephyrusAdblockUrlSkip)", ["--disable-features=ZephyrusAdblockUrlSkip"])

ok = True


def check(name, cond):
    global ok
    print(("PASS " if cond else "FAIL ") + name)
    ok = ok and bool(cond)


def same_way(a, b_):
    """Two requests made the same way: the headers that say how, the cookie and the referrer."""
    keys = ("site", "mode", "dest", "user", "referer", "cookie")
    return len(a) == 1 and len(b_) == 1 and all(a[0][k] == b_[0][k] for k in keys)


check("ON: a navigation to the tracker URL reached only the destination, as /landed?n=1",
      len(on["typed_skip"]) == 1 and on["typed_skip"][0]["path"] == "/landed?n=1")
check("ON: ... and the address bar shows the destination", on["typed_final"] == f"{HOME}/landed?n=1")
check("ON: ... made exactly as a direct navigation to the destination (Sec-Fetch-*, Referer, Cookie)",
      same_way(on["typed_skip"], on["typed_direct"]))
check("ON: the cookie set earlier is sent (a top-level navigation)",
      bool(on["typed_skip"]) and on["typed_skip"][0]["cookie"] == "c=1")
check("ON: a base64 tracker link (storz.ma) was skipped to its destination",
      len(on["b64"]) == 1 and on["b64"][0]["path"] == "/landed?n=b64")
check("ON: clicking a tracker link reached /landed?n=click", len(on["click_skip"]) == 1
      and on["click_skip"][0]["path"] == "/landed?n=click" and on["click_final"] == f"{HOME}/landed?n=click")
def no_leakier(skipped, direct):
    """Same-origin case: the referrer may be the full URL (direct) or just its origin (the
    redirect applied the page's referrer policy as it would have to the tracker): never more."""
    if len(skipped) != 1 or len(direct) != 1:
        return False
    a, b_ = skipped[0], direct[0]
    origin = (b_["referer"] or "").split("/", 3)[:3]
    return (all(a[k] == b_[k] for k in ("site", "mode", "dest", "user", "cookie"))
            and a["referer"] in (b_["referer"], "/".join(origin) + "/"))


check("ON: ... made as clicking the direct link would (same Sec-Fetch-*, Cookie; referrer no fuller)",
      no_leakier(on["click_skip"], on["click_direct"]))
check("ON: a tracker link to ANOTHER SITE reached its destination, with the address bar on it",
      len(on["xsite_skip"]) == 1 and on["xsite_skip"][0]["path"] == "/landed?n=xsite"
      and on["xsite_final"] == "http://localhost:8794/landed?n=xsite")
check("ON: ... cross-site, and made exactly as clicking the direct link (Sec-Fetch-*, Referer, Cookie)",
      same_way(on["xsite_skip"], on["xsite_direct"]) and on["xsite_skip"][0]["site"] == "cross-site")
check("ON: ... and history grew by one entry", on["click_history_growth"] == 1)
check("ON: a tracker URL with nothing to extract was left alone (the server heard nothing)",
      on["nothing_hits"] == 0 and "safelinks" in (on["nothing_final"] or ""))
check("OFF: the kill switch leaves the tracker navigation alone (nothing reaches the destination)",
      len(off["typed_skip"]) == 0 and len(off["b64"]) == 0 and len(off["click_skip"]) == 0
      and len(off["xsite_skip"]) == 0)
print("ALL PASS" if ok else "SOME FAILED")

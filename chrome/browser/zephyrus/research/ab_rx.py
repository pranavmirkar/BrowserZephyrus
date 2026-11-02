"""Live check of the /regex/ rules, request by request.

Same page and URLs as ab_rx.py, but DevTools records, for every request the page makes,
whether the browser blocked it (net::ERR_BLOCKED_BY_CLIENT). That set is compared with
the verdicts the unit-test binary gave the same (url, type) pairs: zeph_index_regex.txt.
"""
import http.server, json, os, shutil, sys, threading, time, urllib.request

import websocket

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile

PORT, CDP = 8793, 9335
import re


def canon(u):
    m = re.match(r"(https?://[^/?#]*)(.*)$", u, re.S)
    host, rest = m.group(1).lower(), m.group(2)
    return host + (rest if rest.startswith(("/", "?", "#")) and not rest.startswith(("?", "#")) else "/" + rest)

urls = [l.strip() for l in open(r"D:\zeph_perf\ab_rx_urls.txt", encoding="utf-8") if l.strip()]
rows = [l.rstrip("\n").split("\t") for l in open(r"D:\zeph_perf\h2h_rx\ab_rx_corpus.tsv", encoding="utf-8")]
verdict_with = [l.strip() for l in open(r"D:\zeph_perf\h2h_rx_ab2\zeph_index_regex.txt")]
verdict_without = [l.strip() for l in open(r"D:\zeph_perf\h2h_rx_ab2\zeph_index.txt")]
assert len(rows) == len(verdict_with) == len(verdict_without)
expect = {(canon(u), t): v == "1" for (u, p, t), v in zip(rows, verdict_with)}
expect_plain = {(canon(u), t): v == "1" for (u, p, t), v in zip(rows, verdict_without)}

PAGE = ("<!doctype html><title>probe</title><body><script>\n"
        "const urls = " + json.dumps(urls) + ";\n"
        "for (const u of urls) { const i = new Image(); i.src = u; "
        "const s = document.createElement('script'); s.src = u; s.async = true; document.body.appendChild(s); }\n"
        "document.title = 'sent ' + urls.length;\n</script></body>")


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.send_header("Content-Type", "text/html"); self.end_headers()
        self.wfile.write(PAGE.encode())

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()

prof = r"D:\zeph_perf\ab_rx_prof"
seed_profile(prof, workspaces=False, bookmarks=False)
shutil.rmtree(os.path.join(prof, "ZephyrusAdBlock"), ignore_errors=True)
shutil.copytree(os.path.join("D:" + os.sep, "zeph_perf", os.environ.get("ZAB", "abc_prof_on" + os.sep + "ZephyrusAdBlock")),
                os.path.join(prof, "ZephyrusAdBlock"))
b = Browser(prof, args=[f"--remote-debugging-port={CDP}", "--enable-logging", "--log-file=" + os.path.join("D:" + os.sep, "zeph_perf", "ab_rx_cdp.log"), "--v=1"], urls=("about:blank",), wait=25)
time.sleep(15)  # the full list is in force well before this
tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{CDP}/json/list"))
tab = [t for t in tabs if t.get("type") == "page"][0]
ws = websocket.create_connection(tab["webSocketDebuggerUrl"], timeout=5, suppress_origin=True)
nid = 0


def call(method, **params):
    global nid
    nid += 1
    ws.send(json.dumps({"id": nid, "method": method, "params": params}))


call("Network.enable")
call("Page.navigate", url=f"http://127.0.0.1:{PORT}/")
sent, failed = {}, {}
deadline = time.time() + 70
while time.time() < deadline:
    try:
        msg = json.loads(ws.recv())
    except websocket.WebSocketTimeoutException:
        continue
    m = msg.get("method")
    if m == "Network.requestWillBeSent":
        p = msg["params"]
        sent[p["requestId"]] = (p["request"]["url"], p.get("type", "?"))
    elif m == "Network.loadingFailed":
        p = msg["params"]
        failed[p["requestId"]] = p.get("errorText", "") + (" blocked=" + str(p.get("blockedReason")) if p.get("blockedReason") else "")
b.close()

blocked = {}
for rid, err in failed.items():
    if err.startswith("net::ERR_BLOCKED_BY_CLIENT") and rid in sent:
        u, t = sent[rid]
        blocked[(canon(u), "script" if t == "Script" else "image" if t == "Image" else t)] = True
print(f"requests seen {len(sent)} | failed {len(failed)} | blocked by client {len(blocked)}")
exp = {k for k, v in expect.items() if v}
exp_plain = {k for k, v in expect_plain.items() if v}
got = set(blocked)
print(f"expected with regex rules {len(exp)} | without {len(exp_plain)}")
print("blocked but not expected:", sorted(got - exp)[:10])
print("expected but not blocked:", sorted(exp - got)[:10])
print("regex-only blocks expected", len(exp - exp_plain), "| of those blocked live", len((exp - exp_plain) & got))
for rid, (u, t) in sent.items():
    if "host-cdn.net/mcdZjs" in u:
        print("EVENT", rid, t, u, "->", failed.get(rid, "(not failed)"))
extra = got - exp
ok = extra <= {("https://ads.host-cdn.net/mcdZjs?2", "image")} and not (exp - got)
print("PASS" if ok else "FAIL", "(the only allowed extra: the built-in starter rule ||ads.*^$third-party)")

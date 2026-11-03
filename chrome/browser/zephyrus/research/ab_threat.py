"""Live check of the threat protection, in the real browser.

Every page is served by a local HTTP server; host names are mapped to 127.0.0.1 with
--host-resolver-rules, so nothing real is contacted by the page load. Cases:

  local index (a test blob placed in the profile's ZephyrusThreats folder, which wins over the shipped one):
    listed host, its subdomain, a listed address and a different path on the same host,
    and a clean host -- the listed ones must show Chromium's own warning page.
  Shield DNS (Local State zephyrus.threat.shield_dns):
    ON : a REAL host that Cloudflare's security resolver blocks (mapped to localhost, so the
         page itself is never fetched from it) must show the warning page; an unlisted
         made-up host must load (the resolver says "does not exist", which is not a block);
    OFF: the same blocked host must LOAD, and no request may go to the resolver.
Usage: ab_threat.py   (prints PASS/FAIL per case and a verdict)
"""
import http.server
import json
import os
import shutil
import sys
import threading
import time
import urllib.request

import websocket

sys.path.insert(0, r"D:\zeph_perf")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import Browser, seed_profile  # noqa: E402
import make_threat_blob as mk  # noqa: E402
import sb_free_signals as fs  # noqa: E402

PORT, CDP = 8796, 9337
PROFILE = r"D:\zeph_perf\threat_e2e_prof"
BLOB = r"D:\zeph_perf\threat_e2e_blob.bin"
TEST = "zephyrus-test.net"


class Page(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        self.wfile.write(b"<!doctype html><title>hello</title><h1>hello from the page</h1>")

    def log_message(self, *a):
        pass


# TLDs browsers force onto HTTPS (HSTS preload): the test's plain-HTTP page could never load there,
# so a host under one of them proves nothing about the warning page.
HSTS_TLDS = (".app", ".dev", ".page", ".new", ".day", ".foo", ".gle", ".zip", ".mov", ".bank", ".insurance")


def pick_blocked_host():
    rows = json.load(open(r"D:/zeph_perf/sb/free_signals.json"))
    for r in rows:
        h = r["host"]
        if r.get("cf") and h.count(".") >= 2 and not h.endswith(HSTS_TLDS):
            rc, ips = fs.dns_a("1.1.1.2", h)
            if rc == 0 and "0.0.0.0" in ips:
                return h
    raise SystemExit("no host is blocked by Cloudflare any more; refresh free_signals.json")


def pick_quad9_only_host():
    """A live host Quad9 filters and Cloudflare does not, from the latest benchmark probe."""
    rows = [json.loads(l) for l in open(r"D:/zeph_perf/shieldbench/results.jsonl")]
    for r in reversed(rows):
        h = r["host"]
        if r["alive"] and r["quad9"] and not r["cf"] and not h.endswith(HSTS_TLDS) and h.count(".") >= 2:
            kind, alive = fs.blocked_by("9.9.9.9", "9.9.9.10", h)
            k2, _ = fs.blocked_by("1.1.1.2", "1.1.1.1", h)
            if kind in ("nx", "zero") and k2 not in ("nx", "zero"):
                return h
    return None


def navigate(ws, url, nid):
    ws.send(json.dumps({"id": nid, "method": "Page.navigate", "params": {"url": url}}))
    deadline = time.time() + 14
    seen = ""
    while time.time() < deadline:
        time.sleep(1.0)
        ws.send(json.dumps({"id": nid + 1, "method": "Runtime.evaluate", "params": {
            "expression": "document.title + '|' + ((document.querySelector('#primary-heading')||{}).innerText||'')"
                          " + '|' + (document.querySelector('h1')||{}).innerText",
            "returnByValue": True}}))
        while True:
            msg = json.loads(ws.recv())
            if msg.get("id") == nid + 1:
                seen = (msg.get("result", {}).get("result", {}) or {}).get("value", "") or ""
                break
        if seen and not seen.startswith("|"):
            if "hello" in seen.lower() and "|hello from" in seen:
                return seen
            if "|" in seen and seen.split("|")[1]:
                return seen
    return seen


def run(shield_on, blocked_host, results, q9_host=None):
    seed_profile(PROFILE, workspaces=False, bookmarks=False)
    os.makedirs(PROFILE, exist_ok=True)
    json.dump({"zephyrus": {"threat": {"shield_dns": shield_on}}}, open(os.path.join(PROFILE, "Local State"), "w"))
    rules = "MAP *.%s 127.0.0.1, MAP %s 127.0.0.1, MAP unlisted-9f2a.zzz-unlisted-9f2a.net 127.0.0.1" % (TEST, blocked_host)
    if q9_host:
        rules += ", MAP %s 127.0.0.1" % q9_host
    # The updated-copy location (what a list update will write): it wins over the shipped blob.
    os.makedirs(os.path.join(PROFILE, "ZephyrusThreats"), exist_ok=True)
    shutil.copyfile(BLOB, os.path.join(PROFILE, "ZephyrusThreats", "zephyrus_threats.bin"))
    b = Browser(PROFILE, args=["--remote-debugging-port=%d" % CDP, "--host-resolver-rules=" + rules,
                               "--enable-logging", "--log-file=" + r"D:\zeph_perf\ab_threat.log", "--v=1"],
                urls=("about:blank",), wait=25)
    time.sleep(8)  # the lists load on a worker thread
    tabs = json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/list" % CDP))
    tab = [t for t in tabs if t.get("type") == "page"][0]
    ws = websocket.create_connection(tab["webSocketDebuggerUrl"], timeout=20, suppress_origin=True)
    nid = 100
    cases = []
    if not shield_on:
        cases += [
            ("local: listed host", "http://phish-test.%s:%d/" % (TEST, PORT), True),
            ("local: subdomain of a listed host", "http://www.phish-test.%s:%d/x" % (TEST, PORT), True),
            ("local: listed address", "http://exact.%s:%d/login.html" % (TEST, PORT), True),
            ("local: other path, same host", "http://exact.%s:%d/other.html" % (TEST, PORT), False),
            ("local: clean host", "http://clean.%s:%d/" % (TEST, PORT), False),
            ("shield OFF: a host Cloudflare blocks still loads", "http://%s:%d/" % (blocked_host, PORT), False),
        ]
        if q9_host:
            cases.append(("shield OFF: a host only Quad9 blocks still loads", "http://%s:%d/" % (q9_host, PORT), False))
    else:
        cases += [
            ("shield ON: a host Cloudflare blocks", "http://%s:%d/" % (blocked_host, PORT), True),
            ("shield ON: unlisted made-up host loads", "http://unlisted-9f2a.zzz-unlisted-9f2a.net:%d/" % PORT, False),
            ("shield ON: listed locally (no lookup needed)", "http://phish-test.%s:%d/" % (TEST, PORT), True),
        ]
        if q9_host:
            cases.append(("shield ON: a host only Quad9 blocks", "http://%s:%d/" % (q9_host, PORT), True))
    for name, url, want_block in cases:
        seen = navigate(ws, url, nid)
        nid += 2
        blocked = "Deceptive" in seen or "Dangerous" in seen or "dangerous" in seen.lower() or "security error" in seen.lower()
        ok = blocked == want_block
        results.append(ok)
        print("%-4s %-52s -> %r" % ("PASS" if ok else "FAIL", name, seen[:90]))
    ws.close()
    b.close()
    time.sleep(3)


def main():
    blocked_host = pick_blocked_host()
    print("Cloudflare-blocked host used for the Shield cases (mapped to localhost, never fetched):", blocked_host)
    key = bytes(range(16))
    open(BLOB, "wb").write(mk.build_blob(
        ["phish-test.%s/" % TEST, "exact.%s/login.html" % TEST], key))
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Page)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    results = []
    q9_host = pick_quad9_only_host()
    print("Quad9-only host used (mapped to localhost, never fetched):", q9_host)
    run(False, blocked_host, results, q9_host)
    run(True, blocked_host, results, q9_host)
    print("\n%d of %d cases as expected" % (sum(results), len(results)))
    print("PASS" if all(results) else "FAIL")


if __name__ == "__main__":
    main()

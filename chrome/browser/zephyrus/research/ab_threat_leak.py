"""How many host names does Shield DNS really send during ordinary browsing?

Visits real, ordinary pages (the pages of the recorded corpus) in the real browser with
Shield DNS on and verbose logging, then counts the "Zephyrus Shield DNS asks about <host>"
lines: exactly the host names that left the machine. The browser checks navigations and
frames, not every sub-resource, and skips the 100,000 popular sites, so the count is much
smaller than the number of hosts a page touches.

Usage: ab_threat_leak.py [max pages]
"""
import json
import os
import re
import sys
import time
import urllib.request

import websocket

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile  # noqa: E402

CDP = 9338
PROFILE = r"D:\zeph_perf\threat_leak_prof"
LOG = r"D:\zeph_perf\ab_threat_leak.log"


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    pages = []
    for line in open(r"D:\zeph_perf\h2h\corpus.tsv", encoding="utf-8"):
        cols = line.rstrip("\n").split("\t")
        if len(cols) >= 2 and cols[1] not in pages:
            pages.append(cols[1])
    pages = pages[:limit]
    # Long-tail benign sites, so that a count of zero means "nothing was asked", not "the log is blind".
    pages += [u for u in os.environ.get("EXTRA_PAGES", "").split(",") if u]
    seed_profile(PROFILE, workspaces=False, bookmarks=False)
    json.dump({"zephyrus": {"threat": {"shield_dns": True}}}, open(os.path.join(PROFILE, "Local State"), "w"))
    if os.path.exists(LOG):
        os.remove(LOG)
    b = Browser(PROFILE, args=["--remote-debugging-port=%d" % CDP, "--enable-logging", "--v=1",
                               "--log-file=" + LOG], urls=("about:blank",), wait=25)
    time.sleep(8)
    tab = [t for t in json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/list" % CDP))
           if t.get("type") == "page"][0]
    ws = websocket.create_connection(tab["webSocketDebuggerUrl"], timeout=10, suppress_origin=True)
    for i, url in enumerate(pages):
        ws.send(json.dumps({"id": 10 + i, "method": "Page.navigate", "params": {"url": url}}))
        t0 = time.time()
        while time.time() - t0 < 9:
            ws.settimeout(1)
            try:
                ws.recv()
            except Exception:
                pass
    ws.close()
    b.close()
    time.sleep(3)
    asked = re.findall(r"Zephyrus Shield DNS asks about (\S+)", open(LOG, encoding="utf-8", errors="replace").read())
    print("pages visited: %d" % len(pages))
    print("host names sent to the resolver: %d (%d distinct), %.1f per page" % (
        len(asked), len(set(asked)), len(asked) / max(1, len(pages))))
    for h in sorted(set(asked)):
        print("  ", h)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Live pages that run anti-adblock scripts: does the page tell the visitor it saw a blocker?

  detect_real.py [arms=zephyrus,brave,ubol,chrome] [loads=2]

Each page is loaded cold and read after it has had time to run its checks: the text of the page
and of its same-origin frames is searched for the messages anti-adblock scripts show. Plain Chrome
is the control: a page that shows the message there is matching something else.
Hidden desktop, as the other rulers.
"""
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shield_ruler as R  # noqa: E402

PAGES = [
    "https://www.detectadblock.com/",
    "https://blockadblock.com/",
    "https://www.forbes.com/",
    "https://www.businessinsider.com/",
    "https://www.wired.com/",
    "https://www.cbsnews.com/",
    "https://www.dailymail.co.uk/",
    "https://www.gamespot.com/",
    "https://www.ign.com/",
    "https://www.livemint.com/",
    "https://www.ndtv.com/",
    "https://www.indiatoday.in/",
]

MESSAGE = re.compile(
    r"ad ?block(er)?s? (is |are )?(detected|enabled|on\b|active)|disable (your |the )?ad ?block|"
    r"turn off (your |the )?ad ?block|(whitelist|allowlist) (us|this site|our site)|"
    r"using an ad ?block|ad blocker|adblocker|pause (your )?ad ?block|support us by disabling",
    re.I)

READ = """(() => {
  const texts = [document.body ? document.body.innerText : ''];
  for (const f of document.querySelectorAll('iframe')) {
    try { if (f.contentDocument && f.contentDocument.body) texts.push(f.contentDocument.body.innerText); } catch (e) {}
  }
  return texts.join('\\n');
})()"""


def run_arm(arm, loads):
    p, _ = R.launch(arm, r"D:\zeph_perf\detect")
    time.sleep(20)
    rows = []
    try:
        cdp = R.Cdp(arm["port"])
        s = cdp.the_tab()
        for _ in range(loads):
            for url in PAGES:
                cdp.go(R.NEUTRAL, 8)
                cdp.call("Network.clearBrowserCache", s, wait=20)
                cdp.call("Network.clearBrowserCookies", s, wait=20)
                cdp.go(url, 25)
                time.sleep(12)
                got = cdp.call("Runtime.evaluate", s, wait=15, expression=READ, returnByValue=True)
                text = (got.get("result") or {}).get("value") or ""
                hits = sorted({m.group(0).lower() for m in MESSAGE.finditer(text)})
                rows.append({"url": url, "hits": hits, "text_len": len(text)})
                print("%-12s %-36s %s" % (arm["name"], url[8:44], hits or "-"), flush=True)
    finally:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
    return rows


def main():
    arms = (sys.argv[1] if len(sys.argv) > 1 else "zephyrus,brave,ubol,chrome").split(",")
    loads = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    data = {}
    for name in arms:
        arm = dict([a for a in R.ARMS if a["name"] == name][0])
        arm["port"] += 200
        data[name] = run_arm(arm, loads)
    path = os.path.join(r"D:\zeph_perf\detect", "real_%s.json" % time.strftime("%Y%m%d_%H%M%S"))
    json.dump(data, open(path, "w"), indent=1)
    control = {r["url"] for r in data.get("chrome", []) if r["hits"]}
    print("\npages that show a blocker message (left out: %s)" % (sorted(control) or "none"))
    for name, rows in data.items():
        if name == "chrome":
            continue
        caught = sorted({r["url"] for r in rows if r["hits"] and r["url"] not in control})
        print("  %-10s %d of %d: %s" % (name, len(caught), len(PAGES) - len(control), caught))
    print(path)


if __name__ == "__main__":
    main()

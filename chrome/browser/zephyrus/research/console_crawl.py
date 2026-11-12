#!/usr/bin/env python3
"""Console errors and security-policy violations that only the blocker causes.

  console_crawl.py [sites=80] [arms=zephyrus,zephyrus_off,brave]

The YouTube failure of 2026-11-12 showed itself as console errors ("requires 'TrustedScript'
assignment") that appeared only with Shield on and never in Brave: something the page, and its CSP
report endpoint, can see. For each popular site this loads the page in each arm and records
uncaught exceptions, console errors and CSP violations; it reports those seen with Zephyrus on but
in neither the same engine with Shield off nor Brave. Hidden desktop, as the rulers.
"""
import collections
import csv
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shield_ruler as R  # noqa: E402

EXTRA = """https://www.youtube.com/watch?v=dQw4w9WgXcQ https://www.hotstar.com/in https://www.jiocinema.com/
https://www.flipkart.com/ https://www.amazon.in/ https://www.myntra.com/ https://www.swiggy.com/ https://www.zomato.com/
https://www.irctc.co.in/ https://www.makemytrip.com/ https://www.ndtv.com/ https://timesofindia.indiatimes.com/
https://www.hindustantimes.com/ https://www.indiatoday.in/ https://www.moneycontrol.com/ https://www.cricbuzz.com/
https://www.espncricinfo.com/ https://www.jagran.com/ https://www.bhaskar.com/ https://www.livemint.com/
https://www.gamerant.com/ https://www.cbr.com/ https://www.androidpolice.com/ https://www.screenrant.com/
https://mail.yahoo.com/ https://www.yahoo.com/ https://x.com/ https://www.reddit.com/ https://www.twitch.tv/
https://www.dailymotion.com/ https://vimeo.com/ https://www.forbes.com/ https://www.cnn.com/ https://www.bbc.com/news
https://www.nytimes.com/ https://www.theguardian.com/international https://www.wikipedia.org/ https://www.imdb.com/
https://www.ebay.com/ https://www.walmart.com/ https://www.target.com/ https://www.etsy.com/ https://www.pinterest.com/
https://www.linkedin.com/ https://www.instagram.com/ https://www.facebook.com/ https://www.quora.com/ https://stackoverflow.com/
https://github.com/ https://www.microsoft.com/ https://www.apple.com/ https://www.weather.com/ https://www.msn.com/
https://www.bing.com/ https://duckduckgo.com/ https://www.google.com/search?q=weather https://www.speedtest.net/
https://www.9gag.com/ https://www.twitch.tv/directory https://www.fandom.com/ https://www.wired.com/ https://www.theverge.com/
https://www.cnet.com/ https://www.ign.com/ https://www.gamespot.com/ https://www.businessinsider.com/ https://www.bloomberg.com/
https://www.reuters.com/ https://www.washingtonpost.com/ https://www.dailymail.co.uk/ https://www.foxnews.com/
https://www.news18.com/ https://economictimes.indiatimes.com/ https://www.thehindu.com/ https://www.buzzfeed.com/
https://www.t-online.de/ https://www.spiegel.de/ https://www.lemonde.fr/""".split()

ERR = re.compile(r"TrustedScript|TrustedHTML|Trusted Type|Content Security Policy|Refused to|is not a function|"
                 r"is not defined|Cannot read properties|Unexpected token|SyntaxError|ERR_BLOCKED_BY_CLIENT", re.I)


def norm(text):
    text = re.sub(r"https?://\S+", "<url>", text)
    text = re.sub(r"\d+", "N", text)
    return text[:140]


def crawl(arm, urls):
    p, _ = R.launch(arm, r"D:\zeph_perf\ruler")
    time.sleep(20)
    out = {}
    try:
        c = R.Cdp(arm["port"])
        s = c.the_tab()
        for d in ("Runtime", "Log", "Audits"):
            c.call(d + ".enable", s)
        for url in urls:
            c.go(R.NEUTRAL, 8)
            c.call("Network.clearBrowserCache", s, wait=20)
            c.events.clear()
            c.send("Page.navigate", s, url=url)
            msgs = collections.Counter()
            end = time.time() + 14
            while time.time() < end:
                m = c.read()
                if not m or m.get("sessionId") != s:
                    continue
                q = m.get("params", {})
                k = m.get("method")
                if k == "Runtime.exceptionThrown":
                    d = q["exceptionDetails"]
                    msgs["exception: " + norm(((d.get("exception") or {}).get("description") or d.get("text") or "").split("\n")[0])] += 1
                elif k == "Log.entryAdded" and q["entry"].get("level") == "error" and "ERR_BLOCKED_BY_CLIENT" not in q["entry"].get("text", ""):
                    msgs["log: " + norm(q["entry"].get("text", ""))] += 1
                elif k == "Audits.issueAdded":
                    code = q["issue"].get("code", "")
                    if "ContentSecurityPolicy" in code or "TrustedType" in json.dumps(q["issue"])[:2000]:
                        det = q["issue"].get("details", {}).get("contentSecurityPolicyIssueDetails", {})
                        msgs["csp: " + det.get("contentSecurityPolicyViolationType", code)] += 1
            out[url] = dict(msgs)
            print("%-13s %-40s %d distinct" % (arm["name"], url[8:48], len(msgs)), flush=True)
    finally:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
    return out


def report(data):
    on = data["zephyrus"]
    print("\nerrors seen with Zephyrus Shield on, and in neither Shield off nor Brave:")
    total = 0
    for url, msgs in on.items():
        others = set(data.get("zephyrus_off", {}).get(url, {})) | set(data.get("brave", {}).get(url, {}))
        own = {m: n for m, n in msgs.items() if m not in others}
        if own:
            total += 1
            print("  %s" % url)
            for m, n in sorted(own.items(), key=lambda kv: -kv[1])[:6]:
                print("      %3dx %s" % (n, m))
    print("\n%d of %d sites have errors only Shield causes" % (total, len(on)))


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 80
    arms = (sys.argv[2] if len(sys.argv) > 2 else "zephyrus,zephyrus_off,brave").split(",")
    urls = os.environ['CRAWL_URLS'].split() if os.environ.get('CRAWL_URLS') else EXTRA[:count]
    data = {}
    for name in arms:
        arm = dict([a for a in R.ARMS if a["name"] == name][0])
        arm["port"] += 900
        data[name] = crawl(arm, urls)
    path = os.path.join(r"D:\zeph_perf\audit", "console_%s.json" % time.strftime("%Y%m%d_%H%M%S"))
    json.dump(data, open(path, "w"), indent=1)
    report(data)
    print(path)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "report":
        report(json.load(open(sys.argv[2])))
    else:
        main()

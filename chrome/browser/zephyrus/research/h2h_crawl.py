"""Record the requests of real page loads with nothing blocked, via Zephyrus's NetLog.

Needs a Zephyrus build and the author's window-driving helper (cap.py: launch, seed a
profile, close); swap in any way of starting the browser with --log-net-log.

Writes D:\\zeph_perf\\h2h\\corpus.tsv: url <TAB> page <TAB> type, one request per line.
"""
import json, os, re, subprocess, sys, time
from urllib.parse import urlparse

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile

OUT = r"D:\zeph_perf\h2h"
os.makedirs(OUT, exist_ok=True)
NET = os.path.join(OUT, "net.json")

SITES = """https://www.cnn.com/ https://www.bbc.com/news https://www.nytimes.com/ https://www.theguardian.com/international
https://www.reuters.com/ https://www.forbes.com/ https://www.buzzfeed.com/ https://www.espn.com/ https://www.imdb.com/
https://www.reddit.com/ https://www.yahoo.com/ https://www.msn.com/ https://timesofindia.indiatimes.com/
https://www.hindustantimes.com/ https://www.ndtv.com/ https://www.cricbuzz.com/ https://www.moneycontrol.com/
https://www.flipkart.com/ https://www.amazon.in/ https://www.ebay.com/ https://www.walmart.com/ https://www.target.com/
https://www.washingtonpost.com/ https://www.foxnews.com/ https://www.dailymail.co.uk/ https://www.indiatoday.in/
https://economictimes.indiatimes.com/ https://www.news18.com/ https://www.thehindu.com/ https://www.zomato.com/""".split()

prof = r"D:\zeph_perf\h2h_prof"
seed_profile(prof, workspaces=False, bookmarks=False,
             extra_prefs={"zephyrus.adblock.enabled": False})   # nothing blocked: record everything
if os.path.exists(NET):
    os.remove(NET)
b = Browser(prof, args=[f"--log-net-log={NET}", "--net-log-capture-mode=Default"], urls=tuple(SITES), wait=90)
time.sleep(45)
b.close()
time.sleep(3)

text = open(NET, encoding="utf-8", errors="replace").read()
text = text.rstrip().rstrip(",")
if not text.endswith("]}"):
    text += "]}"                      # the log is cut when the process is killed
data = json.loads(text)
names = {v: k for k, v in data["constants"]["logEventTypes"].items()}
rows, seen = [], set()
for ev in data["events"]:
    if names.get(ev["type"]) != "URL_REQUEST_START_JOB":
        continue
    p = ev.get("params") or {}
    url, ini = p.get("url"), p.get("initiator")
    if not url or not url.startswith("http") or not ini or ini == "not an origin":
        continue
    host = urlparse(url).path.lower()
    ext = host.rsplit(".", 1)[-1] if "." in host.rsplit("/", 1)[-1] else ""
    kind = {"js": "script", "mjs": "script", "css": "stylesheet", "png": "image", "jpg": "image", "jpeg": "image",
            "gif": "image", "webp": "image", "svg": "image", "ico": "image", "woff": "font", "woff2": "font",
            "ttf": "font", "mp4": "media", "webm": "media", "m3u8": "media", "json": "xhr"}.get(ext, "other")
    rows.append((url, ini + "/", kind))
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, "corpus.tsv"), "w", encoding="utf-8", newline="\n") as f:
    for r in rows:
        f.write("\t".join(r) + "\n")
by_page = {}
for u, i, k in rows:
    by_page[i] = by_page.get(i, 0) + 1
print("requests:", len(rows), "| pages:", len(by_page), "| third-party share: %.0f%%" % (
    100.0 * sum(1 for u, i, k in rows if urlparse(u).hostname and urlparse(i).hostname and
                not urlparse(u).hostname.endswith(".".join(urlparse(i).hostname.split(".")[-2:]))) / max(len(rows), 1)))

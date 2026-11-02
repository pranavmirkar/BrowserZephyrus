"""One REAL rule per stand-in: a URL the rule blocks, and how a page would request it.

Reads the preprocessed list, takes `redirect=` rules (host-anchored, no $domain=/regex), and for
each resource name writes a case {name, url, kind}: kind says how the test page asks for it
(script, image, xhr, media-audio, media-video, frame, css). Writes D:\\zeph_perf\\rr\\cases.json.
"""
import json, re, collections

LIST = r"D:\zeph_perf\h2h_rx_run\preprocessed.txt"
OUT = r"D:\zeph_perf\rr\cases.json"
ALIAS = {"noopjs": "noop.js", "nooptext": "noop.txt", "noopframe": "noop.html", "noopjson": "noop.json",
         "noopmp3-0.1s": "noop-0.1s.mp3", "noopmp4-1s": "noop-1s.mp4", "google-ima3": "google-ima.js",
         "google-analytics.com/analytics.js": "google-analytics_analytics.js",
         "google-analytics.com/ga.js": "google-analytics_ga.js",
         "google-analytics.com/cx/api.js": "google-analytics_cx_api.js",
         "googletagmanager.com/gtm.js": "googletagmanager_gtm.js",
         "googletagservices.com/gpt.js": "googletagservices_gpt.js",
         "scorecardresearch.com/beacon.js": "scorecardresearch_beacon.js",
         "amazon-adsystem.com/aax2/amzn_ads.js": "amazon_ads.js", "popads.net.js": "popads.js",
         "noopvast-2.0": "noop-vast2.xml", "noopvast-3.0": "noop-vast3.xml", "noopvmap-1.0": "noop-vmap1.0.xml",
         "1x1-transparent.gif": "1x1.gif"}
KIND_BY_OPT = {"script": "script", "image": "image", "xhr": "xhr", "xmlhttprequest": "xhr", "media": "media",
               "frame": "frame", "subdocument": "frame", "css": "css", "stylesheet": "css"}
KIND_BY_RESOURCE = [(".js", "script"), (".gif", "image"), (".png", "image"), (".mp3", "audio"), (".mp4", "video"),
                    (".html", "frame"), (".css", "css"), (".txt", "xhr"), (".json", "xhr"), (".xml", "xhr"), ("empty", "xhr")]
cases = collections.OrderedDict()
for line in open(LIST, encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if not line.startswith("||") or "$" not in line or re.search(r"#[@?$%]*#", line):
        continue
    pat, _, opts = line.rpartition("$")
    toks = opts.split(",")
    m = [t for t in toks if t.startswith("redirect=")]
    if not m or any(t.split("=")[0] in ("domain", "from", "to", "denyallow", "badfilter", "important", "method",
                                        "header", "1p", "first-party", "strict1p") or t.startswith("~") for t in toks):
        continue
    name = m[0][9:].split(":")[0]
    name = ALIAS.get(name, name)
    if name == "none" or name in cases:
        continue
    host_path = pat[2:].replace("*", "x").rstrip("|")
    host_path = re.sub(r"\^$", "/", host_path).replace("^", "/")
    if not re.match(r"^[a-z0-9.-]+\.[a-z]{2,}(/|$)", host_path) or "|" in host_path:
        continue
    if host_path.endswith("/") is False and "/" not in host_path:
        host_path += "/"
    kind = next((KIND_BY_OPT[t] for t in toks if t in KIND_BY_OPT), None)
    if kind == "media":
        kind = "video" if name.endswith(".mp4") else "audio"
    if kind is None:
        kind = next((k for suffix, k in KIND_BY_RESOURCE if name.endswith(suffix) or name == suffix), "script")
    cases[name] = {"name": name, "url": "https://" + host_path, "kind": kind, "rule": line[:160]}
json.dump(list(cases.values()), open(OUT, "w"), indent=1)
print(len(cases), "cases")
for c in cases.values():
    print(f"  {c['name']:36s} {c['kind']:7s} {c['url'][:80]}")

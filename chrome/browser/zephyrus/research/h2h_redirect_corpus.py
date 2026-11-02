"""Requests built from EVERY real $redirect= / $redirect-rule= rule of the list, for a three-way comparison
(Zephyrus engine, Zephyrus index, Brave's adblock-rust) of whether each is blocked.
One request per rule, at a URL the pattern matches, with the page and type its options ask for.
Writes h2h_rr/redirect_corpus.tsv: url, page, type.
"""
import re, random, os

LIST = r"D:\zeph_perf\h2h_rx_run\preprocessed.txt"
OUT = r"D:\zeph_perf\h2h_rr\redirect_corpus.tsv"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
TYPES = {"script": "script", "image": "image", "stylesheet": "stylesheet", "css": "stylesheet", "xhr": "xhr",
         "xmlhttprequest": "xhr", "subdocument": "subdocument", "frame": "subdocument", "media": "media",
         "font": "font", "object": "object", "ping": "ping", "other": "other", "websocket": "websocket"}
NAMES_OK = {"third-party", "3p", "first-party", "1p", "important", "domain", "from", "to", "denyallow", "redirect",
            "redirect-rule", "match-case"} | set(TYPES)
random.seed(1)
rows, skipped = [], 0
for line in open(LIST, encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if (not line.startswith("||") and not line.startswith("|http")) or "$" not in line or re.search(r"#[@?$%]*#", line):
        continue
    pat, _, opts = line.rpartition("$")
    toks = opts.split(",")
    if not any(t.startswith("redirect=") or t.startswith("redirect-rule=") for t in toks):
        continue
    if any(t.split("=")[0].lstrip("~") not in NAMES_OK for t in toks) or "badfilter" in toks:
        skipped += 1
        continue
    p = pat[2:] if pat.startswith("||") else pat
    p = p.replace("*", "x").rstrip("|")
    p = re.sub(r"\^$", "/", p).replace("^", "/")
    if not re.match(r"^[a-z0-9.-]+\.[a-z]{2,}(/|\?|$)", p) or "|" in p:
        skipped += 1
        continue
    if "/" not in p and "?" not in p:
        p += "/"
    url = "https://" + p
    typ = next((TYPES[t.lstrip("~")] for t in toks if t in TYPES), "script")
    if not any(t in TYPES for t in toks):
        typ = "script"
    page = "https://news.example.org/"
    dom = [t for t in toks if t.startswith(("domain=", "from="))]
    if dom:
        ds = [d for d in dom[0].split("=", 1)[1].split("|") if d and not d.startswith("~")]
        if ds:
            page = "https://" + ds[0].replace(".*", ".com") + "/"
    if "1p" in toks or "first-party" in toks:
        page = "https://" + re.match(r"https://([^/?]+)", url).group(1) + "/"
    rows.append((url, page, typ))
random.shuffle(rows)
open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join("\t".join(r) for r in rows) + "\n")
print(len(rows), "requests from redirect rules |", skipped, "rules skipped by the generator")

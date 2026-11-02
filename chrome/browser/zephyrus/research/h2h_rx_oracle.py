"""An independent oracle for the regex-rule step: Python `re` + the option semantics,
compared with what AdblockRegexMatcher said (zeph_regex_only.txt = block or important).

Run: python rx_oracle.py [run_dir]
"""
import re, sys, collections

RUN = sys.argv[1] if len(sys.argv) > 1 else r"D:\zeph_perf\h2h_rx_run"
CORPUS = sys.argv[2] if len(sys.argv) > 2 else r"D:\zeph_perf\h2h_rx\rx_corpus.tsv"
LIST = r"D:\zeph_perf\h2h_rx_run\preprocessed.txt"  # what the engines load: !#if blocks resolved

TYPES = {"script": "script", "image": "image", "stylesheet": "stylesheet", "css": "stylesheet", "object": "object",
         "xmlhttprequest": "xhr", "xhr": "xhr", "subdocument": "subdocument", "frame": "subdocument",
         "font": "font", "media": "media", "ping": "ping", "websocket": "websocket", "other": "other",
         "popup": "popup", "popunder": "popup", "document": "document", "doc": "document"}
EVERY = {"script", "image", "stylesheet", "object", "xhr", "subdocument", "font", "media", "ping", "websocket",
         "other", "popup", "document"}
SUPPORTED = set(TYPES) | {"third-party", "3p", "first-party", "1p", "important", "denyallow", "domain", "from",
                          "to", "match-case", "all"}


def reg_domain(host):
    parts = host.split(".")
    return ".".join(parts[-2:]) if len(parts) >= 2 else host


def host_in(host, dom):
    if dom.endswith(".*"):
        base = dom[:-2]
        parts = host.split(".")
        for k in range(1, len(parts)):
            cand = ".".join(parts[:k])  # strip a registry suffix: try every prefix
            if cand == base or cand.endswith("." + base):
                return True
        return False
    return host == dom or host.endswith("." + dom)


def host_of(url):
    m = re.match(r"https?://([^/:?#]*)", url)
    return m.group(1).lower() if m else ""


rules = []
unsupported = []
for line in open(LIST, encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if not line or line[0] in "![" or re.search(r"#[@?$%]*#", line):
        continue
    exc = line.startswith("@@")
    body = line[2:] if exc else line
    if not body.startswith("/") or len(body) < 2:
        continue
    close = body.rfind("/")
    if close <= 0 or not (close == len(body) - 1 or body[close + 1] == "$"):
        continue
    rx = body[1:close]
    optl = [o for o in (body[close + 2:] if close < len(body) - 1 else "").split(",") if o]
    names = [o.split("=")[0].lstrip("~") for o in optl]
    if any(n not in SUPPORTED for n in names):
        continue
    if exc and any(n in ("document", "doc") for n in names):
        continue
    r = dict(exc=exc, important="important" in names, mc="match-case" in names, line=line,
             types=set(), not_types=set(), party=0, dom_in=[], dom_ex=[], to_in=[], to_ex=[], deny=[])
    for o in optl:
        neg = o.startswith("~")
        o2 = o[1:] if neg else o
        k, _, v = o2.partition("=")
        if k in TYPES:
            (r["not_types"] if neg else r["types"]).add(TYPES[k])
        elif k == "all":
            r["types"] |= EVERY
        elif k in ("third-party", "3p"):
            r["party"] = 1 if neg else 2
        elif k in ("first-party", "1p"):
            r["party"] = 2 if neg else 1
        elif k in ("domain", "from"):
            for d in v.split("|"):
                (r["dom_ex"] if d.startswith("~") else r["dom_in"]).append(d.lstrip("~").lower())
        elif k == "to":
            for d in v.split("|"):
                (r["to_ex"] if d.startswith("~") else r["to_in"]).append(d.lstrip("~").lower())
        elif k == "denyallow":
            r["deny"] += [d.lower() for d in v.split("|") if d]
    # RE2 (and Brave's Rust regex) have no lookahead or back-references: those rules are
    # left out of both, so they are left out of the oracle too, and counted.
    if re.search(r"\(\?(=|!|<=|<!)|\[1-9]", rx):
        unsupported.append(line)
        continue
    try:
        r["re"] = re.compile(rx, 0 if r["mc"] else re.I)
    except re.error:
        continue
    rules.append(r)
print("oracle rules:", len(rules), "| lookahead/backref rules left out:", len(unsupported))


def canon(url):
    m = re.match(r"(https?://[^/?#]*)(.*)$", url, re.S)
    return m.group(1).lower() + m.group(2) if m else url


def expected(url, page, typ):
    url = canon(url)
    host, phost = host_of(url), host_of(page)
    third = reg_domain(host) != reg_domain(phost)
    block = False
    for r in rules:
        if r["exc"]:
            continue
        if r["types"]:
            if typ not in r["types"]:
                continue
        elif typ in ("document", "popup"):
            continue
        if typ in r["not_types"]:
            continue
        if r["party"] == 1 and third:
            continue
        if r["party"] == 2 and not third:
            continue
        if any(host_in(host, d) for d in r["deny"]):
            continue
        if r["to_in"] and not any(host_in(host, d) for d in r["to_in"]):
            continue
        if any(host_in(host, d) for d in r["to_ex"]):
            continue
        if r["dom_in"] and not any(host_in(phost, d) for d in r["dom_in"]):
            continue
        if any(host_in(phost, d) for d in r["dom_ex"]):
            continue
        if r["re"].search(url):
            return True, r["line"]
    return False, None


rows = [l.rstrip("\n").split("\t") for l in open(CORPUS, encoding="utf-8")]
z = [l.strip() for l in open(RUN + r"\zeph_regex_only.txt")]
assert len(rows) == len(z)
agree = 0
only_oracle, only_zeph = [], []
for i, (u, p, t) in enumerate(rows):
    e, why = expected(u, p, t)
    zz = z[i] == "1"
    if e == zz:
        agree += 1
    elif e:
        only_oracle.append((u, p, t, why))
    else:
        only_zeph.append((u, p, t))
print(f"requests {len(rows)} | agree {agree} | oracle-only {len(only_oracle)} | zephyrus-only {len(only_zeph)}")
for u, p, t, why in only_oracle[:12]:
    print("  ORACLE ONLY", t, u[:100], "| page", p, "\n      ", why[:170])
for u, p, t in only_zeph[:12]:
    print("  ZEPHYRUS ONLY", t, u[:110], "| page", p)

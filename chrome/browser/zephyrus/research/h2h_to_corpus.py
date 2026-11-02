"""Requests built from the real $to= rules of the full list, to compare three engines.

For each plain (non-regex) rule that has $to= and nothing the engine cannot read, make
a URL the pattern matches, once at a host the rule's $to= admits and once at a host it
does not, so the answer depends on $to=. Writes h2h/to_corpus.tsv: url, page, type.
"""
import re, random

LIST = r"D:\zeph_perf\abc_prof_on\ZephyrusAdBlock\filters.txt"
OUT = r"D:\zeph_perf\h2h\to_corpus.tsv"
SUPPORTED = {"third-party", "3p", "first-party", "1p", "script", "image", "stylesheet", "css", "object",
             "xmlhttprequest", "xhr", "subdocument", "frame", "font", "media", "ping", "websocket", "other",
             "popup", "document", "doc", "important", "denyallow", "domain", "from", "to"}
TYPE = {"script": "script", "image": "image", "stylesheet": "stylesheet", "css": "stylesheet", "xhr": "xhr",
        "xmlhttprequest": "xhr", "font": "font", "media": "media", "subdocument": "subdocument",
        "frame": "subdocument", "doc": "document", "document": "document", "other": "other"}
random.seed(11)
rows = []
n_rules = 0
for line in open(LIST, encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if not line or line[0] in "![" or re.search(r"#[@?$%]*#", line):
        continue
    body = line[2:] if line.startswith("@@") else line
    if "$" not in body or re.match(r"/.+/(\$.*)?$", body):
        continue
    pat, _, opts = body.rpartition("$")
    optl = opts.split(",")
    names = [o.split("=")[0].lstrip("~") for o in optl]
    if "to" not in names or any(n not in SUPPORTED for n in names):
        continue
    to = [o for o in optl if o.startswith("to=")][0][3:].split("|")
    inc = [h for h in to if h and not h.startswith("~")]
    exc = [h[1:] for h in to if h.startswith("~")]
    dom = [o for o in optl if o.startswith(("domain=", "from="))]
    page = "https://news.example.org/"
    if dom:
        d = [x for x in dom[0].split("=", 1)[1].split("|") if x and not x.startswith("~")]
        if d:
            page = "https://" + d[0].replace(".*", ".com") + "/"
    rtype = "script"
    for n in names:
        if n in TYPE:
            rtype = TYPE[n]
            break
    # a URL the pattern matches
    anchored_host = pat.startswith("||")
    p = pat[2:] if anchored_host else pat.lstrip("|")
    p = p.replace("*", "x7").replace("^", "/").rstrip("|")
    if not p:
        continue
    n_rules += 1
    in_hosts = [inc[0]] if inc else ["admitted.example"]
    out_hosts = ["unrelated.invalid"] + ([exc[0]] if exc else [])
    for h in in_hosts + out_hosts:
        if anchored_host:
            host_part = p.split("/")[0]
            # the pattern names its own host; keep it, and put the $to= host in a subdomain-free place
            url = "https://" + p if "/" in p else "https://" + p + "/"
            if inc and not host_part.endswith(inc[0]):
                continue            # pattern host and $to= disagree: nothing to ask
        else:
            url = "https://" + h + "/" + p.lstrip("/")
        # a real host: a pattern like ||example. means "example.<any suffix>"
        m = re.match(r"https://([^/]*)(/.*)?$", url)
        host, rest = m.group(1), m.group(2) or "/"
        if host.endswith("."):
            host += "xyz"
        if "." not in host:
            host += ".xyz"
        rows.append(("https://" + host + rest, page, rtype))
random.shuffle(rows)
open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join("\t".join(r) for r in rows) + "\n")
print("rules with $to= and only options we read:", n_rules, "| requests:", len(rows))

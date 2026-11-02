"""Requests built from the real /regex/ rules of the full list, to compare three engines.

For each regex rule whose options we read and whose regex Python can parse, make URLs
the regex matches (by walking the parsed regex and choosing a string for each node),
plus near-misses (one character changed), each once with the request type / page the
rule's options ask for and once with them wrong. Writes h2h_rx/rx_corpus.tsv:
url, page, type.   Run: python rx_corpus.py
"""
import re, random, sys
try:
    import re._parser as sre_parse, re._constants as sre_c
except ImportError:  # python < 3.11
    import sre_parse, sre_constants as sre_c

LIST = r"D:\zeph_perf\abc_prof_on\ZephyrusAdBlock\filters.txt"
OUT = r"D:\zeph_perf\h2h_rx\rx_corpus.tsv"
SUPPORTED = {"third-party", "3p", "first-party", "1p", "script", "image", "stylesheet", "css", "object",
             "xmlhttprequest", "xhr", "subdocument", "frame", "font", "media", "ping", "websocket", "other",
             "popup", "document", "doc", "important", "denyallow", "domain", "from", "to", "match-case", "all"}
TYPE = {"script": "script", "image": "image", "stylesheet": "stylesheet", "css": "stylesheet", "xhr": "xhr",
        "xmlhttprequest": "xhr", "font": "font", "media": "media", "subdocument": "subdocument",
        "frame": "subdocument", "doc": "document", "document": "document", "other": "other", "popup": "popup"}
random.seed(7)
ALNUM = "abcdefghijklmnopqrstuvwxyz0123456789"


def pick_in(items):
    """A character matching a set of class items."""
    neg = False
    chars = []
    for op, av in items:
        if op == sre_c.NEGATE:
            neg = True
        elif op == sre_c.LITERAL:
            chars.append(chr(av))
        elif op == sre_c.RANGE:
            lo, hi = av
            chars.extend(chr(c) for c in range(lo, min(hi, lo + 80) + 1))
        elif op == sre_c.CATEGORY:
            if av == sre_c.CATEGORY_DIGIT:
                chars.extend("0123456789")
            elif av == sre_c.CATEGORY_WORD:
                chars.extend(ALNUM + "_")
            elif av == sre_c.CATEGORY_SPACE:
                chars.append(" ")
    if neg:
        pool = [c for c in ALNUM if c not in chars]
        return random.choice(pool) if pool else "a"
    return random.choice(chars) if chars else "a"


def gen(parsed, depth=0):
    out = []
    for op, av in parsed:
        if op == sre_c.LITERAL:
            out.append(chr(av))
        elif op == sre_c.NOT_LITERAL:
            out.append("a" if chr(av) != "a" else "b")
        elif op == sre_c.ANY:
            out.append(random.choice(ALNUM))
        elif op == sre_c.IN:
            out.append(pick_in(av))
        elif op == sre_c.BRANCH:
            out.append(gen(random.choice(av[1]), depth + 1))
        elif op == sre_c.SUBPATTERN:
            out.append(gen(av[3], depth + 1))
        elif op in (sre_c.MAX_REPEAT, sre_c.MIN_REPEAT):
            lo, hi, sub = av
            hi = min(hi, lo + 3) if hi != sre_c.MAXREPEAT else lo + 3
            n = random.randint(lo, max(lo, hi))
            out.append("".join(gen(sub, depth + 1) for _ in range(n)))
        elif op == sre_c.CATEGORY:
            out.append("1" if av == sre_c.CATEGORY_DIGIT else "a")
        elif op == sre_c.AT:
            pass
        elif op == sre_c.ASSERT or op == sre_c.ASSERT_NOT:
            pass
        else:
            raise ValueError(op)
    return "".join(out)


def host_of(url):
    m = re.match(r"https?://([^/:?#]*)", url)
    return m.group(1) if m else ""


rules = []
for line in open(LIST, encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if not line or line[0] in "![" or re.search(r"#[@?$%]*#", line):
        continue
    body = line[2:] if line.startswith("@@") else line
    if not body.startswith("/") or len(body) < 2:
        continue
    close = body.rfind("/")
    if close <= 0 or not (close == len(body) - 1 or body[close + 1] == "$"):
        continue
    rx = body[1:close]
    opts = body[close + 2:] if close < len(body) - 1 else ""
    optl = [o for o in opts.split(",") if o]
    names = [o.split("=")[0].lstrip("~") for o in optl]
    if any(n not in SUPPORTED for n in names):
        continue
    rules.append((line, rx, optl, names))

rows = []
stats = {"rules": len(rules), "generated": 0, "unparseable": 0, "nomatch": 0}
for line, rx, optl, names in rules:
    flags = 0 if "match-case" in names else re.I
    try:
        parsed = sre_parse.parse(rx, flags)
        creg = re.compile(rx, flags)
    except Exception:
        stats["unparseable"] += 1
        continue
    rtype = "script"
    for n in names:
        if n in TYPE:
            rtype = TYPE[n]
            break
    is_3p = "3p" in names or "third-party" in names
    is_1p = "1p" in names or "first-party" in names
    dom = [o for o in optl if o.startswith(("domain=", "from="))]
    to = [o for o in optl if o.startswith("to=")]
    made = 0
    for attempt in range(40):
        if made >= 3:
            break
        try:
            s = gen(parsed)
        except ValueError:
            stats["unparseable"] += 1
            break
        # make it a URL the regex still matches
        url = None
        for prefix in ("", "https://cdn.example", "https://foo", "https://cdn.example/"):
            cand = prefix + s
            if not cand.startswith("http"):
                continue
            if creg.search(cand):
                url = cand
                break
        if not url or not re.match(r"https?://[a-z0-9.-]+(/|$)", url):
            continue
        host = host_of(url)
        if not host or host.endswith(".") or "." not in host:
            continue
        page = "https://news.example.org/"
        if dom:
            d = [x for x in dom[0].split("=", 1)[1].split("|") if x and not x.startswith("~")]
            if d:
                page = "https://" + d[0].replace(".*", ".com") + "/"
        if is_1p:
            page = "https://" + host + "/"
        elif is_3p and host.endswith("example.org"):
            page = "https://other.example/"
        made += 1
        stats["generated"] += 1
        # as the rule asks, with the type wrong, and as a near miss
        rows.append((url, page, rtype))
        wrong = "image" if rtype != "image" else "script"
        rows.append((url, page, wrong))
        if len(url) > 30:
            k = random.randrange(len(url) - 12, len(url))
            miss = url[:k] + ("Z" if url[k] != "Z" else "Y") + url[k + 1:]
            rows.append((miss, page, rtype))
random.shuffle(rows)
seen = set()
uniq = []
for r in rows:
    if r not in seen:
        seen.add(r)
        uniq.append(r)
open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join("\t".join(r) for r in uniq) + "\n")
print(stats, "| requests:", len(uniq))

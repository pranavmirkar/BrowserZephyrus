"""URLs built from the REAL $urlskip rules of the list, with the destination each must skip to.

For each rule (uBO sections only, plain pattern, options limited to $doc) an address is made that
the pattern matches and that carries a destination, by running the rule's steps backwards in Python:
  +https -> strip the scheme   -base64 -> base64   -safebase64 -> url-safe base64
  -uricomponent -> percent-encode   ?name -> put it in that query parameter   # -> put it in the fragment
A rule with a regex step or &N step is not invertible this way and is left out (counted).
Each address is also written with its parameter removed, and on a host the pattern does not name:
neither may skip ("-").   Writes D:\\zeph_perf\\h2h_rx\\urlskip_corpus.tsv: url<TAB>expected.
"""
import base64, re, random
from urllib.parse import quote

LIST = r"D:\zeph_perf\h2h_rx_run\preprocessed.txt"
OUT = r"D:\zeph_perf\h2h_rx\urlskip_corpus.tsv"
random.seed(5)
KNOWN = {"doc", "document", "1p", "3p", "first-party", "third-party", "strict1p", "strict3p", "all", "image", "script",
         "stylesheet", "css", "xhr", "xmlhttprequest", "frame", "subdocument", "font", "media", "object", "ping",
         "websocket", "other", "popup", "important", "match-case", "to", "from", "domain", "denyallow", "badfilter",
         "header", "method", "redirect", "redirect-rule", "csp", "removeparam", "ipaddress", "reason"}


def tokenize(text):
    out, i = [], 0
    while i < len(text):
        if text[i] == " ":
            i += 1
            continue
        start = i
        if text[i] == "/":
            j, in_class = i + 1, False
            while j < len(text):
                c = text[j]
                if c == "\\":
                    j += 1
                elif in_class:
                    in_class = c != "]"
                elif c == "[":
                    in_class = True
                elif c == "/":
                    break
                j += 1
            if j >= len(text):
                return None
            i = j + 1
        while i < len(text) and text[i] != " ":
            i += 1
        out.append(text[start:i])
    return out


def b64(s, safe):
    e = base64.b64encode(s.encode()).decode()
    e = e.rstrip("=") if random.random() < 0.5 else e
    return e.replace("+", "-").replace("/", "_") if safe else e


regex_rules = []
rules, left_out = [], {"regex pattern": 0, "regex/&N step": 0, "other options": 0, "untrusted": 0, "exception": 0}
trusted = False
for line in open(LIST, encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    if line.startswith("! ====="):
        trusted = line.startswith("! ===== https://raw.githubusercontent.com/uBlockOrigin/uAssets/")
        continue
    if "urlskip=" not in line or line.startswith("!") or re.search(r"#[@?$%]*#", line):
        continue
    if not trusted:
        left_out["untrusted"] += 1
        continue
    if line.startswith("@@"):
        left_out["exception"] += 1
        continue
    at = line.index("urlskip=")
    dollar = line.rfind("$", 0, at)
    pattern, opts = line[:dollar], line[dollar + 1:]
    toks = opts.split(",")
    steps_text, rest, in_value = None, [], False
    for t in toks:
        if steps_text is None and t.startswith("urlskip="):
            steps_text, in_value = t[8:], True
        elif in_value and t.split("=")[0].lstrip("~") not in KNOWN:
            steps_text += "," + t
        else:
            in_value = False
            rest.append(t)
    if pattern.startswith("/"):
        left_out["regex pattern"] += 1
        continue
    if "%3A%2F%2F" in pattern or "%2F%2F" in pattern or "://" in pattern:
        left_out["other options"] += 1   # the pattern names one destination itself
        continue
    if any(r not in ("doc", "document") for r in rest):
        left_out["other options"] += 1
        continue
    steps = tokenize(steps_text)
    if not steps or any(s.startswith("&") and len(s) > 1 for s in steps):
        left_out["regex/&N step"] += 1
        continue
    if any(s.startswith("/") for s in steps):
        regex_rules.append((line, pattern, steps))
        continue
    rules.append((line, pattern, steps))


def materialize(pattern):
    """A host-anchored address the pattern matches, ending where the pattern ends."""
    p = pattern[2:] if pattern.startswith("||") else pattern.lstrip("|")
    p = p.rstrip("|")
    out = ""
    i = 0
    while i < len(p):
        c = p[i]
        if c == "*":
            nxt = p[i + 1:i + 2]
            if nxt == "&" and "?" not in out:
                out += "x?zz=1"
            elif nxt == "^":
                out += "x"
            elif nxt.isalpha() and out.endswith(("?", "&")):
                out += "zz=1&"
            elif nxt.isalpha() and "=" in p[i + 1:] and "/" not in p[i + 1:p.find("=", i + 1)]:
                out += "x?zz=1&" if "?" not in out else "x&"
            else:
                out += "x"
        elif c == "^":
            out += "&" if "?" in out else ("/" if i == len(p) - 1 else "?")
        else:
            out += c
        i += 1
    return out


def invert(steps, dest, pattern):
    """The address that these steps turn into `dest`, or None."""
    steps = [s for s in steps if s != "-blocked"]
    cur = dest
    base = materialize(pattern)
    for n, step in enumerate(reversed(steps)):
        first = n == len(steps) - 1
        if step == "+https":
            if not cur.startswith("https://"):
                return None
            cur = "http://" + cur[len("https://"):]
        elif step == "-base64":
            cur = b64(cur, False)
        elif step == "-safebase64":
            cur = b64(cur, True)
        elif step == "-uricomponent":
            cur = quote(cur, safe="")
        elif step.startswith("?") and len(step) > 1:
            name = step[1:]
            value = quote(cur, safe="")
            if first:
                # the pattern may already end in `name=` or `name=http`
                m = re.search(r"(?<![A-Za-z0-9_.-])" + re.escape(name) + r"(=[A-Za-z0-9%]*)?$", base)
                if m:
                    base = base[:m.start()] + name + "="
                    cur = "https://" + base + value
                else:
                    sep = "&" if "?" in base else "?"
                    cur = "https://" + base + sep + name + "=" + value
            else:
                cur = "https://r.example/p?" + name + "=" + value
        elif step == "#":
            cur = ("https://" + base + "#" + cur) if first else "https://r.example/p#" + cur
        else:
            return None
    return cur


rows, made = [], 0
for line, pattern, steps in rules:
    # a pattern that wants `http%` (not `https`) right after the parameter is for http destinations
    wants_plain_http = bool(re.search(r"=http%", pattern)) and "+https" not in steps
    dest = "http%s://dest%d.example.net/landing/page?a=1&b=two" % ("" if wants_plain_http else "s", random.randint(1, 999))
    url = invert(steps, dest, pattern)
    if not url or not re.match(r"https://[a-z0-9.-]+(/|\?|#|$)", url):
        continue
    made += 1
    rows.append((url, dest, line))
    # the parameter gone: nothing to extract
    m = re.search(r"[?&]([A-Za-z0-9_.-]+)=", url)
    if m and any(s.startswith("?") for s in steps):
        rows.append((url.split("?")[0] + "?other=1", "-", line))
    # the same address on a host the rule does not name
    host = re.match(r"https://([^/?#]+)", url).group(1)
    rows.append((url.replace(host, "unrelated-host.example", 1), "-", line))
random.shuffle(rows)
open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join("\t".join(r) for r in rows) + "\n")
print("rules used:", len(rules), "| addresses made:", made, "| rows:", len(rows), "| left out:", left_out)


# ---- rules with a regex step: build the matching text around the destination ----------------
try:
    import re._parser as sre_parse, re._constants as sre_c
except ImportError:
    import sre_parse, sre_constants as sre_c
ALNUM = "abcdefghijklmnopqrstuvwxyz0123456789"


def gen(parsed, payload):
    out = []
    for op, av in parsed:
        if op == sre_c.LITERAL:
            out.append(chr(av))
        elif op == sre_c.NOT_LITERAL:
            out.append("a" if chr(av) != "a" else "b")
        elif op == sre_c.ANY:
            out.append(random.choice(ALNUM))
        elif op == sre_c.IN:
            ch = [chr(a) if o == sre_c.LITERAL else (chr(a[0]) if o == sre_c.RANGE else "a") for o, a in av if o != sre_c.NEGATE]
            neg = any(o == sre_c.NEGATE for o, a in av)
            out.append(random.choice([c for c in ALNUM if c not in ch]) if neg else random.choice(ch))
        elif op == sre_c.BRANCH:
            out.append(gen(random.choice(av[1]), payload))
        elif op == sre_c.SUBPATTERN:
            out.append(payload if av[0] == 1 else gen(av[3], payload))
        elif op in (sre_c.MAX_REPEAT, sre_c.MIN_REPEAT):
            lo, hi, sub = av
            out.append("".join(gen(sub, payload) for _ in range(max(lo, 1) if hi else 0)))
        elif op == sre_c.AT:
            if av in (sre_c.AT_BEGINNING, sre_c.AT_BEGINNING_STRING):
                raise ValueError("anchored")
        else:
            raise ValueError(op)
    return "".join(out)


rx_made = rx_skipped = 0
for line, pattern, steps in regex_rules:
    steps = [t for t in steps if t != "-blocked"]
    rx = steps[0]
    if not rx.startswith("/") or any(t.startswith("/") for t in steps[1:]):
        rx_skipped += 1
        continue
    try:
        parsed = sre_parse.parse(rx[1:-1])
        cre = re.compile(rx[1:-1])
    except Exception:
        rx_skipped += 1
        continue
    # the payload the regex extracts, from the destination through the later steps backwards
    dest = "https://dest%d.example.net/landing/page?a=1&b=two" % random.randint(1, 999)
    cur = dest
    ok = True
    for step in reversed(steps[1:]):
        if step == "+https":
            cur = "http://" + cur[len("https://"):] if cur.startswith("https://") else None
        elif step == "-base64":
            cur = b64(cur, False)
        elif step == "-safebase64":
            cur = b64(cur, True)
        elif step == "-uricomponent":
            cur = quote(cur, safe="")
        else:
            cur = None
        if cur is None:
            ok = False
            break
    if not ok:
        rx_skipped += 1
        continue
    made_one = False
    for attempt in range(30):
        try:
            text = gen(parsed, cur)
        except ValueError:
            break
        base = materialize(pattern)
        url = "https://" + base + ("" if text[:1] in ("/", "?", "&", "#") or base.endswith(("/", "?", "&")) else "/") + text
        m = cre.search(url)
        if m and m.group(1) == cur and re.match(r"https://[a-z0-9.-]+(/|\?|#|$)", url):
            rows.append((url, dest, line))
            rows.append((url.replace(re.match(r"https://([^/?#]+)", url).group(1), "unrelated-host.example", 1), "-", line))
            made_one = True
            rx_made += 1
            break
    if not made_one:
        rx_skipped += 1
random.shuffle(rows)
open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join("\t".join(r) for r in rows) + "\n")
print("regex-step rules:", len(regex_rules), "| addresses made:", rx_made, "| not invertible:", rx_skipped, "| total rows:", len(rows))

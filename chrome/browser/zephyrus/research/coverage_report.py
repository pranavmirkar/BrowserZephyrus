#!/usr/bin/env python3
"""Turns the coverage audit (AdblockCoverageAudit, a .tsv) into the list of rules that do nothing.

  coverage_report.py <coverage.tsv> <adblock_scriptlet_engine.cc> <adblock_redirect_resources_data.inc> [top-1m.csv]

Three ways a rule does nothing without anything saying so:
  dropped     no engine took the line
  no-op       taken, but it names a scriptlet the library does not have, or a stand-in we do not ship
  partial     taken, but it passes option words the scriptlet never reads
Each is ranked by the most popular site it is written for (Tranco rank), because that is who notices.
"""
import collections
import csv
import re
import sys

tsv, engine_cc, resources_inc = sys.argv[1:4]
tranco = sys.argv[4] if len(sys.argv) > 4 else None

rank = {}
if tranco:
    for row in csv.reader(open(tranco, encoding="utf-8")):
        if len(row) >= 2 and len(rank) < 200000:
            rank[row[1].lower()] = int(row[0])


def best_rank(domains):
    best = None
    for d in domains:
        d = d.strip().lstrip("~").lower()
        if not d or d.startswith("/"):
            continue
        d = d.replace(".*", ".com")
        parts = d.split(".")
        for i in range(len(parts) - 1):
            r = rank.get(".".join(parts[i:]))
            if r and (best is None or r < best):
                best = r
    return best


def domains_of(line):
    m = re.match(r"^([^#$]*?)#[@?$%]*#", line)
    if m:
        return [d for d in m.group(1).split(",") if d and not d.startswith("~")]
    m = re.search(r"\$(?:.*,)?domain=([^,]+)", line)
    if m:
        return [d for d in m.group(1).split("|") if not d.startswith("~")]
    m = re.match(r"^@?@?\|\|([^/^$*]+)", line)
    return [m.group(1)] if m else []


cc = open(engine_cc, encoding="utf-8").read()
table = cc[cc.index("var table = {"):]
table = table[:table.index("return table;")]
have = set(re.findall(r"^\s*'([^']+)'\s*:", table, re.M))
resources = set(re.findall(r'\{\d+, "([^"]+)"', open(resources_inc, encoding="utf-8").read()))
resources |= set(re.findall(r'\{"([^"]+)", \d+\}', open(resources_inc, encoding="utf-8").read()))

# Option words each scriptlet reads, found in the library: the literal strings it
# compares its arguments with. Coarse on purpose: a word that appears anywhere in
# the library counts as read.
read_words = set(re.findall(r"'([a-zA-Z][a-zA-Z0-9-]{2,})'", cc[cc.index("kScriptletLibrary"):cc.index(')JS";')]))
OPTION_WORDS = {"stay", "quitAfter", "runAt", "condition", "sedCount", "excludes", "propsToMatch",
                "extraMatch", "delay", "log", "debug", "asap", "throttle", "reload", "uncomment",
                "shadow", "observer", "clickDelay", "dontOverwrite"}

problems = []  # (kind, what, rank, is_exception, line)
for raw in open(tsv, encoding="utf-8"):
    who, trusted, line = raw.rstrip("\n").split("\t", 2)
    exception = line.startswith("@@") or "#@" in line
    r = best_rank(domains_of(line))
    if who == "-":
        why = "?"
        if re.search(r"#@?[?$%]*#\+js\(\s*(trusted-[a-z-]+)", line) and trusted != "trusted":
            why = "trusted scriptlet outside a trusted section"
        elif "##^" in line or "#@#^" in line:
            why = "##^ (HTML filtering)"
        elif re.search(r"#@?\$\??#", line):
            why = "#$# (CSS injection)"
        elif re.search(r"#@?\?#", line):
            ops = re.findall(r":(-abp-[a-z-]+|[a-z-]+)\(", line)
            why = "procedural " + (",".join(sorted(set(ops))) or "?")
        else:
            m = re.search(r"\$([^/]*)$", line)
            if m:
                opts = [o.split("=")[0].lstrip("~") for o in m.group(1).split(",") if o]
                why = "option " + ",".join(o for o in opts if o not in ("domain", "third-party", "3p", "first-party", "1p", "script", "image", "xhr", "important", "all", "subdocument", "frame", "stylesheet", "media", "font", "ping", "other", "document", "popup")) or "option ?"
            elif line.startswith("/") and line.endswith("/"):
                why = "regex"
        problems.append(("dropped", why, r, exception, line))
        continue
    m = re.search(r"#@?[?$%]*#\+js\(([^)]*)\)?", line)
    if m and "scriptlet" in who:
        args = [a.strip() for a in re.split(r"(?<!\\),", m.group(1))]
        name = args[0].strip("'\"")
        if name and name not in have and name.replace(".js", "") not in have:
            problems.append(("no-op", "scriptlet " + name, r, exception, line))
        else:
            unread = [a for a in args[1:] if a in OPTION_WORDS and a not in read_words]
            if unread:
                problems.append(("partial", "%s ignores %s" % (name, ",".join(sorted(set(unread)))), r, exception, line))
    for rd in re.findall(r"redirect(?:-rule)?=([^,:$]+)", line):
        if rd not in resources and rd != "none":
            problems.append(("no-op", "stand-in " + rd, r, exception, line))

by = collections.defaultdict(list)
for p in problems:
    by[(p[0], p[1])].append(p)
print("rule lines that do nothing or only part of what they say: %d\n" % len(problems))
print("%-8s %-58s %6s %6s %8s  %s" % ("kind", "what", "rules", "exc", "top rank", "example (most popular site)"))
rows = []
for (kind, what), ps in by.items():
    ranks = [p[2] for p in ps if p[2]]
    top = min(ranks) if ranks else None
    best = min(ps, key=lambda p: p[2] or 10**9)
    rows.append((top or 10**9, kind, what, len(ps), sum(1 for p in ps if p[3]), best[4]))
rows.sort(key=lambda r: (r[0], -r[3]))
for top, kind, what, n, exc, ex in rows[:70]:
    print("%-8s %-58s %6d %6d %8s  %s" % (kind, what[:58], n, exc, top if top < 10**9 else "-", ex[:110]))

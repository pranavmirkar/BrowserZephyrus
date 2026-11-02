"""Explain the differences between Zephyrus (index + regex) and Brave on the regex corpus.

For each request the two disagree on, list the regex rules whose regex (per Python `re`,
honouring $match-case) matches the URL, and bucket the disagreement by the options of
the rule that explains it.   Run: python rx_diff.py [zeph_file]
"""
import re, sys, collections

RUN = r"D:\zeph_perf\h2h_rx_run"
CORPUS = r"D:\zeph_perf\h2h_rx\rx_corpus.tsv"
LIST = r"D:\zeph_perf\abc_prof_on\ZephyrusAdBlock\filters.txt"
zfile = sys.argv[1] if len(sys.argv) > 1 else "zeph_index_regex.txt"

rows = [l.rstrip("\n").split("\t") for l in open(CORPUS, encoding="utf-8")]
z = [l.strip() for l in open(RUN + "\\" + zfile)]
b = [l.strip() for l in open(RUN + r"\brave.txt")]
assert len(rows) == len(z) == len(b), (len(rows), len(z), len(b))

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
    names = [o.split("=")[0] for o in opts.split(",") if o]
    flags = 0 if "match-case" in names else re.I
    try:
        c = re.compile(rx, flags)
    except re.error:
        continue
    rules.append((line, c, names, line.startswith("@@")))

only_z = [i for i in range(len(rows)) if z[i] == "1" and b[i] == "0"]
only_b = [i for i in range(len(rows)) if z[i] == "0" and b[i] == "1"]
print(f"requests {len(rows)} | both block {sum(1 for i in range(len(rows)) if z[i]=='1' and b[i]=='1')} | "
      f"zephyrus only {len(only_z)} | brave only {len(only_b)}")


def explain(idx):
    url, page, typ = rows[idx]
    hits = [r for r in rules if r[1].search(url)]
    return hits


def bucket(name, idxs, show=6):
    cat = collections.Counter()
    ex = collections.defaultdict(list)
    for i in idxs:
        hits = explain(i)
        key = "no python-regex rule matches"
        if hits:
            # the option tokens of the matching rules, minus the ones every rule has
            toks = set()
            for line, c, names, exc in hits:
                toks.update(n for n in names if n in ("to", "match-case", "important", "domain", "from", "3p", "1p",
                                                       "third-party", "first-party", "doc", "document", "popup",
                                                       "frame", "script", "xhr", "image", "all"))
                if exc:
                    toks.add("@@")
            key = ",".join(sorted(toks)) or "(no options)"
        cat[key] += 1
        if len(ex[key]) < 2:
            ex[key].append((rows[i], [h[0][:130] for h in hits[:2]]))
    print(f"\n== {name}: {len(idxs)} ==")
    for key, n in cat.most_common(12):
        print(f"  {n:4d}  {key}")
    for key, n in cat.most_common(show):
        for (u, p, t), lines in ex[key][:1]:
            print(f"     e.g. [{key}] {t} {u[:110]}  page={p}")
            for l in lines:
                print(f"          rule: {l}")


bucket("Zephyrus blocks, Brave does not", only_z)
bucket("Brave blocks, Zephyrus does not", only_b)

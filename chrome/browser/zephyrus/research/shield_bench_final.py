#!/usr/bin/env python3
"""The numbers a claim can stand on: per feed, on credential traps, with 95% intervals.

  shield_bench_final.py <results.jsonl> <content.json> [more results.jsonl content.json pairs]

Only the first probe of each link is used (the moment it is first seen). "Zephyrus" is Cloudflare or Quad9,
the two resolvers Shield DNS asks; "Brave" is Google's Safe Browsing lists as Brave holds them. A lead is
called only when the two 95% intervals do not overlap. A credential trap is a live page with a password
field on a host that is not popular: no blocklist had to say it is bad. urlscan.io links are listed but not
counted as ground truth (most of what it tags is not phishing).
"""
import collections
import json
import math
import sys


def wilson(k, n, z=1.96):
    if n == 0:
        return 0.0, 0.0
    p = k / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    a = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return 100 * (c - a) / d, 100 * (c + a) / d


def main():
    rows, info = [], {}
    args = sys.argv[1:]
    for i in range(0, len(args), 2):
        got = [json.loads(l) for l in open(args[i])]
        first = {}
        for r in got:
            first.setdefault(r["url"], r)
            if r["t"] < first[r["url"]]["t"]:
                first[r["url"]] = r
        rows += [r for r in first.values() if r["alive"]]
        info.update(json.load(open(args[i + 1])))
    tools = [("Brave (Google lists)", lambda r: r["brave"]),
             ("Cloudflare", lambda r: r["cf"]),
             ("Quad9", lambda r: r["quad9"]),
             ("Zephyrus (CF or Quad9)", lambda r: r["cf"] or r["quad9"])]

    def table(title, sel):
        n = len(sel)
        print("\n%s (n=%d)" % (title, n))
        res = {}
        for name, f in tools:
            k = sum(1 for r in sel if f(r))
            lo, hi = wilson(k, n)
            res[name] = (k, lo, hi)
            print("  %-24s %4d/%-4d %5.1f%%   95%% CI %4.1f-%4.1f%%" % (name, k, n, 100.0 * k / max(1, n), lo, hi))
        b, z = res["Brave (Google lists)"], res["Zephyrus (CF or Quad9)"]
        verdict = ("Zephyrus ahead" if z[1] > b[2] else "Brave ahead" if b[1] > z[2] else "no clear difference")
        print("  -> %s" % verdict)

    for src in sorted({r["source"] for r in rows}):
        table("feed: " + src, [r for r in rows if r["source"] == src])
    traps = [r for r in rows if info.get(r["url"], {}).get("password") and r["source"] != "urlscan"]
    table("credential traps from OpenPhish + PhishTank (password field, independent of any list)", traps)
    traps_all = [r for r in rows if info.get(r["url"], {}).get("password")]
    table("credential traps from every feed incl. urlscan", traps_all)
    print("\ncounts:", dict(collections.Counter(r["source"] for r in rows)))


if __name__ == "__main__":
    main()

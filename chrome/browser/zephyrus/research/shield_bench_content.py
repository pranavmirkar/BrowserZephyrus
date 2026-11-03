#!/usr/bin/env python3
"""Ground truth from the page itself, not from any blocklist.

  shield_bench_content.py <results.jsonl> <out.json>

Takes the first probe's live hosts, fetches each page with a plain GET (nothing runs, the HTML is only
parsed) and marks it a CREDENTIAL TRAP when it has a password field. A password form on a host that is not
a popular site is the thing phishing is for; a feed or a service that merely lists it is not what makes it
one. Recall is then reported per feed on that subset, so a service that reads one feed shows up as strong
only on that feed.
"""
import collections
import concurrent.futures
import json
import sys

import sb_free_signals as fs


def main():
    results, out = sys.argv[1:3]
    rows = [json.loads(l) for l in open(results)]
    first = min(r["t"] for r in rows)
    rows = [r for r in rows if r["t"] == first and r["alive"]]

    def one(r):
        page = fs.fetch_page(r["url"])
        return r["url"], {"html": bool(page.get("html")), "password": bool(page.get("password")),
                          "login": bool(page.get("login")), "title": page.get("title", "")}

    info = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=24) as ex:
        for url, p in ex.map(one, rows):
            info[url] = p
    json.dump(info, open(out, "w"))
    names = ["brave", "cf", "quad9", "clean"]
    for label, pick in [("all live", lambda r: True),
                        ("page fetched as HTML", lambda r: info[r["url"]]["html"]),
                        ("CREDENTIAL TRAP (password field)", lambda r: info[r["url"]]["password"])]:
        print("\n== %s" % label)
        for src in sorted({r["source"] for r in rows}) + ["ALL"]:
            s = [r for r in rows if (src == "ALL" or r["source"] == src) and pick(r)]
            n = len(s)
            if not n:
                print("  %-10s n=0" % src)
                continue
            parts = ["%s %5.1f%%" % (nm, 100.0 * sum(1 for r in s if r[nm]) / n) for nm in names]
            both = "Google or CF or Quad9 %5.1f%%" % (100.0 * sum(1 for r in s if r["brave"] or r["cf"] or r["quad9"]) / n)
            print("  %-10s n=%-4d %s | %s" % (src, n, " | ".join(parts), both))


if __name__ == "__main__":
    main()

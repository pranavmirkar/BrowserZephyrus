#!/usr/bin/env python3
"""Fresh phishing from one feed, tested against the other feeds and against Brave.

Usage: sb_leave_one_out.py <brave Safe Browsing dir> <feed dir>

OpenPhish's community feed is a few hundred URLs seen in the last hours. PhishTank's
verified list is much larger but mostly stale. Each is a held-out test for an index
built from the others.
"""
import csv
import datetime
import os
import sys

import sb_coverage as sb


def index_from_urls(urls):
    entries = set()
    for u in urls:
        ex = sb.expressions(u)
        if ex:
            entries.add(sb.hash64(ex[0]))
    return entries


def pct(x, n):
    return "%5.1f%%" % (100.0 * x / max(n, 1))


def main():
    sbdir, feeddir = sys.argv[1:3]
    prefixes, _ = sb.load_brave(sbdir)
    pt_rows = list(csv.DictReader(open(os.path.join(
        feeddir, "https___data_phishtank_com_data_online_valid_csv.txt"),
        encoding="utf-8", errors="replace")))
    now = datetime.datetime.now(datetime.timezone.utc)
    pt_fresh, pt_all = [], []
    for r in pt_rows:
        try:
            t = datetime.datetime.fromisoformat(r["submission_time"])
        except ValueError:
            continue
        pt_all.append(r["url"])
        if (now - t).days < 7:
            pt_fresh.append(r["url"])
    op = list(sb.read_lines(os.path.join(feeddir, "https___openphish_com_feed_txt.txt")))
    print("OpenPhish fresh: %d URLs; PhishTank <7 days: %d; PhishTank all: %d" %
          (len(op), len(pt_fresh), len(pt_all)))

    open_pdb, _, _ = sb.build_open_index(feeddir)
    pt_index = index_from_urls(pt_all)
    op_index = index_from_urls(op)
    floor = 0.0290  # measured by sb_coverage.py on 7,512 real benign URLs

    def row(title, urls, ours):
        b = sum(sb.brave_listed(prefixes, urls))
        n = len(urls)
        print("\n%s (%d URLs)" % (title, n))
        print("  Brave / Google lists   raw %s   corrected for chance hits %s" %
              (pct(b, n), pct(max(0, b - floor * n), n * (1 - floor))))
        for name, entries in ours:
            o = sum(sb.open_listed(entries, urls))
            both = sum(x or y for x, y in zip(sb.brave_listed(prefixes, urls),
                                             sb.open_listed(entries, urls)))
            print("  %-34s %s   (Brave or this: %s)" % (name, pct(o, n), pct(both, n)))

    row("OpenPhish, last hours", op,
        [("index of Phishing.Database + blocklist", open_pdb),
         ("index of PhishTank (all verified)", pt_index),
         ("index of both", open_pdb | pt_index)])
    row("PhishTank, submitted < 7 days ago", pt_fresh,
        [("index of Phishing.Database + blocklist", open_pdb),
         ("index of OpenPhish", op_index),
         ("index of both", open_pdb | op_index)])


if __name__ == "__main__":
    main()

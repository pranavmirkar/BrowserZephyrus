#!/usr/bin/env python3
"""Can a purely local check of the HOSTNAME find phishing that Google's list does not yet hold?

The check: the hostname uses a well-known brand's name but is not that brand's own
site (paypal-secure-login.example.top). Nothing is sent anywhere and it needs no list
update. It is measured on PhishTank's verified phishing (recall, and its overlap with
what Brave's Safe Browsing lists already hold) and on the Tranco top million (false
positives).

Usage: sb_heuristics.py <phishtank csv> <brave Safe Browsing dir> <tranco top-1m.csv> <public_suffix_list.dat> [benign.tsv]
"""
import collections
import csv
import datetime
import re
import sys

import sb_coverage as sb


def load_psl(path):
    rules, wild, exc = set(), set(), set()
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        if line.startswith("!"):
            exc.add(line[1:])
        elif line.startswith("*."):
            wild.add(line[2:])
        else:
            rules.add(line)
    return rules, wild, exc


class Psl:
    def __init__(self, path):
        self.rules, self.wild, self.exc = load_psl(path)

    def reg_domain(self, host):
        labels = host.split(".")
        n = len(labels)
        suffix_len = 1
        for i in range(n):
            cand = ".".join(labels[i:])
            parent = ".".join(labels[i + 1:])
            if cand in self.exc:
                suffix_len = n - i - 1
                break
            if cand in self.rules or (parent and parent in self.wild):
                suffix_len = n - i
                break
        if n <= suffix_len:
            return host
        return ".".join(labels[n - suffix_len - 1:])


def brand_keyword(target):
    k = re.sub(r"[^a-z0-9]", "", target.lower())
    return k


def host_of(url):
    c = sb.canonical(url)
    return c[0] if c else None


def main():
    phishtank, sbdir, tranco, pslpath = sys.argv[1:5]
    benign_path = sys.argv[5] if len(sys.argv) > 5 else None
    psl = Psl(pslpath)
    prefixes, _ = sb.load_brave(sbdir)

    rows = list(csv.DictReader(open(phishtank, encoding="utf-8", errors="replace")))
    counts = collections.Counter(r["target"] for r in rows)
    brands = [t for t, c in counts.most_common(120) if t != "Other" and c >= 25]
    keywords = {}
    for t in brands:
        k = brand_keyword(t)
        if len(k) >= 4:
            keywords[k] = t
    print("brands from PhishTank targets (>=25 reports):", len(keywords))

    popular = {}
    with open(tranco, encoding="utf-8") as f:
        for line in f:
            rank, dom = line.strip().split(",", 1)
            popular[dom] = int(rank)
    top100k = {d for d, r in popular.items() if r <= 100000}
    legit = collections.defaultdict(set)
    for d in top100k:
        label = d.split(".")[0]
        for k in keywords:
            if k in label:
                legit[k].add(d)
    print("legitimate brand domains found in the Tranco top 100k:",
          sum(len(v) for v in legit.values()))

    def matches(host):
        """The brand keyword whose name this host borrows, or None."""
        reg = psl.reg_domain(host)
        if reg in top100k:
            return None  # an established site; phishers use throwaway domains
        tokens = re.split(r"[.\-_]|(?<=[a-z])(?=\d)|(?<=\d)(?=[a-z])", host)
        for k in keywords:
            if reg in legit[k]:
                continue
            for tok in tokens:
                if tok == k or (len(k) >= 6 and k in tok):
                    return k
        return None

    now = datetime.datetime.now(datetime.timezone.utc)
    items = []
    for r in rows:
        if r.get("verified") != "yes" or r.get("online") != "yes":
            continue
        try:
            t = datetime.datetime.fromisoformat(r["submission_time"])
        except ValueError:
            continue
        h = host_of(r["url"])
        if h:
            items.append((r["url"], h, (now - t).days, r["target"]))
    brave = sb.brave_listed(prefixes, [i[0] for i in items])

    floor = 0.029
    for label, lo, hi in [("submitted < 30 days ago", 0, 30), ("older (mostly dead links)", 30, 10 ** 6)]:
        sel = [(i, b) for i, b in zip(items, brave) if lo <= i[2] < hi]
        n = len(sel)
        h_hit = [matches(i[1]) is not None for i, _ in sel]
        bb = [b for _, b in sel]
        n_h = sum(h_hit)
        n_b = sum(bb)
        n_hb = sum(1 for x, y in zip(h_hit, bb) if x and y)
        n_either = sum(1 for x, y in zip(h_hit, bb) if x or y)
        n_only = sum(1 for x, y in zip(h_hit, bb) if x and not y)
        print("\nPhishTank verified, %s: %d URLs" % (label, n))
        print("  Brave / Google lists (raw)         %5.1f%%" % (100 * n_b / n))
        print("  hostname-borrows-a-brand check     %5.1f%%   of which Google already holds %d, NEW %d" %
              (100 * n_h / n, n_hb, n_only))
        print("  either                             %5.1f%%   (adds %.1f points to Google alone)" %
              (100 * n_either / n, 100 * (n_either - n_b) / n))

    # false positives: the long tail of the Tranco top million, and real traffic
    tail = [d for d, r in popular.items() if r > 100000]
    flagged = [(d, matches(d)) for d in tail]
    flagged = [(d, k) for d, k in flagged if k]
    print("\nfalse positives on the Tranco ranks 100,001 to 1,000,000: %d of %d (%.4f%%)" %
          (len(flagged), len(tail), 100.0 * len(flagged) / len(tail)))
    print("  examples:", ", ".join("%s (%s)" % x for x in flagged[:12]))
    if benign_path:
        urls = [l.split("\t")[0] for l in sb.read_lines(benign_path)]
        hosts = {host_of(u) for u in urls if host_of(u)}
        bad = [h for h in hosts if matches(h)]
        print("false positives on %d distinct hosts of real browsing: %d %s" % (len(hosts), len(bad), bad[:5]))
    by_brand = collections.Counter(matches(i[1]) for i in items if i[2] < 30 and matches(i[1]))
    print("brands behind the < 30 day hits:", by_brand.most_common(8))


if __name__ == "__main__":
    main()

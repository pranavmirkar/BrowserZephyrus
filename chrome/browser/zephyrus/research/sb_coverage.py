#!/usr/bin/env python3
"""How much of today's phishing does Brave's Safe Browsing list already hold, and
how much would a local index built from open feeds hold?

Brave (desktop) checks URLs against Google's Safe Browsing lists, which it keeps as
sorted 4-byte SHA-256 prefixes under "User Data/Safe Browsing/*.store.4_<stamp>".
Those files are read here directly: no page is loaded and nothing is sent anywhere.

  sb_coverage.py <phishtank online-valid.csv> <brave Safe Browsing dir> <feed dir> [benign.tsv]

A URL is "listed" when ANY of its Safe Browsing expressions (host suffixes x path
prefixes, at most 30) has its prefix in the list. An unlisted URL still hits by
chance about 30 x (entries / 2^32) of the time, so a control set of benign URLs
measures that noise floor and the results are corrected by it.

The test set is PhishTank's verified, still-online phishing, which is NOT an input to
the open-feed index, so "ours" is held out. Results are bucketed by how long ago the
URL was submitted: that is where a list's lag shows.
"""
import bisect
import collections
import csv
import datetime
import glob
import hashlib
import os
import re
import sys
from urllib.parse import unquote_to_bytes

import numpy as np


# --- Safe Browsing URL canonicalisation and expressions (developers.google.com/safe-browsing/v4/urls-hashing)

def _escape(b: bytes) -> str:
    out = []
    for c in b:
        if c <= 0x20 or c >= 0x7F or c in (0x23, 0x25):
            out.append("%%%02X" % c)
        else:
            out.append(chr(c))
    return "".join(out)


def _unescape_fully(s: str) -> bytes:
    b = s.encode("utf-8", "replace")
    while True:
        n = unquote_to_bytes(b)
        if n == b:
            return b
        b = n


def canonical(url: str):
    url = url.strip().replace("\t", "").replace("\r", "").replace("\n", "")
    url = url.split("#", 1)[0]
    if "://" in url:
        url = url.split("://", 1)[1]
    m = re.match(r"([^/?]*)(.*)", url, re.S)
    authority, tail = m.group(1), m.group(2)
    if "@" in authority:
        authority = authority.rsplit("@", 1)[1]
    authority = re.sub(r":\d*$", "", authority)
    host = _unescape_fully(authority).decode("utf-8", "replace").strip(".").lower()
    host = re.sub(r"\.+", ".", host)
    if not host:
        return None
    if "?" in tail:
        path, query = tail.split("?", 1)
    else:
        path, query = tail, None
    path_b = _unescape_fully(path).decode("utf-8", "replace")
    trailing = path_b.endswith("/") or path_b.endswith("/.") or path_b.endswith("/..")
    parts = []
    for seg in path_b.split("/"):
        if seg in ("", "."):
            continue
        if seg == "..":
            if parts:
                parts.pop()
            continue
        parts.append(seg)
    path_c = "/" + "/".join(parts) + ("/" if (trailing and parts) else "")
    path_c = _escape(path_c.encode("utf-8", "replace"))
    query_c = None if query is None else _escape(_unescape_fully(query))
    return host, path_c, query_c


def expressions(url: str):
    c = canonical(url)
    if c is None:
        return []
    host, path, query = c
    hosts = [host]
    if not re.fullmatch(r"\d+\.\d+\.\d+\.\d+", host):
        comps = host.split(".")
        for i in range(max(1, len(comps) - 5), len(comps) - 1):
            h = ".".join(comps[i:])
            if h not in hosts:
                hosts.append(h)
    paths = []
    if query is not None:
        paths.append(path + "?" + query)
    paths.append(path)
    paths.append("/")
    cur = "/"
    for d in [p for p in path.split("/")[1:-1]][:4]:
        cur += d + "/"
        paths.append(cur)
    seen, out = set(), []
    for h in hosts:
        for p in paths:
            e = h + p
            if e not in seen:
                seen.add(e)
                out.append(e)
    return out


def prefix4(expr: str) -> int:
    return int.from_bytes(hashlib.sha256(expr.encode("utf-8", "replace")).digest()[:4], "big")


def hash64(expr: str) -> int:
    return int.from_bytes(hashlib.sha256(expr.encode("utf-8", "replace")).digest()[:8], "big")


# --- Brave's local lists

def load_brave(sbdir):
    arrays, names = [], []
    for f in sorted(glob.glob(os.path.join(sbdir, "*.store.4_*"))):
        a = np.fromfile(f, dtype=">u4")
        arrays.append(a.astype(np.uint32))
        names.append((os.path.basename(f).split(".")[0], len(a)))
    allp = np.unique(np.concatenate(arrays))
    return allp, names


def brave_listed(prefixes, urls):
    hits = []
    for u in urls:
        ex = [prefix4(e) for e in expressions(u)]
        if not ex:
            hits.append(False)
            continue
        arr = np.array(ex, dtype=np.uint32)
        idx = np.searchsorted(prefixes, arr)
        idx[idx >= len(prefixes)] = len(prefixes) - 1
        hits.append(bool((prefixes[idx] == arr).any()))
    return hits


# --- the open-feed index (what a ThreatIndex would hold)

def read_lines(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                yield line


def build_open_index(feeddir):
    """64-bit hashes of: the full expression of every listed URL, and 'host/' of every
    listed domain (which matches that host and every subdomain of it)."""
    entries = set()
    n_urls = n_domains = 0
    for name in ("pdb_phishing-links-ACTIVE.txt", "pdb_phishing-links-NEW-today.txt"):
        p = os.path.join(feeddir, name)
        if os.path.exists(p):
            for line in read_lines(p):
                ex = expressions(line)
                if ex:
                    # the exact-URL expression is the first host with the longest path
                    entries.add(hash64(ex[0]))
                    n_urls += 1
    for name in ("pdb_phishing-domains-ACTIVE.txt", "pdb_phishing-domains-NEW-today.txt"):
        p = os.path.join(feeddir, name)
        if os.path.exists(p):
            for line in read_lines(p):
                c = canonical(line)
                if c:
                    entries.add(hash64(c[0] + "/"))
                    n_domains += 1
    p = os.path.join(feeddir, "https___raw_githubusercontent_com_blocklistproject_Lists_mas.txt")
    if os.path.exists(p):
        for line in read_lines(p):
            parts = line.split()
            host = parts[-1] if parts else ""
            c = canonical(host)
            if c:
                entries.add(hash64(c[0] + "/"))
                n_domains += 1
    return entries, n_urls, n_domains


def open_listed(entries, urls):
    return [any(hash64(e) in entries for e in expressions(u)) for u in urls]


# --- main

def main():
    phishtank, sbdir, feeddir = sys.argv[1:4]
    benign_path = sys.argv[4] if len(sys.argv) > 4 else None
    prefixes, names = load_brave(sbdir)
    print("Brave local Safe Browsing lists:", ", ".join("%s %d" % n for n in names),
          "-> %d distinct 4-byte prefixes" % len(prefixes))
    entries, nu, nd = build_open_index(feeddir)
    print("open-feed index: %d entries (%d URLs, %d domains read)" % (len(entries), nu, nd))

    rows = list(csv.DictReader(open(phishtank, encoding="utf-8", errors="replace")))
    now = datetime.datetime.now(datetime.timezone.utc)
    urls, ages = [], []
    for r in rows:
        if r.get("verified") != "yes" or r.get("online") != "yes":
            continue
        try:
            t = datetime.datetime.fromisoformat(r["submission_time"])
        except ValueError:
            continue
        urls.append(r["url"])
        ages.append((now - t).total_seconds() / 3600.0)
    print("PhishTank verified+online URLs:", len(urls))

    b = brave_listed(prefixes, urls)
    o = open_listed(entries, urls)

    # control: benign URLs, for the chance-hit floor of each
    floor_b = floor_o = 0.0
    if benign_path:
        benign = []
        for line in read_lines(benign_path):
            benign.append(line.split("\t")[0])
        benign = list(dict.fromkeys(benign))
        bb = brave_listed(prefixes, benign)
        bo = open_listed(entries, benign)
        floor_b = sum(bb) / len(benign)
        floor_o = sum(bo) / len(benign)
        print("control (%d real benign URLs): Brave prefix hits %.2f%%, open index hits %.2f%%"
              % (len(benign), 100 * floor_b, 100 * floor_o))

    buckets = [("< 6 h", 0, 6), ("6-24 h", 6, 24), ("1-3 days", 24, 72), ("3-7 days", 72, 168),
               ("7-30 days", 168, 720), ("> 30 days", 720, 1e9)]
    print("\n%-10s %7s | %-18s %-18s %-18s" % ("age", "URLs", "Brave (Google)", "open feeds", "either"))
    tot = collections.Counter()
    for label, lo, hi in buckets:
        idx = [i for i, a in enumerate(ages) if lo <= a < hi]
        if not idx:
            continue
        n = len(idx)
        cb = sum(b[i] for i in idx)
        co = sum(o[i] for i in idx)
        ce = sum(b[i] or o[i] for i in idx)

        def corr(c, floor):
            raw = c / n
            return max(0.0, (raw - floor) / (1 - floor))
        print("%-10s %7d | %6.1f%% (raw %5.1f%%) %6.1f%% (raw %5.1f%%) %6.1f%%"
              % (label, n, 100 * corr(cb, floor_b), 100 * cb / n, 100 * corr(co, floor_o),
                 100 * co / n, 100 * corr(ce, max(floor_b, floor_o))))
        tot["n"] += n
        tot["b"] += cb
        tot["o"] += co
        tot["e"] += ce
    n = tot["n"]
    print("%-10s %7d | %6.1f%% (raw %5.1f%%) %6.1f%% (raw %5.1f%%) %6.1f%%"
          % ("all", n, 100 * max(0, (tot["b"] / n - floor_b) / (1 - floor_b)), 100 * tot["b"] / n,
             100 * max(0, (tot["o"] / n - floor_o) / (1 - floor_o)), 100 * tot["o"] / n,
             100 * max(0, (tot["e"] / n - max(floor_b, floor_o)) / (1 - max(floor_b, floor_o)))))


if __name__ == "__main__":
    main()

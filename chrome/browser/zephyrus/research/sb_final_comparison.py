#!/usr/bin/env python3
"""The shipped configuration against Brave, on the same live fresh phishing.

Uses the 550 phishing URLs sb_free_signals.py collected (PhishTank verified < 3 days, plus
OpenPhish's last hours) and keeps those whose host still resolves. For each, as the browser
would decide:
  brave     Google's lists as Brave holds them (prefix match)
  local     the shipped threat index (Phishing.Database + Blocklist Project)
  shield    Cloudflare's security resolver blocks it AND the browser would ask: not on the
            popular-sites list, a public name
  zephyrus  local, or shield when Shield DNS is on
Also: how many hostnames of real browsing Shield DNS would send out.

Usage: sb_final_comparison.py <free_signals.json> <brave Safe Browsing dir> <feed dir>
                              <majestic csv> <psl> <benign corpus tsv>
"""
import json
import sys

import make_threat_blob as mk
import sb_coverage as sb
import sb_free_signals as fs


def main():
    signals, sbdir, feeddir, majestic, psl, benign = sys.argv[1:7]
    rows = [r for r in json.load(open(signals)) if r["alive"]]
    prefixes, _ = sb.load_brave(sbdir)
    local_entries, _, _ = sb.build_open_index(feeddir)
    popular = mk.popular_expressions(majestic, psl, 100000)

    def is_popular(url):
        c = sb.canonical(url)
        if not c:
            return False
        host = c[0]
        parts = host.split(".")
        return any(".".join(parts[i:]) + "/" in popular for i in range(len(parts) - 1))

    def would_ask(url):
        c = sb.canonical(url)
        return bool(c) and fs.ShieldDnsChecker_eligible(c[0]) and not is_popular(url) if hasattr(
            fs, "ShieldDnsChecker_eligible") else bool(c) and "." in c[0] and not is_popular(url)

    n = len(rows)
    brave = [bool(x) for x in sb.brave_listed(prefixes, [r["url"] for r in rows])]
    local = sb.open_listed(local_entries, [r["url"] for r in rows])
    asked = [would_ask(r["url"]) for r in rows]
    shield = [bool(r.get("cf")) and a for r, a in zip(rows, asked)]
    shield_unskipped = [bool(r.get("cf")) for r in rows]
    zeph_default = local
    zeph_shield = [l or s for l, s in zip(local, shield)]
    both = [b or z for b, z in zip(brave, zeph_shield)]

    def pct(x):
        return "%5.1f%%  (%d of %d)" % (100.0 * sum(x) / n, sum(x), n)

    print("live fresh phishing URLs: %d (PhishTank < 3 days + OpenPhish last hours)\n" % n)
    print("Brave (Google's lists)                     ", pct(brave))
    print("Zephyrus default (local index only)        ", pct(zeph_default))
    print("Zephyrus + Shield DNS (shipped config)     ", pct(zeph_shield))
    print("   of which Shield DNS alone               ", pct(shield))
    print("   Shield DNS if popular sites were asked  ", pct(shield_unskipped))
    print("   phishing on a popular site: skipped     %d of %d" % (sum(1 for a in asked if not a), n))
    print("Brave and Zephyrus together (if both lists) ", pct(both))
    print("caught by Zephyrus Shield but not by Brave: %d;  by Brave but not Zephyrus: %d" % (
        sum(1 for b, z in zip(brave, zeph_shield) if z and not b),
        sum(1 for b, z in zip(brave, zeph_shield) if b and not z)))

    hosts = set()
    for line in sb.read_lines(benign):
        c = sb.canonical(line.split("\t")[0])
        if c:
            hosts.add(c[0])
    asked_hosts = [h for h in hosts if "." in h and not is_popular("http://" + h + "/")]
    print("\nreal browsing: %d distinct hosts, Shield DNS would ask about %d (%.1f%%)" % (
        len(hosts), len(asked_hosts), 100.0 * len(asked_hosts) / max(1, len(hosts))))
    print("  e.g.", ", ".join(sorted(asked_hosts)[:8]))


if __name__ == "__main__":
    main()

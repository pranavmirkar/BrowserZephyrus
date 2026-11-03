#!/usr/bin/env python3
"""A benchmark for phishing protection that is hard to flatter: several feeds, a time axis, and false positives.

  shield_bench.py collect <items.json> [max per feed]
      Fresh phishing links from independent feeds (PhishTank, OpenPhish, urlscan.io), at most 2 per
      registrable domain so one campaign cannot dominate, with when each was reported.
  shield_bench.py probe <items.json> <results.jsonl> <brave Safe Browsing dir>
      One probe at this moment: does the host still resolve; do Google's lists (as the given Brave
      profile holds them), Cloudflare's, Quad9's and CleanBrowsing's security resolvers block it?
      Run it again later with the same items: the rows carry a timestamp, so the second run
      measures how fast each service catches what it missed.
  shield_bench.py report <results.jsonl>
      Per feed and per probe time: how many each service blocks, and the unions.
  shield_bench.py fp <report.json> [n]
      Benign-looking domains (Majestic Million, popular and long-tail) through each security resolver:
      the share it blocks. An upper bound on false positives (some long-tail domains are really bad).

Why this exists: a single test on one feed can flatter a service that reads that feed. The honest number
is the lowest across feeds, with the time axis next to it.
"""
import collections
import concurrent.futures
import csv
import datetime
import json
import os
import random
import re
import subprocess
import sys
import time

import sb_coverage as sb
import sb_free_signals as fs
from sb_heuristics import Psl

PSL = "D:/zeph_perf/sb/psl.dat"
RESOLVERS = {"cf": ("1.1.1.2", "1.1.1.1"), "quad9": ("9.9.9.9", "9.9.9.10"),
             "clean": ("185.228.168.9", "1.1.1.1")}


def curl(url, timeout=60, ua="Mozilla/5.0"):
    r = subprocess.run(["curl", "-s", "-L", "-m", str(timeout), "-A", ua, url], capture_output=True)
    return r.stdout.decode("utf-8", "replace")


def collect(out, cap=400):
    psl = Psl(PSL)
    now = datetime.datetime.now(datetime.timezone.utc)
    items = []
    # PhishTank: submitted in the last 24 hours
    text = curl("https://data.phishtank.com/data/online-valid.csv", 120)
    for r in csv.DictReader(text.splitlines()):
        try:
            t = datetime.datetime.fromisoformat(r["submission_time"])
        except (ValueError, KeyError):
            continue
        if (now - t).total_seconds() < 24 * 3600:
            items.append({"url": r["url"], "source": "phishtank", "reported": t.isoformat()})
    # OpenPhish: its community feed is the last few hours
    for u in curl("https://openphish.com/feed.txt", 60).split():
        items.append({"url": u, "source": "openphish", "reported": now.isoformat()})
    # urlscan.io: scans tagged phishing in the last 24 hours (paged by its sort cursor)
    after = ""
    got = 0
    while got < cap * 2:
        q = "task.tags:phishing AND date:>now-24h"
        url = "https://urlscan.io/api/v1/search/?size=100&q=" + q.replace(" ", "%20") + after
        try:
            j = json.loads(curl(url, 60))
        except ValueError:
            break
        res = j.get("results", [])
        if not res:
            break
        for r in res:
            u = r.get("page", {}).get("url") or r.get("task", {}).get("url")
            if u:
                items.append({"url": u, "source": "urlscan", "reported": r.get("task", {}).get("time", now.isoformat())})
                got += 1
        last = res[-1].get("sort")
        if not last or len(res) < 100:
            break
        after = "&search_after=" + ",".join(str(x) for x in last)
        time.sleep(2)
    # at most 2 per registrable domain per source, cap per source
    seen = collections.Counter()
    per_source = collections.Counter()
    out_items = []
    random.seed(5)
    random.shuffle(items)
    for it in items:
        c = sb.canonical(it["url"])
        if not c:
            continue
        reg = psl.reg_domain(c[0])
        key = (it["source"], reg)
        if seen[key] >= 2 or per_source[it["source"]] >= cap:
            continue
        seen[key] += 1
        per_source[it["source"]] += 1
        it["host"] = c[0]
        out_items.append(it)
    json.dump(out_items, open(out, "w"))
    print("collected", dict(per_source), "from", len(items), "links")


def probe_one(it):
    host = it["host"]
    row = {"url": it["url"], "host": host, "source": it["source"]}
    alive = False
    for name, (filt, plain) in RESOLVERS.items():
        kind, a = fs.blocked_by(filt, plain, host)
        row[name] = kind
        alive = alive or a
    row["alive"] = bool(alive)
    return row


def probe(items_path, out_path, sbdir):
    items = json.load(open(items_path))
    prefixes, _ = sb.load_brave(sbdir)
    t0 = datetime.datetime.now(datetime.timezone.utc).isoformat()
    brave = sb.brave_listed(prefixes, [i["url"] for i in items])
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
        for it, row, b in zip(items, ex.map(probe_one, items), brave):
            row["brave"] = bool(b)
            row["t"] = t0
            row["reported"] = it["reported"]
            rows.append(row)
    sinks = collections.defaultdict(set)
    for r in rows:
        for name in RESOLVERS:
            k = r.get(name)
            if isinstance(k, str) and k.startswith("sink:"):
                sinks[(name, k)].add(r["host"])
    for r in rows:
        for name in RESOLVERS:
            k = r.get(name)
            if isinstance(k, str):
                r[name] = k in ("nx", "zero") or len(sinks[(name, k)]) >= 3
    with open(out_path, "a") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print("probed", len(rows), "at", t0, "| alive", sum(1 for r in rows if r["alive"]))


def report(path):
    rows = [json.loads(l) for l in open(path)]
    times = sorted({r["t"] for r in rows})
    names = ["brave", "cf", "quad9", "clean"]
    for t in times:
        sel = [r for r in rows if r["t"] == t and r["alive"]]
        print("\n=== probe at %s (%d live hosts)" % (t[:19], len(sel)))
        for src in sorted({r["source"] for r in sel}) + ["ALL"]:
            s = [r for r in sel if src == "ALL" or r["source"] == src]
            n = len(s)
            if not n:
                continue

            def pc(f):
                return "%5.1f%%" % (100.0 * sum(1 for r in s if f(r)) / n)
            print("  %-10s n=%-4d Google/Brave %s | Cloudflare %s | Quad9 %s | CleanBrowsing %s | CF or Quad9 %s | any resolver %s | Brave or any resolver %s" % (
                src, n, pc(lambda r: r["brave"]), pc(lambda r: r["cf"]), pc(lambda r: r["quad9"]),
                pc(lambda r: r["clean"]), pc(lambda r: r["cf"] or r["quad9"]),
                pc(lambda r: r["cf"] or r["quad9"] or r["clean"]),
                pc(lambda r: r["brave"] or r["cf"] or r["quad9"] or r["clean"])))
    if len(times) > 1:
        first = times[0]
        base = {r["url"]: r for r in rows if r["t"] == first}
        print("\n=== lag: of hosts NOT blocked at the first probe, how many each service caught by each later probe")
        for t in times[1:]:
            later = {r["url"]: r for r in rows if r["t"] == t}
            line = []
            for name in names:
                missed = [u for u, r in base.items() if r["alive"] and not r[name]]
                caught = [u for u in missed if u in later and later[u][name]]
                line.append("%s %d of %d (%.1f%%)" % (name, len(caught), len(missed), 100.0 * len(caught) / max(1, len(missed))))
            print("  by %s: %s" % (t[:16], " | ".join(line)))


def fp(path, n=4000):
    random.seed(9)
    pools = {"top 100k": [], "100k-1M": []}
    with open("D:/zeph_perf/sb/majestic.csv", encoding="utf-8") as f:
        f.readline()
        for line in f:
            c = line.split(",")
            rank = int(c[0])
            (pools["top 100k"] if rank <= 100000 else pools["100k-1M"]).append(c[2].lower())
    result = {}
    for label, pool in pools.items():
        sample = random.sample(pool, min(n, len(pool)))

        def one(d):
            out = {}
            for name, (filt, plain) in RESOLVERS.items():
                kind, alive = fs.blocked_by(filt, plain, d)
                out[name] = (kind, alive)
            return d, out
        res = collections.Counter()
        alive_n = collections.Counter()
        examples = collections.defaultdict(list)
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
            for d, out in ex.map(one, sample):
                for name, (kind, alive) in out.items():
                    if alive:
                        alive_n[name] += 1
                        if kind in ("nx", "zero"):
                            res[name] += 1
                            if len(examples[name]) < 6:
                                examples[name].append(d)
        result[label] = {name: {"blocked": res[name], "of": alive_n[name],
                                "pct": round(100.0 * res[name] / max(1, alive_n[name]), 3),
                                "examples": examples[name]} for name in RESOLVERS}
        print(label, {k: "%d/%d = %.2f%%" % (v["blocked"], v["of"], v["pct"]) for k, v in result[label].items()})
        for name in RESOLVERS:
            print("   ", name, "e.g.", examples[name])
    json.dump(result, open(path, "w"), indent=1)


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "collect":
        collect(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 400)
    elif cmd == "probe":
        probe(sys.argv[2], sys.argv[3], sys.argv[4])
    elif cmd == "report":
        report(sys.argv[2])
    elif cmd == "fp":
        fp(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 4000)

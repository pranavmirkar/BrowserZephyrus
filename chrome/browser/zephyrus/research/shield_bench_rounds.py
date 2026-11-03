#!/usr/bin/env python3
"""Grows the benchmark's sample over days: fresh links every few hours, each probed the moment it is collected.

  shield_bench_rounds.py <dir> <Brave profile dir> [rounds=12] [hours between rounds=6]

Each round: collect new links from the feeds (skipping every link seen before), snapshot the Brave profile's
Safe Browsing files, probe the new links (Google's lists as Brave holds them, Cloudflare, Quad9,
CleanBrowsing), fetch every live page once to see whether it has a password field (a credential trap, which
no blocklist had to say so), and append to <dir>/rounds.jsonl and <dir>/rounds_content.json.
Every row is probed at the moment its link is first seen, so recall here is "at first sight", the case that
matters for someone who clicks a fresh phishing mail.
"""
import concurrent.futures
import glob
import json
import os
import shutil
import sys
import time

import sb_coverage as sb
import sb_free_signals as fs
import shield_bench as bench


def main():
    work, profile = sys.argv[1:3]
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    gap = float(sys.argv[4]) * 3600 if len(sys.argv) > 4 else 6 * 3600
    seen = set()
    for f in glob.glob(os.path.join(work, "items*.json")):
        try:
            seen |= {i["url"] for i in json.load(open(f))}
        except (OSError, ValueError):
            pass
    out = os.path.join(work, "rounds.jsonl")
    content_path = os.path.join(work, "rounds_content.json")
    content = json.load(open(content_path)) if os.path.exists(content_path) else {}
    for r in range(rounds):
        started = time.time()
        raw = os.path.join(work, "round%d_raw.json" % r)
        bench.collect(raw, 600)
        items = [i for i in json.load(open(raw)) if i["url"] not in seen]
        seen |= {i["url"] for i in items}
        mine = os.path.join(work, "items_round%d.json" % r)
        json.dump(items, open(mine, "w"))
        snap = os.path.join(work, "round_snapshot")
        shutil.rmtree(snap, ignore_errors=True)
        os.makedirs(snap)
        for f in glob.glob(os.path.join(profile, "Safe Browsing", "*.store.4_*")):
            shutil.copy(f, snap)
        before = sum(1 for _ in open(out)) if os.path.exists(out) else 0
        bench.probe(mine, out, snap)
        rows = [json.loads(l) for l in open(out)][before:]
        live = [x for x in rows if x["alive"]]

        def one(x):
            page = fs.fetch_page(x["url"])
            return x["url"], {"html": bool(page.get("html")), "password": bool(page.get("password")),
                              "login": bool(page.get("login")), "title": page.get("title", "")}
        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as ex:
            for url, info in ex.map(one, live):
                content[url] = info
        json.dump(content, open(content_path, "w"))
        print("round %d: %d new links, %d live, %d with a password field" % (
            r, len(items), len(live), sum(1 for x in live if content[x["url"]]["password"])), flush=True)
        wait = started + gap - time.time()
        if r + 1 < rounds and wait > 0:
            time.sleep(wait)


if __name__ == "__main__":
    main()

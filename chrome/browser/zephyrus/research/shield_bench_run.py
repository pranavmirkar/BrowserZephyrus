#!/usr/bin/env python3
"""Runs shield_bench.py probe on one fixed list of links at +0, +1h, +3h, +6h and +24h.

  shield_bench_run.py <items.json> <results.jsonl> <Brave profile dir kept running for fresh lists>

A copy of the profile's Safe Browsing files is taken before each probe, so Google's lists are read as they
stand at that moment and nothing holds a lock on the running browser. Results are appended, so a partial
run can be reported at any time with `shield_bench.py report`.
"""
import glob
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DELAYS = [0, 3600, 3 * 3600, 6 * 3600, 24 * 3600]


def main():
    items, results, profile = sys.argv[1:4]
    start = time.time()
    for delay in DELAYS:
        wait = start + delay - time.time()
        if wait > 0:
            time.sleep(wait)
        snap = os.path.join(os.path.dirname(results), "brave_sb_snapshot")
        shutil.rmtree(snap, ignore_errors=True)
        os.makedirs(snap)
        for f in glob.glob(os.path.join(profile, "Safe Browsing", "*.store.4_*")):
            shutil.copy(f, snap)
        subprocess.run([sys.executable, "-u", os.path.join(HERE, "shield_bench.py"), "probe", items, results, snap],
                       cwd=HERE)
        print("probe at +%d s done" % delay, flush=True)


if __name__ == "__main__":
    main()

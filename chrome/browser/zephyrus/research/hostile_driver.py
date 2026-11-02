#!/usr/bin/env python3
"""Runs a damaged / rewritten engine-blob campaign against Brave's engine or
Zephyrus's compiled index and tallies what happened, surviving the crashes it
is looking for.

  hostile_driver.py brave <damage|hostile> <cases> [seed]
  hostile_driver.py zeph  <damage|hostile> <cases> [seed]

Both children print "CASE n" before a case and "OUT n <outcome>" after it. A
child that dies between the two crashed on case n (or hung, when the watchdog
killed it); the driver records that and restarts at n + 1. Outcomes: rejected
(refused at load), same / changed (loaded; the 300 probe decisions are the same
as the undamaged engine's / differ), panic (Brave: a caught Rust panic), crash,
hang.

Paths are the ones on this machine; override with the environment variables
HOSTILE_LIST, HOSTILE_CORPUS, HOSTILE_BRAVE, HOSTILE_ZEPH.
"""
import collections
import os
import queue
import subprocess
import sys
import threading
import time

LIST = os.environ.get("HOSTILE_LIST", r"D:\zeph_perf\h2h\preprocessed.txt")
CORPUS = os.environ.get("HOSTILE_CORPUS", r"D:\zeph_perf\h2h\corpus.tsv")
BRAVE = os.environ.get("HOSTILE_BRAVE", r"D:\zeph_perf\cargo_target\release\corrupt.exe")
ZEPH = os.environ.get("HOSTILE_ZEPH", r"D:\chromium\src\out\Release\zephyrus_adblock_engine_unittests.exe")
WATCHDOG_SECONDS = 60


def child_command(which, mode, first, end, seed):
    env = dict(os.environ)
    if which == "brave":
        return [BRAVE, LIST, CORPUS, mode, str(first), str(end), str(seed)], env
    env.update({
        "ZEPHYRUS_H2H_LIST": LIST,
        "ZEPHYRUS_H2H_CORPUS": CORPUS,
        "ZEPHYRUS_HOSTILE_MODE": mode,
        "ZEPHYRUS_HOSTILE_FIRST": str(first),
        "ZEPHYRUS_HOSTILE_END": str(end),
        "ZEPHYRUS_HOSTILE_SEED": str(seed),
    })
    return [ZEPH, "--gtest_filter=AdblockZIndexTest.HostileBlobCampaign",
            "--single-process-tests"], env


def pump(stream, lines):
    for raw in iter(stream.readline, b""):
        lines.put(raw.decode("utf-8", "replace").rstrip())
    lines.put(None)


def main():
    which, mode, cases = sys.argv[1], sys.argv[2], int(sys.argv[3])
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    tally = collections.Counter()
    crashes = []
    next_case = 0
    started = time.time()
    base_line = ""
    while next_case < cases:
        cmd, env = child_command(which, mode, next_case, cases, seed)
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, bufsize=0)
        lines = queue.Queue()
        threading.Thread(target=pump, args=(proc.stdout, lines), daemon=True).start()
        current = None
        done = False
        hung = False
        while True:
            try:
                line = lines.get(timeout=WATCHDOG_SECONDS)
            except queue.Empty:
                hung = True
                proc.kill()
                break
            if line is None:
                break
            if line.startswith("BASE"):
                base_line = line
            elif line.startswith("LOAD_MS"):
                base_line += "   load " + line.split()[1] + " ms"
            elif line.startswith("CASE "):
                current = int(line.split()[1])
            elif line.startswith("OUT "):
                _, n, outcome = line.split()[:3]
                tally[outcome] += 1
                next_case = int(n) + 1
                current = None
            elif line.startswith("DONE"):
                done = True
        proc.wait()
        if done and current is None:
            break
        if current is not None:
            kind = "hang" if hung else "crash"
            tally[kind] += 1
            crashes.append((current, kind, proc.returncode))
            next_case = current + 1
        elif not done:
            # Died before reaching its first case (a setup failure): stop, do not spin.
            print("child failed before running a case:", cmd[0], "exit", proc.returncode)
            break
    # A mutation that rewrote bytes with the values already there damaged nothing:
    # it is counted, but left out of the percentages.
    noop = tally.pop("noop", 0)
    total = sum(tally.values())
    print(f"{which} / {mode}: {total} damaged files (+{noop} no-op mutations) in "
          f"{time.time() - started:.0f} s   {base_line}")
    for outcome in ("rejected", "same", "changed", "panic", "crash", "hang"):
        count = tally.get(outcome, 0)
        print(f"  {outcome:<9} {count:>8}  {100.0 * count / max(total, 1):6.2f}%")
    for case, kind, code in crashes[:10]:
        print(f"  {kind} on case {case} (exit {code:#x})" if code is not None else f"  {kind} on case {case}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Does it matter whether `$third-party` and `$domain=` are judged against the frame that issued a request
(what Zephyrus does today, via `request_initiator`) or against the tab's top-level page?

usage: initiator_compare.py real.tsv decisions_frame.txt decisions_top.txt
Column 1 of each decisions file is the shipped engine's verdict (1 = block). Both files cover the same requests;
they differ only in which URL was handed to the engine as the initiator.

Read the second half of the output before the first. Judged one request at a time, the tab-relative choice looks like it
blocks more. But the log comes from a visit with nothing blocked, and most of those extra blocks are requests made by ad
frames that the engine blocks at load anyway. Replayed the way a protected visit unfolds (reach.py), the difference is the
requests that would really be made.
"""
import sys, os, collections
from urllib.parse import urlparse
sys.dont_write_bytecode = True  # keep .pyc files out of the repo
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I drops the script directory from the path
from reach import load_rows, load_verdicts, reachable

rows = load_rows(sys.argv[1]); frame = load_verdicts(sys.argv[2]); top = load_verdicts(sys.argv[3])
assert len(rows) == len(frame) == len(top), 'the three files must describe the same requests'
n = len(rows)
def host(u): return (urlparse(u).hostname or '').lower().rstrip('.')

moved = [i for i in range(n) if not frame[i] and top[i]]
print(f'{n} requests. Judged one at a time: {sum(frame)} blocked with the issuing frame as initiator, {sum(top)} with the tab as initiator')
print(f'  {len(moved)} requests are blocked only when the tab is the initiator; {sum(1 for i in range(n) if frame[i] and not top[i])} the other way')

rf, rt = reachable(rows, frame), reachable(rows, top)
def summary(name, v, rc):
    att = sum(rc); blocked = sum(1 for i in range(n) if rc[i] and v[i]); kept = att - blocked
    print(f'  {name:19s} a protected visit makes {att} requests, blocks {blocked}, keeps {kept}')
print('\nReplayed as a protected visit (a blocked frame loads nothing):')
summary('frame as initiator', frame, rf); summary('tab as initiator', top, rt)
never = [i for i in moved if not rf[i] or frame[i]]
print(f'\nOf the {len(moved)}: {sum(1 for i in moved if frame[i] == 0 and not rf[i])} come from a frame the engine already blocks at load, so they would never be made.')
real = [i for i in moved if rf[i] and not frame[i]]
by = collections.Counter((host(rows[i][1]), host(rows[i][2]), rows[i][3]) for i in real)
print(f'{len(real)} would really be made and would really change:')
for (h, f, t), c in by.most_common(8): print(f'    {c:3d}  {h}  <-  {f}  [{t}]')

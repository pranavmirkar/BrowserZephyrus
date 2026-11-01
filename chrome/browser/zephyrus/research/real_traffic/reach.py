#!/usr/bin/env python3
"""Which requests would a protected visit actually make?

crawl.js visits each page with nothing blocked, so the log contains requests made by frames that a blocker would have
stopped at load. Judged one request at a time, those look like extra work for the blocker. In a protected visit they never
happen. A request is reachable when the frame that issued it is reachable and that frame's own document request was not
blocked. Requests from the main frame are reachable. A frame whose document request is missing from the log (about:blank,
srcdoc) is treated as reachable, which can only overcount.
"""
import collections, sys

def load_rows(path):
    return [l.split('\t') for l in open(path).read().split('\n') if l]

def load_verdicts(path):
    return [int(l.split()[0]) for l in open(path).read().split('\n') if l]

def _norm(u):
    return u.split('#')[0]

def reachable(rows, verdict):
    sys.setrecursionlimit(20000)
    docs = collections.defaultdict(list)
    for j, r in enumerate(rows):
        if r[3] == 'subdocument':
            docs[(r[0], _norm(r[1]))].append(j)
    memo = {}
    def ok(i, stack=()):
        if i in memo: return memo[i]
        r = rows[i]
        if _norm(r[2]) == _norm(r[11]):
            res = True
        else:
            loads = [j for j in docs.get((r[0], _norm(r[2])), []) if j != i and j not in stack]
            res = True if not loads else any((not verdict[j]) and ok(j, stack + (i,)) for j in loads)
        memo[i] = res
        return res
    return [ok(i) for i in range(len(rows))]

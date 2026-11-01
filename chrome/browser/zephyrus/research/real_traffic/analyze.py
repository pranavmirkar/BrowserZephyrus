#!/usr/bin/env python3
"""What the engines decided on real traffic, and what independent sources would add.
usage: analyze.py real.tsv zeph_decisions.txt brave_decisions.txt [indep-dir]"""
import sys, os, json, re, collections
from urllib.parse import urlparse
sys.dont_write_bytecode = True  # keep .pyc files out of the repo
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I drops the script directory from the path
from reach import reachable
rows = [l.split('\t') for l in open(sys.argv[1]).read().split('\n')]
dz = [tuple(map(int, l.split())) for l in open(sys.argv[2]).read().split('\n') if l]
sh, zi = [x[0] for x in dz], [x[1] for x in dz]
br = [int(x) for x in open(sys.argv[3]).read().split()]
n = len(rows); host = lambda u: (urlparse(u).hostname or '').lower().rstrip('.')
suf = lambda h: ['.'.join(h.split('.')[i:]) for i in range(len(h.split('.')) - 1)] + [h]
print(f'{n} requests on {len(set(r[0] for r in rows))} pages (average {n // len(set(r[0] for r in rows))} per page)')
print(f'shipped engine == ZIndex on {sum(a == b for a, b in zip(sh, zi))}; shipped == Brave on {sum(a == b for a, b in zip(sh, br))} of {n}')
blocked = sum(sh); pages = len(set(r[0] for r in rows)); rc = reachable(rows, sh)
att = sum(rc); blocked_att = sum(1 for i in range(n) if rc[i] and sh[i]); kept_idx = [i for i in range(n) if rc[i] and not sh[i]]
print(f'an unprotected visit makes {n} requests; {blocked} ({100 * blocked / n:.1f}%) match a rule; by type: {dict(collections.Counter(r[3] for r, s in zip(rows, sh) if s).most_common(6))}')
print(f'a protected visit makes {att} ({round(att / pages)} per page): {n - att} sit inside frames the engine blocks at load and never happen; of the {att}, {blocked_att} ({100 * blocked_att / att:.1f}%) are blocked and {len(kept_idx)} ({round(len(kept_idx) / pages)} per page) load')
gone = sum(int(r[6]) for i, r in enumerate(rows) if (not rc[i] or sh[i]) and int(r[6]) >= 0); gone_n = sum(1 for i, r in enumerate(rows) if (not rc[i] or sh[i]) and int(r[6]) >= 0)
print(f'declared size known for {gone_n} of the {n - len(kept_idx)} requests that never load: {gone / 1e6:.1f} MB, {gone / pages / 1e6:.2f} MB per page (an undercount: many responses declare no length)')
pg = collections.defaultdict(lambda: [0, 0])
for i, r in enumerate(rows): pg[r[0]][0] += 1; pg[r[0]][1] += (not rc[i]) or sh[i]
fr = sorted(v[1] / v[0] for v in pg.values()); print(f'share of a page\'s unprotected requests that never load: median {100 * fr[len(fr) // 2]:.0f}%, min {100 * fr[0]:.0f}%, max {100 * fr[-1]:.0f}%')
if len(sys.argv) > 4:
    D = sys.argv[4]
    sb = {l.split()[1].lower() for l in open(D + '/stevenblack.txt', errors='replace') if l.startswith(('0.0.0.0 ', '127.0.0.1 ')) and len(l.split()) > 1}
    pl = {l.strip().lower() for l in open(D + '/peterlowe.txt', errors='replace') if l.strip() and not l.startswith('#')}
    ag = {m.group(1) for l in open(D + '/adguard_dns.txt', errors='replace') for m in [re.match(r'^\|\|([a-z0-9.-]+)\^(\$.*)?$', l.strip())] if m}
    dcs = {}
    dc = json.load(open(D + '/disconnect.txt'))
    for cat, items in dc['categories'].items():
        if cat in ('Advertising', 'Analytics', 'FingerprintingGeneral', 'FingerprintingInvasive', 'Cryptomining', 'Social'):
            for ent in items:
                for nm, sites in ent.items():
                    for s, d in sites.items():
                        if isinstance(d, list):
                            for x in d: dcs[x.lower()] = cat
    allowed = [rows[i] for i in kept_idx]
    src = {'StevenBlack hosts': lambda h: h in sb, "Peter Lowe's list": lambda h: h in pl, 'AdGuard DNS filter': lambda h: any(x in ag for x in suf(h)), 'Disconnect (trackers)': lambda h: any(x in dcs for x in suf(h))}
    union = set()
    print(f'\nrequests a protected visit still makes: {len(allowed)}. What other sources would additionally block:')
    for name, fn in src.items():
        hit = [i for i, r in enumerate(allowed) if fn(host(r[1]))]; union.update(hit)
        print(f'  {name:24s} {len(hit):4d} requests ({100 * len(hit) / att:.1f}% of {att}, {100 * len(hit) / len(allowed):.1f}% of the {len(allowed)} that load) on {len(set(host(allowed[i][1]) for i in hit))} hosts')
    hosts = collections.Counter(host(allowed[i][1]) for i in union)
    print(f'  any of the four          {len(union):4d} requests ({100 * len(union) / att:.1f}% of {att}, {100 * len(union) / len(allowed):.1f}% of the {len(allowed)} that load) on {len(hosts)} hosts')
    print('  most frequent:', ', '.join(f'{h} ({c})' for h, c in hosts.most_common(10)))

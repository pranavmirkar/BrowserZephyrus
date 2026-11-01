#!/usr/bin/env python3
"""A crude 'looks like tracking' test on third-party requests that the lists allow. It is a NEGATIVE RESULT kept for the record:
the shapes below find real ad-tech that no list covers, but they also flag large numbers of ordinary CDNs and widgets, so they
could feed a human review queue and could not safely drive blocking.

usage: shape_heuristic.py real.tsv decisions.txt indep-dir
Column 1 of decisions.txt is the shipped engine's verdict (1 = block). Only requests a protected visit would really make
and the lists let through are examined (see reach.py). indep-dir is the folder run_real_traffic.sh fills with the
independent blocklists.
Shapes: an identifier-looking query value, a tiny image, a POST/ping beacon, a tracking-style path segment.
"""
import sys, os, json, re, collections
from urllib.parse import urlparse, parse_qsl
sys.dont_write_bytecode = True  # keep .pyc files out of the repo
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # -I drops the script directory from the path
from reach import reachable

rows = [l.split('\t') for l in open(sys.argv[1]).read().split('\n') if l]
zt = [int(l.split()[0]) for l in open(sys.argv[2]).read().split('\n') if l]
rc = reachable(rows, zt)
D = sys.argv[3]
TWO = {'co.uk', 'org.uk', 'ac.uk', 'gov.uk', 'com.au', 'net.au', 'co.in', 'net.in', 'org.in', 'co.jp', 'com.br', 'com.cn',
       'com.mx', 'co.nz', 'co.za', 'com.sg', 'com.tr', 'com.tw', 'com.hk', 'co.kr'}
def host(u): return (urlparse(u).hostname or '').lower().rstrip('.')
def reg(h):
    p = h.split('.')
    return '.'.join(p[-3:]) if len(p) >= 3 and '.'.join(p[-2:]) in TWO else '.'.join(p[-2:])
def suf(h): return ['.'.join(h.split('.')[i:]) for i in range(len(h.split('.')) - 1)]

sb = {l.split()[1].lower() for l in open(D + '/stevenblack.txt', errors='replace')
      if l.startswith(('0.0.0.0 ', '127.0.0.1 ')) and len(l.split()) > 1}
ag = {m.group(1) for l in open(D + '/adguard_dns.txt', errors='replace')
      for m in [re.match(r'^\|\|([a-z0-9.-]+)\^(\$.*)?$', l.strip())] if m}
dcs = set()
for cat, items in json.load(open(D + '/disconnect.txt'))['categories'].items():
    if cat in ('Advertising', 'Analytics', 'FingerprintingGeneral', 'FingerprintingInvasive', 'Cryptomining', 'Social'):
        for ent in items:
            for _, sites in ent.items():
                for _, d in sites.items():
                    if isinstance(d, list): dcs.update(x.lower() for x in d)
def indep(h): return h in sb or any(x in ag or x in dcs for x in suf(h) + [h])

ID = re.compile(r'^(uid|u|cid|cuid|guid|uuid|vid|vuid|sid|did|idfa|gaid|ifa|aid|pid|ppid|puid|anid|visitor_?id|user_?id|client_?id|device_?id|_ga|_gid|fbp|fbc|gclid|msclkid|ttclid)$', re.I)
IDVAL = re.compile(r'^[0-9a-f]{16,}$|^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$|^[A-Za-z0-9_-]{22,}$', re.I)
PATH = re.compile(r'/(sync|usersync|cookie_?sync|getuid|setuid|match|cm|pixel|px|collect|beacon|track|event|ping|impression|imp)(/|\?|$|\.)', re.I)

kept = [r for r, s, ok in zip(rows, zt, rc) if ok and not s]
third = [r for r in kept if reg(host(r[1])) != reg(host(r[11]))]
print(f'requests a protected visit still makes: {len(kept)}; of those third-party to the page: {len(third)}')
flags = collections.Counter(); flagged = []
for r in third:
    u = urlparse(r[1]); q = parse_qsl(u.query, keep_blank_values=True); f = set()
    if r[3] == 'image' and 0 <= int(r[6]) <= 200 and r[5].startswith('image/'): f.add('tiny image (pixel)')
    if r[3] == 'ping' or (r[8] == 'POST' and int(r[7]) > 0 and r[3] in ('xhr', 'ping', 'other') and not r[5].startswith('text/html')): f.add('beacon / POST to third party')
    if any(ID.match(k) and v for k, v in q) or any(IDVAL.match(v) for _, v in q if len(v) >= 16): f.add('identifier in query')
    if PATH.search(u.path): f.add('tracking-style path')
    for x in f: flags[x] += 1
    if f: flagged.append((r, indep(host(r[1]))))
n = len(flagged); unl = sum(1 for _, i in flagged if not i)
print(f'  look like tracking by shape: {n} ({100 * n / max(len(third), 1):.0f}% of them); covered by none of the four independent lists: {unl} ({100 * unl / max(n, 1):.0f}%)')
for k, v in flags.most_common(): print(f'    {v:4d}  {k}')
hosts = collections.Counter(host(r[1]) for r, i in flagged if not i)
print(f'\n  shape-flagged third-party hosts that no list covers ({len(hosts)} hosts); top by requests:')
for h, c in hosts.most_common(14): print(f'    {c:3d}  {h}')
print('\n  CAVEAT: heuristic flags, not verified labels. Many are benign (CDNs, widgets, consent and error reporting);\n'
      '  a few are real ad-tech. That mix is the finding: use it to queue human review, never to block.')

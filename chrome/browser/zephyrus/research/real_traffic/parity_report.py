#!/usr/bin/env python3
"""Cross-tabulates which rules the Zephyrus parser and Brave's parser each keep as network rules.
usage: parity_report.py lines.tsv zeph.txt brave.txt"""
import sys, collections
lines = [l.split('\t', 1) for l in open(sys.argv[1]).read().split('\n')]
z = [int(x) for x in open(sys.argv[2]).read().split()]; b = open(sys.argv[3]).read().split('\n')[:len(lines)]
def opts(rule):
    r = rule[2:] if rule.startswith('@@') else rule; i = r.rfind('$')
    return [o.lstrip('~').split('=')[0] for o in r[i + 1:].split(',') if o] if i >= 0 else []
def isregex(rule):
    r = rule[2:] if rule.startswith('@@') else rule; i = r.find('$'); p = r if i < 0 else r[:i]
    return len(p) >= 2 and p[0] == '/' and p[-1] == '/'
KNOWN = ('redirect', 'redirect-rule', 'removeparam', 'csp', 'method', 'to', 'badfilter', 'tag', 'match-case', 'rewrite', 'header', 'replace', 'urlskip', 'cname', 'ipaddress')
cat = collections.Counter(); only_b = collections.Counter(); only_z = collections.Counter()
for (lst, rule), zk, bv in zip(lines, z, b):
    if bv == 'C' and not zk: continue
    if bv == 'N' and zk: cat['both keep'] += 1
    elif bv == 'N': cat['Brave keeps, Zephyrus drops'] += 1; o = [x for x in opts(rule) if x in KNOWN]; only_b['regex pattern' if isregex(rule) else (o[0] if o else 'other')] += 1
    elif zk: cat['Zephyrus keeps, Brave drops'] += 1; un = [x for x in opts(rule) if x in ('popup', 'popunder', 'denyallow', 'ehide')]; only_z[un[0] if un else 'other'] += 1
    elif bv.startswith('E'): cat['both drop'] += 1
tot = sum(cat.values()); print(f'network-rule lines: {tot}')
for k, v in cat.most_common(): print(f'  {v:7d} {100 * v / tot:5.2f}%  {k}')
print('Brave keeps, Zephyrus drops, by cause (note: the `badfilter` count is an artefact of testing one line at a time):'); [print(f'  {v:5d} {k}') for k, v in only_b.most_common()]
print('Zephyrus keeps, Brave drops, by option Brave does not parse:'); [print(f'  {v:5d} {k}') for k, v in only_z.most_common()]

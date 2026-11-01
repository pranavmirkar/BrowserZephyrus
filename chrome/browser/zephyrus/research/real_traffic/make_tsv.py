#!/usr/bin/env python3
"""Turns crawl.js output into real.tsv, one request per row:
page, url, initiator(frame), type, status, content-type, content-length, post-bytes, method, is-main-frame, failed, top-level-page-url
Pages that did not really load (a bot-wall's single request) are dropped."""
import json, glob, re, sys
src, out = sys.argv[1], sys.argv[2]
TYPE = {'document': 'document', 'stylesheet': 'stylesheet', 'image': 'image', 'media': 'media', 'font': 'font', 'script': 'script',
        'texttrack': 'media', 'xhr': 'xhr', 'fetch': 'xhr', 'eventsource': 'xhr', 'websocket': 'websocket', 'manifest': 'other', 'other': 'other', 'ping': 'ping'}
rows, pages = [], 0
for f in sorted(glob.glob(src + '/*.json')):
    d = json.load(open(f))
    if d['n'] < 40: continue
    pages += 1; top = d['top']
    for r in d['reqs']:
        u = r['url']
        if not re.match(r'https?://', u): continue
        t = TYPE.get(r['type'], 'other')
        ini = r['frame'] if r['frame'].startswith('http') else top
        if r['type'] == 'document': ini = top          # a frame's own navigation is issued by the page that embeds it
        if r['type'] == 'document' and not r['main']: t = 'subdocument'
        rows.append((pages, u, ini, t, r.get('status', 0), r.get('ct', '').split(';')[0], r.get('cl', -1), r.get('post', 0), r['method'], int(r['main']), int(r.get('failed', False)), top))
open(out, 'w').write('\n'.join('\t'.join(str(x).replace('\t', ' ').replace('\n', ' ') for x in row) for row in rows))
print(pages, 'pages,', len(rows), 'requests')

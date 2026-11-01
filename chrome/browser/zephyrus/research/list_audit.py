import re, collections, sys, glob, os
EMPTY = {"noopjs","noop.js","nooptext","noop.txt","noopframe","noop.html","noopjson","noop.json","noopmp3-0.1s","noop-0.1s.mp3","noopmp4-1s","noop-1s.mp4","1x1.gif","2x2.png","3x2.png","32x32.png","1x1-transparent.gif","2x2-transparent.png","3x2-transparent.png","32x32-transparent.png","empty","none"}
SUP = {"third-party","3p","first-party","1p","script","image","stylesheet","css","object","xmlhttprequest","xhr","subdocument","frame","font","media","ping","websocket","popup","popunder","document","doc","all","generichide","ghide","elemhide","ehide","specifichide","shide","other","important","badfilter"}
tot = collections.Counter(); opts_dropped = collections.Counter(); proc = collections.Counter(); scr = collections.Counter(); per = {}
for path in sorted(sys.argv[1:]):
    c = collections.Counter()
    for l in open(path, encoding='utf-8', errors='replace'):
        s = l.strip()
        if not s or s[0] in '![' : continue
        if s.startswith('!#'): continue
        if re.search(r'#@?[?$%]?#', s):
            if '+js(' in s:
                c['scriptlet']+=1; m=re.search(r'\+js\(([^,)]+)', s)
                if m: scr[m.group(1).strip()]+=1
            elif '#?#' in s: c['procedural #?#']+=1
            elif '#$#' in s or '#%#' in s: c['css-inject / ABP snippet']+=1
            else:
                c['element-hide']+=1
                for op in re.findall(r':(has-text|upward|xpath|matches-css|matches-attr|matches-media|min-text-length|watch-attr|remove|has|nth-ancestor|others)\(', s): proc[op]+=1
            continue
        body = s[2:] if s.startswith('@@') else s
        i = body.find('$'); pat = body if i<0 else body[:i]; opts = '' if i<0 else body[i+1:]
        if len(pat)>=2 and pat[0]=='/' and pat[-1]=='/': c['net: regex']+=1; opts_dropped['regex pattern']+=1; continue
        bad=None
        for o in [x for x in opts.split(',') if x]:
            n=o.lstrip('~'); k=n.split('=')[0]
            if n in SUP or k in ('domain','from','denyallow'): continue
            if k=='redirect' and not s.startswith('@@') and n.split('=',1)[-1].split(':')[0] in EMPTY: continue
            bad = k if k!='redirect' else 'redirect (surrogate)'; break
        if bad: c['net: dropped']+=1; opts_dropped[bad]+=1
        else: c['net: kept']+=1
    per[os.path.basename(path)] = c
    tot.update(c)
print(f"{'list':26} {'net kept':>9} {'net drop':>9} {'regex':>6} {'script-lets':>11} {'elemhide':>9} {'procedural':>10}")
for k,c in per.items(): print(f"{k:26} {c['net: kept']:9d} {c['net: dropped']:9d} {c['net: regex']:6d} {c['scriptlet']:11d} {c['element-hide']:9d} {c['procedural #?#']:10d}")
n = tot['net: kept']+tot['net: dropped']+tot['net: regex']
print(f"\nTOTAL network rules {n}: kept {tot['net: kept']} ({100*tot['net: kept']/n:.1f}%), dropped on options {tot['net: dropped']}, regex {tot['net: regex']}")
print('procedural #?# rules:', tot['procedural #?#'], '| css-inject/snippets:', tot['css-inject / ABP snippet'])
print('\nwhy network rules are dropped:'); [print(f'{v:7d}  {k}') for k,v in opts_dropped.most_common(14)]
print('\nextended selectors in ## rules:'); [print(f'{v:7d}  {k}') for k,v in proc.most_common(10)]
print('\nscriptlets used (top 25):'); [print(f'{v:7d}  {k}') for k,v in scr.most_common(25)]

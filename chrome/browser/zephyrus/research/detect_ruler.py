#!/usr/bin/env python3
"""The detection ruler: can a page tell that an ad blocker is running?

  detect_ruler.py [arms=zephyrus,brave,ubol,chrome] [out dir=D:\\zeph_perf\\detect]

A local page runs the checks anti-adblock scripts use, each written from scratch from the published
techniques, and reports which ones fired. Plain Chrome is the control: a check that fires there is a
broken check, not a detection, and is left out of the score. Every arm loads the page three times.

It also reports whether the ads were actually blocked, because an undetectable blocker that lets the
ads through has only stopped blocking.

Runs on the hidden desktop shield_ruler.py uses.
"""
import http.server
import json
import os
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shield_ruler as R  # noqa: E402

PORT = 8793
ADSENSE = "https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js"
GPT = "https://securepubads.g.doubleclick.net/tag/js/gpt.js"
PIXEL = "https://googleads.g.doubleclick.net/pagead/viewthroughconversion/1/?random=1"
IMA = "https://imasdk.googleapis.com/js/sdkloader/ima3.js"
PREBID_HOST = "https://ib.adnxs.com/ut/v3/prebid"

PAGE = r"""<!doctype html><meta charset=utf-8><title>Detect</title>
<style>body{font:14px sans-serif}</style>
<h1>A page with ads</h1><p>Some article text that a reader came for.</p>
<div id=slot1 class="adsbygoogle" style="display:block;width:300px;height:250px"></div>
<script>
const results = {};
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
function hiddenOrGone(el) {
  if (!el.isConnected) return 'removed';
  const cs = getComputedStyle(el);
  if (cs.display === 'none') return 'display:none';
  if (cs.visibility === 'hidden') return 'visibility:hidden';
  if (el.offsetParent === null && cs.position !== 'fixed') return 'no offsetParent';
  if (el.offsetHeight === 0 || el.offsetWidth === 0) return 'zero size';
  if (el.clientHeight === 0) return 'zero clientHeight';
  const r = el.getBoundingClientRect();
  if (r.height === 0 || r.width === 0) return 'zero rect';
  if (parseFloat(cs.opacity) === 0) return 'opacity 0';
  return null;
}
function loadScript(src) {
  return new Promise((res) => {
    const s = document.createElement('script'); s.src = src; s.async = true;
    s.onload = () => res('load'); s.onerror = () => res('error');
    document.head.appendChild(s); setTimeout(() => res('timeout'), 8000);
  });
}
async function bait(name, html, container) {
  const holder = document.createElement('div'); holder.innerHTML = html;
  const el = holder.firstElementChild; (container || document.body).appendChild(el);
  await wait(150); await new Promise(requestAnimationFrame); await wait(350);
  results[name] = hiddenOrGone(el); el.remove();
}
async function run() {
  // 1-4. Bait elements, the BlockAdBlock / FuckAdBlock / "adsbox" family.
  await bait('bait_blockadblock', '<div class="pub_300x250 pub_300x250m pub_728x90 text-ad textAd text_ad text_ads text-ads text-ad-links" style="width:1px!important;height:1px!important;position:absolute!important;left:-10000px!important;top:-1000px!important">&nbsp;</div>');
  await bait('bait_adsbox', '<div class="adsbox" style="height:1px">&nbsp;</div>');
  await bait('bait_ad_banner_id', '<div id="ad_banner" style="height:10px">x</div>');
  await bait('bait_in_page', '<div class="ad-placement ad-slot adsbygoogle" style="height:90px;width:728px">&nbsp;</div>');
  await bait('bait_img_banner', '<img src="data:image/gif;base64,R0lGODlhAQABAAAAACw=" class="ad_banner" width=10 height=10>');
  // 5. The page's own <ins class=adsbygoogle> slot, measured after a while.
  await wait(500);
  results.slot_collapsed = hiddenOrGone(document.getElementById('slot1'));
  // 6. Load AdSense and look for what it defines.
  const ads = await loadScript('""" + ADSENSE + r"""');
  results.adsense_script = ads === 'load' ? null : 'script ' + ads;
  results.adsense_api = (window.adsbygoogle && typeof window.adsbygoogle.push === 'function') ? null : 'adsbygoogle missing';
  // 6b. What AdSense leaves on a slot it was asked to fill.
  {
    const ins = document.createElement('ins'); ins.className = 'adsbygoogle';
    ins.style.cssText = 'display:block;width:300px;height:250px';
    ins.setAttribute('data-ad-client', 'ca-pub-0000000000000000'); ins.setAttribute('data-ad-slot', '1234567890');
    document.body.appendChild(ins);
    try { (window.adsbygoogle = window.adsbygoogle || []).push({}); } catch (e) {}
    await wait(2500);
    results.adsense_slot_marked = ins.hasAttribute('data-adsbygoogle-status') ? null : 'slot never marked';
  }
  // 7. Google Publisher Tag.
  const gpt = await loadScript('""" + GPT + r"""');
  results.gpt_script = gpt === 'load' ? null : 'script ' + gpt;
  await wait(300);
  results.gpt_api = (window.googletag && (googletag.apiReady || typeof googletag.cmd !== 'undefined')) ? null : 'googletag missing';
  // 8. A fetch of an ad script, no-cors, the cheapest check there is.
  try { await fetch('""" + ADSENSE + r"""', {mode: 'no-cors', cache: 'no-store'}); results.fetch_nocors = null; }
  catch (e) { results.fetch_nocors = 'fetch rejected'; }
  // 9. An XHR to an ad server.
  results.xhr = await new Promise((res) => {
    const x = new XMLHttpRequest(); x.open('GET', '""" + PREBID_HOST + r"""');
    x.onload = () => res(null); x.onerror = () => res('xhr error'); x.send(); setTimeout(() => res('xhr timeout'), 8000);
  });
  // 10. An ad pixel.
  results.pixel = await new Promise((res) => {
    const i = new Image(); i.onload = () => res(null); i.onerror = () => res('img error'); i.src = '""" + PIXEL + r"""';
    setTimeout(() => res('img timeout'), 8000);
  });
  // 10b. The published detectadblock.com check: an ad server's favicon must load AND be
  // wider than 1px, because blockers answer images with a 1x1 stand-in.
  results.favicon_size = await new Promise((res) => {
    const i = new Image(); i.referrerPolicy = 'no-referrer';
    i.onload = () => res(i.width < 2 ? 'image ' + i.width + 'px wide' : null); i.onerror = () => res('img error');
    i.src = 'https://googleads.g.doubleclick.net/favicon.ico?' + Date.now(); setTimeout(() => res('img timeout'), 8000);
  });
  // 11. Video ads: the IMA SDK.
  const ima = await loadScript('""" + IMA + r"""');
  results.ima = (ima === 'load' && window.google && google.ima) ? null : 'ima ' + (ima === 'load' ? 'empty' : ima);
  // 12. "ads.js": a first-party file that only sets a flag, which generic rules block by name.
  window.canRunAds = undefined;
  const local = await loadScript('/js/ads.js?v=1');
  results.local_ads_js = window.canRunAds === true ? null : 'canRunAds unset (' + local + ')';
  // 13. Resource timing of the ad script: a blocked or faked load has no real transfer.
  const entry = performance.getEntriesByType('resource').find((e) => e.name.startsWith('""" + ADSENSE + r"""'));
  results.resource_timing = !entry ? 'no timing entry' : (entry.responseEnd - entry.startTime < 2 && entry.transferSize === 0 && entry.decodedBodySize < 1000 ? 'instant empty load' : null);
  // 14. Patched built-ins: a scriptlet that wraps a function must still look native.
  const looksNative = (f) => typeof f === 'function' && /\{\s*\[native code\]\s*\}/.test(Function.prototype.toString.call(f));
  const patched = ['fetch', 'setTimeout', 'setInterval', 'addEventListener', 'XMLHttpRequest', 'JSON.parse', 'Element.prototype.remove', 'Element.prototype.attachShadow', 'document.createElement', 'EventTarget.prototype.addEventListener']
    .filter((n) => { try { return !looksNative(n.split('.').reduce((o, k) => o[k], window)); } catch (e) { return false; } });
  results.patched_builtins = patched.length ? 'not native: ' + patched.join(',') : null;
  results.tostring_tostring = looksNative(Function.prototype.toString) ? null : 'toString patched';
  // 15. Style sheets the page did not add, readable from script.
  const foreign = [...document.styleSheets].filter((s) => !s.ownerNode || !s.ownerNode.isConnected || (s.ownerNode.tagName !== 'STYLE' && s.ownerNode.tagName !== 'LINK') || (s.ownerNode.tagName === 'STYLE' && !s.ownerNode.hasAttribute('data-page')));
  results.extra_stylesheets = (foreign.length > 1 || document.adoptedStyleSheets.length) ? (foreign.length + ' foreign sheets, ' + document.adoptedStyleSheets.length + ' adopted') : null;
  // 16. Globals a blocker leaves behind.
  const known = new Set(Object.getOwnPropertyNames(window));
  const odd = [...known].filter((k) => /zephyrus|ublock|ubo_|adguard|brave|scriptlet|__ab|_abp/i.test(k) && !['adsbygoogle'].includes(k));
  results.globals = odd.length ? odd.slice(0, 5).join(',') : null;
  // 17. A MutationObserver watching for elements the page did not touch being hidden or removed.
  const target = document.createElement('div'); target.className = 'sponsored-content ad-unit'; target.innerHTML = '<a href="#">Buy</a> sponsored'; document.body.appendChild(target);
  let touched = null;
  new MutationObserver((m) => { for (const r of m) if (r.type === 'attributes' && r.target === target) touched = 'attribute ' + r.attributeName + ' changed'; else if (r.removedNodes.length) touched = 'removed'; })
    .observe(document.body, {attributes: true, childList: true, subtree: true, attributeOldValue: true});
  await wait(1200);
  results.mutation = touched || hiddenOrGone(target);
  return JSON.stringify(results);
}
</script>
<style data-page>p{margin:4px 0}</style>
"""


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            body, kind = PAGE.encode("utf-8"), "text/html"
        elif path == "/js/ads.js":
            body, kind = b"window.canRunAds = true;", "text/javascript"
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", kind + "; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


def run_arm(arm, work, loads=3):
    p, _ = R.launch(arm, work)
    time.sleep(20)
    out = []
    try:
        cdp = R.Cdp(arm["port"])
        s = cdp.the_tab()
        cdp.call("Network.enable", s)
        for _ in range(loads):
            cdp.go(R.NEUTRAL, 8)
            cdp.call("Network.clearBrowserCache", s, wait=20)
            cdp.events.clear()
            cdp.go("http://127.0.0.1:%d/" % PORT, 15)
            got = cdp.call("Runtime.evaluate", s, wait=60, expression="run()", awaitPromise=True,
                           returnByValue=True)
            # What actually reached the ad servers.
            reached = set()
            for m in cdp.events:
                if m.get("method") == "Network.responseReceived":
                    r = m["params"]["response"]
                    if r.get("remoteIPAddress") and any(h in r["url"] for h in ("googlesyndication", "doubleclick", "imasdk", "adnxs")):
                        reached.add(r["url"].split("/")[2])
            try:
                row = json.loads(got["result"]["value"])
            except (KeyError, TypeError, ValueError):
                row = {"error": str(got)[:200]}
            row["_reached_ad_servers"] = sorted(reached)
            out.append(row)
    finally:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
    return out


def report(data):
    control = data.get("chrome")
    checks = [k for k in (control or next(iter(data.values())))[0] if not k.startswith("_") and k != "error"]
    broken = {k for k in checks if control and any(r.get(k) for r in control)}
    if broken:
        print("checks that fire in plain Chrome too (left out):", ", ".join(sorted(broken)))
    print("\n%-20s" % "check" + "".join("%-16s" % a for a in data if a != "chrome"))
    score = {a: 0 for a in data if a != "chrome"}
    for k in checks:
        if k in broken:
            continue
        cells = []
        for a in score:
            hits = [r.get(k) for r in data[a] if r.get(k)]
            if hits:
                score[a] += 1
            cells.append(("DETECTED " + str(hits[0])[:6]) if hits else "clean")
        print("%-20s" % k + "".join("%-16s" % c for c in cells))
    print("\n%-20s" % "detected by" + "".join("%-16s" % ("%d of %d" % (score[a], len(checks) - len(broken))) for a in score))
    print("%-20s" % "ad servers reached" + "".join("%-16s" % (",".join(h.split(".")[0] for h in data[a][0]["_reached_ad_servers"]) or "none") for a in score))
    if control:
        print("%-20s" % "(chrome reached)" + ",".join(h.split(".")[0] for h in control[0]["_reached_ad_servers"]))
    for a in score:
        print("\n%s details:" % a)
        for k in checks:
            vals = sorted({str(r.get(k)) for r in data[a] if r.get(k)})
            if vals and k not in broken:
                print("   %-20s %s" % (k, "; ".join(vals)[:150]))


def main():
    arms = (sys.argv[1] if len(sys.argv) > 1 else "zephyrus,brave,ubol,chrome").split(",")
    work = sys.argv[2] if len(sys.argv) > 2 else r"D:\zeph_perf\detect"
    os.makedirs(work, exist_ok=True)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    data = {}
    for name in arms:
        arm = dict([a for a in R.ARMS if a["name"] == name][0])
        arm["port"] += 100  # never the ports a running ruler uses
        if os.environ.get("DETECT_LOG"):
            arm["args"] = list(arm.get("args", [])) + ["--enable-logging", "--log-file=" + os.environ["DETECT_LOG"] + "_" + name + ".log"]
        # The rulers' profiles, so the lists are already downloaded; copied, so a
        # running ruler's profile is never opened twice.
        src = os.path.join(r"D:\zeph_perf\ruler", "prof_" + name)
        dst = os.path.join(work, "prof_" + name)
        if not os.path.exists(dst) and os.path.exists(src):
            subprocess.run(["robocopy", src, dst, "/E", "/R:0", "/W:0", "/NFL", "/NDL", "/NJH", "/NJS", "/XD", "Cache", "Code Cache", "GPUCache", "Service Worker"], capture_output=True)
        data[name] = run_arm(arm, work)
    path = os.path.join(work, "detect_%s.json" % time.strftime("%Y%m%d_%H%M%S"))
    json.dump(data, open(path, "w"), indent=1)
    report(data)
    print("\n", path)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "report":
        report(json.load(open(sys.argv[2])))
    else:
        main()

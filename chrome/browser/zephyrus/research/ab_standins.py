"""Live check of $redirect= stand-ins.

A test page asks, the way real pages do, for files the lists block with a stand-in: scripts, images,
stylesheets, audio and video, frames, and fetch/XHR (CORS, with credentials). Every URL is on a host that
does not exist -- the stand-in is answered inside the browser -- so a request that is NOT intercepted fails.
Two sets of cases:
  * REAL: one real rule per resource from the shipped list (rr_pick.py), requested at the URL it blocks;
  * ALL : one test rule per resource appended to the profile's list (the real IMA rules are scoped to
          particular sites, so a local page can only reach the machinery with a rule of its own).
For a script the stand-in must fire `load`, not `error`, and the API the page expects must exist (an ad
manager that answers, a `ga` that runs callbacks, ...). With --disable-features=ZephyrusAdblockStandIns every
case must fail, as a plain block does.
"""
import http.server, json, os, shutil, sys, threading, time, urllib.parse

sys.path.insert(0, r"D:\zeph_perf")
from cap import Browser, seed_profile

PORT = 8795
GEN = r"D:\chromium\src\chrome\browser\zephyrus\adblock\tools\gen_redirect_resources.py"
ns = {}
exec(open(GEN, encoding="utf-8").read().split("def cpp_text")[0].replace("__file__", repr(GEN)), ns)
RES = [(r[0], r[1], r[2]) for r in ns["R"]]          # id, name, mime

API = {
    "google-ima.js": "typeof google.ima.AdsLoader==='function' && google.ima.__stub===true",
    "googletagservices_gpt.js": "typeof googletag.pubads==='function' && googletag.apiReady===true",
    "googletagmanager_gtm.js": "Array.isArray(dataLayer) && !!google_tag_manager",
    "google-analytics_analytics.js": "typeof ga==='function' && ga.loaded===true",
    "google-analytics_ga.js": "typeof _gat==='object' && typeof _gaq.push==='function'",
    "google-analytics_cx_api.js": "typeof cxApi==='object'",
    "googlesyndication_adsbygoogle.js": "adsbygoogle.loaded===true",
    "doubleclick_instream_ad_status.js": "google_ad_status===1",
    "amazon_apstag.js": "typeof apstag.fetchBids==='function'",
    "amazon_ads.js": "typeof amznads==='object'",
    "scorecardresearch_beacon.js": "typeof COMSCORE.beacon==='function'",
    "chartbeat.js": "typeof pSUPERFLY.activity==='function'",
    "outbrain-widget.js": "typeof OBR.extern.reloadWidget==='function'",
    "fingerprint2.js": "typeof Fingerprint2==='function'",
    "fingerprint3.js": "typeof FingerprintJS.load==='function'",
    "prebid-ads.js": "canRunAds===true",
    "fuckadblock.js-3.2.0": "typeof fuckAdBlock==='object'",
    "nobab2.js": "typeof blockAdBlock==='object'", "nobab.js": "typeof blockAdBlock==='object'",
    "nofab.js": "typeof blockAdBlock==='object'",
    "popads.js": "typeof PopAds!=='undefined'",
    "nitropay_ads.js": "nitroAds.loaded===true",
    "adthrive_abd.js": "adthrive.abd===false",
    "sensors-analytics.js": "typeof sensorsDataAnalytic201505.track==='function'",
}
EXPECT_TEXT = {"noop.txt": "", "empty": "", "noop.json": "{}"}


def kind_of(name, mime):
    if mime == "text/javascript":
        return "script"
    return {"image/gif": "image", "image/png": "image", "audio/mpeg": "audio", "video/mp4": "video",
            "text/html": "frame", "text/css": "css"}.get(mime, "xhr")


TYPE_OPT = {"script": "script", "image": "image", "audio": "media", "video": "media", "frame": "subdocument",
            "css": "stylesheet", "xhr": "xhr"}
cases, rules = [], []
for rid, name, mime in RES:
    k = kind_of(name, mime)
    host = "s%d.standin.test" % rid
    url = f"https://{host}/{name}"
    rules.append(f"||{host}/{name}${TYPE_OPT[k]},redirect={name}")
    cases.append({"set": "ALL", "name": name, "kind": k, "url": url,
                  "api": API.get(name), "text": EXPECT_TEXT.get(name)})
MIME = {n: m for _, n, m in RES}
for c in json.load(open(r"D:\zeph_perf\rr\cases.json")):
    # a rule that asks for a picture and names a text stand-in cannot be judged by the picture loading
    mismatch = kind_of(c["name"], MIME.get(c["name"], "")) != c["kind"] and c["kind"] != "xhr"
    cases.append({"set": "REAL", "name": c["name"], "kind": c["kind"], "url": c["url"], "mismatch": mismatch,
                  "api": API.get(c["name"]), "text": EXPECT_TEXT.get(c["name"])})

PAGE = """<!doctype html><title>standins</title><body><script>
const cases = %s;
const results = {};
function done(i, status, extra) { if (!(i in results)) results[i] = Object.assign({status}, extra || {}); }
function el(tag, props) { const e = document.createElement(tag); Object.assign(e, props || {}); return e; }
async function run(i, c) {
  const settle = new Promise((resolve) => {
    const finish = (s, x) => { done(i, s, x); resolve(); };
    setTimeout(() => finish('timeout'), 9000);
    try {
      if (c.kind === 'script') {
        const s = el('script', {src: c.url, async: false});
        s.onload = () => { let api = null; try { api = c.api ? !!eval(c.api) : true; } catch (e) { api = false; }
                           finish('load', {api}); };
        s.onerror = () => finish('error'); document.body.appendChild(s);
      } else if (c.kind === 'image') {
        const m = el('img'); m.onload = () => finish('load', {w: m.naturalWidth, h: m.naturalHeight});
        m.onerror = () => finish('error'); m.src = c.url;
      } else if (c.kind === 'css') {
        const l = el('link', {rel: 'stylesheet', href: c.url}); l.onload = () => finish('load');
        l.onerror = () => finish('error'); document.head.appendChild(l);
      } else if (c.kind === 'audio' || c.kind === 'video') {
        const m = el(c.kind); m.preload = 'auto'; m.muted = true;
        m.onloadedmetadata = () => finish('load', {duration: m.duration}); m.onerror = () => finish('error');
        m.src = c.url;
      } else if (c.kind === 'frame') {
        const f = el('iframe', {src: c.url}); f.onload = () => finish('load'); f.onerror = () => finish('error');
        document.body.appendChild(f);
      } else {  // xhr: fetch with CORS, fetch with credentials, and XMLHttpRequest with credentials
        (async () => {
          try {
            const a = await fetch(c.url);
            const t = await a.text();
            const b = await fetch(c.url, {credentials: 'include'});
            const x = await new Promise((res, rej) => { const r = new XMLHttpRequest(); r.open('GET', c.url);
              r.withCredentials = true; r.onload = () => res(r.status + ':' + r.responseText.length);
              r.onerror = () => rej(new Error('xhr')); r.send(); });
            finish('load', {fetch: a.status, cred: b.status, xhr: x, len: t.length, text: t.slice(0, 60)});
          } catch (e) { finish('error', {msg: String(e).slice(0, 80)}); }
        })();
      }
    } catch (e) { finish('exception', {msg: String(e).slice(0, 80)}); }
  });
  await settle;
}
(async () => {
  for (let i = 0; i < cases.length; i++) { await run(i, cases[i]); }
  const v = document.createElement('video');
  results.diag = {h264: v.canPlayType('video/mp4; codecs="avc1.42E01E"'), vp9mp4: v.canPlayType('video/mp4; codecs="vp09.00.10.08"'), av1mp4: v.canPlayType('video/mp4; codecs="av01.0.00M.08"'), vp9webm: v.canPlayType('video/webm; codecs="vp9"'), mp3: document.createElement('audio').canPlayType('audio/mpeg')};
  await fetch('/report', {method: 'POST', body: JSON.stringify(results)});
})();
</script></body>"""


class H(http.server.BaseHTTPRequestHandler):
    report = None

    def do_GET(self):
        body = (PAGE % json.dumps(cases)).encode()
        self.send_response(200); self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        H.report = json.loads(self.rfile.read(n))
        self.send_response(200); self.send_header("Content-Length", "0"); self.end_headers()

    def log_message(self, *a):
        pass


srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()


def run(label, extra):
    H.report = None
    prof = r"D:\zeph_perf\standin_prof"
    seed_profile(prof, workspaces=False, bookmarks=False)
    dst = os.path.join(prof, "ZephyrusAdBlock")
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(os.path.join("D:" + os.sep, "zeph_perf", "abc_prof_on", "ZephyrusAdBlock"), dst)
    os.remove(os.path.join(dst, "filters.zidx"))
    with open(os.path.join(dst, "filters.txt"), "a", encoding="utf-8", newline="\n") as f:
        f.write("\n! ===== https://raw.githubusercontent.com/uBlockOrigin/uAssets/master/filters/test.txt =====\n")
        f.write("\n".join(rules) + "\n")
    b = Browser(prof, args=["--enable-logging", "--log-file=" + os.path.join("D:" + os.sep, "zeph_perf", "standins.log"),
                            "--v=1"] + extra, urls=(f"http://127.0.0.1:{PORT}/",), wait=30)
    deadline = time.time() + 150
    while H.report is None and time.time() < deadline:
        time.sleep(1)
    b.close()
    print(f"== {label}: {'report received' if H.report is not None else 'NO REPORT'}")
    return H.report or {}


def judge(report, expect_ok):
    bad = []
    counts = {"REAL": [0, 0], "ALL": [0, 0]}
    for i, c in enumerate(cases):
        if c.get("mismatch"):
            continue
        if c["kind"] == "frame" and not expect_ok:
            continue  # a blocked frame still fires `load` (it shows an error page): not a signal
        r = report.get(str(i), {"status": "missing"})
        good = r.get("status") == "load" and r.get("api") is not False
        if c["kind"] == "xhr" and good:
            good = r.get("fetch") == 200 and r.get("cred") == 200 and r.get("xhr", "").startswith("200:")
            if c["text"] is not None:
                good = good and r.get("text") == c["text"]
        if c["kind"] == "image" and good:
            good = (r.get("w") or 0) >= 1
        counts[c["set"]][0] += good
        counts[c["set"]][1] += 1
        if good != expect_ok:
            bad.append((c["set"], c["name"], c["kind"], r))
    return counts, bad


on = run("stand-ins ON (default)", [])
off = run("stand-ins OFF (--disable-features=ZephyrusAdblockStandIns)", ["--disable-features=ZephyrusAdblockStandIns"])
on_counts, on_bad = judge(on, True)
off_counts, off_bad = judge(off, False)
print("codec support:", on.get("diag"))
print("ON : served correctly -- ALL %d/%d, REAL %d/%d" % (on_counts["ALL"][0], on_counts["ALL"][1],
                                                          on_counts["REAL"][0], on_counts["REAL"][1]))
for b in on_bad:
    print("   NOT OK:", b)
print("OFF: still working (should be 0) -- ALL %d/%d, REAL %d/%d" % (off_counts["ALL"][0], off_counts["ALL"][1],
                                                                   off_counts["REAL"][0], off_counts["REAL"][1]))
for b in off_bad[:10]:
    print("   LEAKED:", b)
ok = not on_bad and not off_bad and len(on) > 0
print("PASS" if ok else "FAIL")

#!/usr/bin/env python3
"""The fingerprint ruler: what each browser tells a page about the machine, context by context.

  fp_ruler.py [out dir=D:\\zeph_perf\\fpruler]

A local page reads the usual fingerprinting surfaces (canvas, WebGL, audio, fonts, screen, navigator,
element sizes) in a document, a dedicated worker, a shared worker and a service worker, on two
origins (127.0.0.1 and localhost), twice in one browser session and again in a second session.

Arms: Zephyrus; Zephyrus with randomisation switched off (the machine's true values); Brave.
"hidden" is always measured against that true-value arm, so for Brave a "yes" on a value every
browser words differently (the user agent) says nothing about randomisation.

For every surface it answers the questions a fingerprint defence has to get right:

  hidden      does the page get something other than the true value?
  steady      same origin, same session, read twice: the same answer? (if not, averaging recovers the truth)
  per-origin  do two sites get different answers? (if not, they can still link the visitor)
  per-session does a new session get a new answer? (if not, the value is a permanent identifier)
  agrees      do the worker contexts give the document's answer? (the last column is a dedicated
              worker started BY the shared worker) (if not, the lie is detectable, and the
              worker hands out the true value)

Everything runs on the hidden desktop shield_ruler.py uses; nothing appears on screen.
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

PORT = 8791

COMMON_JS = r"""
function fnv(bytes) { let h = 0x811c9dc5; for (let i = 0; i < bytes.length; i++) { h ^= bytes[i]; h = Math.imul(h, 0x01000193) >>> 0; } return h.toString(16); }
function fnvs(s) { return fnv(new TextEncoder().encode(String(s))); }
function drawOn(ctx, w, h) {
  ctx.fillStyle = '#f60'; ctx.fillRect(10, 10, 100, 40);
  ctx.fillStyle = '#069'; ctx.font = '16px Arial'; ctx.fillText('Zephyrus fp \u{1F600} Cwm fjord', 4, 30);
  ctx.strokeStyle = 'rgba(120,20,200,0.6)'; ctx.beginPath(); ctx.arc(60, 40, 25, 0, 5); ctx.stroke();
  const g = ctx.createLinearGradient(0, 0, w, h); g.addColorStop(0, '#0f0'); g.addColorStop(1, '#00f');
  ctx.fillStyle = g; ctx.fillRect(120, 5, 60, 50);
}
function offscreenRead() {
  if (typeof OffscreenCanvas === 'undefined') return null;
  const c = new OffscreenCanvas(200, 64); const ctx = c.getContext('2d'); drawOn(ctx, 200, 64);
  return fnv(ctx.getImageData(0, 0, 200, 64).data);
}
async function offscreenBlob() {
  if (typeof OffscreenCanvas === 'undefined') return null;
  const c = new OffscreenCanvas(200, 64); const ctx = c.getContext('2d'); drawOn(ctx, 200, 64);
  const b = await c.convertToBlob(); return fnv(new Uint8Array(await b.arrayBuffer()));
}
function glRead(canvas) {
  const gl = canvas.getContext('webgl'); if (!gl) return {};
  const vs = gl.createShader(gl.VERTEX_SHADER); gl.shaderSource(vs, 'attribute vec2 p;void main(){gl_Position=vec4(p,0,1);}'); gl.compileShader(vs);
  const fs = gl.createShader(gl.FRAGMENT_SHADER); gl.shaderSource(fs, 'precision mediump float;void main(){gl_FragColor=vec4(sin(gl_FragCoord.x*0.31),cos(gl_FragCoord.y*0.17),0.5,1.0);}'); gl.compileShader(fs);
  const pr = gl.createProgram(); gl.attachShader(pr, vs); gl.attachShader(pr, fs); gl.linkProgram(pr); gl.useProgram(pr);
  const b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1,1,-1,0,1]), gl.STATIC_DRAW);
  const loc = gl.getAttribLocation(pr, 'p'); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  const px = new Uint8Array(64 * 64 * 4); gl.readPixels(0, 0, 64, 64, gl.RGBA, gl.UNSIGNED_BYTE, px);
  const ext = gl.getExtension('WEBGL_debug_renderer_info');
  return { webgl_pixels: fnv(px),
           webgl_vendor: ext ? fnvs(gl.getParameter(ext.UNMASKED_VENDOR_WEBGL) + '|' + gl.getParameter(ext.UNMASKED_RENDERER_WEBGL)) : fnvs(gl.getParameter(gl.VENDOR) + '|' + gl.getParameter(gl.RENDERER)),
           webgl_params: fnvs([gl.getParameter(gl.MAX_TEXTURE_SIZE), gl.getParameter(gl.MAX_VIEWPORT_DIMS), gl.getSupportedExtensions()].join('|')) };
}
function nav() {
  const n = self.navigator;
  return { hardware_concurrency: String(n.hardwareConcurrency), device_memory: String(n.deviceMemory),
           user_agent: fnvs(n.userAgent), platform: String(n.platform), languages: String(n.languages) };
}
async function videoFrameRead() {
  if (typeof VideoFrame === 'undefined' || typeof OffscreenCanvas === 'undefined') return null;
  try {
    const c = new OffscreenCanvas(200, 64); const ctx = c.getContext('2d'); drawOn(ctx, 200, 64);
    const f = new VideoFrame(c, {timestamp: 0}); const buf = new Uint8Array(f.allocationSize({format: 'RGBA'}));
    await f.copyTo(buf, {format: 'RGBA'}); f.close(); return fnv(buf);
  } catch (e) { return 'error'; }
}
async function workerSurfaces() {
  const out = Object.assign({}, nav());
  out.offscreen_pixels = offscreenRead();
  out.offscreen_encoded = await offscreenBlob();
  out.video_frame = await videoFrameRead();
  if (typeof OffscreenCanvas !== 'undefined') { try { Object.assign(out, glRead(new OffscreenCanvas(64, 64))); } catch (e) { out.webgl_error = String(e); } }
  return out;
}
"""

PAGE_HTML = """<!doctype html><meta charset=utf-8><title>fp</title>
<span id=s style="font:17px Georgia">Sphinx of black quartz, judge my vow 0123456789</span>
<script src=common.js></script>
<script>
async function audio() {
  const C = self.OfflineAudioContext || self.webkitOfflineAudioContext; if (!C) return null;
  const ctx = new C(1, 5000, 44100); const o = ctx.createOscillator(); o.type = 'triangle'; o.frequency.value = 10000;
  const comp = ctx.createDynamicsCompressor(); o.connect(comp); comp.connect(ctx.destination); o.start(0);
  const buf = await ctx.startRendering(); const d = buf.getChannelData(0);
  let s = 0; for (let i = 4000; i < 5000; i++) s += Math.abs(d[i]); return String(s);
}
function fonts() {
  const c = document.createElement('canvas').getContext('2d'); const out = [];
  for (const f of ['Arial','Calibri','Cambria','Consolas','Courier New','Georgia','Impact','Segoe UI','Tahoma','Times New Roman','Verdana','Nirmala UI','Mangal','Comic Sans MS','serif','sans-serif','monospace','system-ui'])
    { c.font = '20px "' + f + '", monospace'; out.push(c.measureText('mmmmmmmmmmlli WwQq 0123').width); }
  return fnvs(out.join(','));
}
function ask(worker, port) { return new Promise((res) => { const t = setTimeout(() => res({error: 'timeout'}), 8000); (port || worker).onmessage = (e) => { clearTimeout(t); res(e.data); }; (port || worker).postMessage('go'); }); }
async function collect() {
  const out = {};
  const d = Object.assign({}, nav());
  const c = document.createElement('canvas'); c.width = 200; c.height = 64; const ctx = c.getContext('2d'); drawOn(ctx, 200, 64);
  d.canvas_pixels = fnv(ctx.getImageData(0, 0, 200, 64).data);
  d.canvas_encoded = fnvs(c.toDataURL());
  // A canvas large enough to be drawn on the GPU, whose pixels come back as a texture.
  const big = document.createElement('canvas'); big.width = 1400; big.height = 900; const bctx = big.getContext('2d'); drawOn(bctx, 1400, 900);
  d.big_canvas_encoded = fnvs(big.toDataURL());
  d.big_canvas_blob = await new Promise((res) => big.toBlob(async (b) => res(fnv(new Uint8Array(await b.arrayBuffer())))));
  d.offscreen_pixels = offscreenRead();
  d.offscreen_encoded = await offscreenBlob();
  d.video_frame = await videoFrameRead();
  const g = document.createElement('canvas'); g.width = 64; g.height = 64; Object.assign(d, glRead(g));
  d.audio = await audio();
  d.fonts = fonts();
  const r = document.getElementById('s').getBoundingClientRect(); d.element_size = r.width + 'x' + r.height;
  d.screen = [screen.width, screen.height, screen.availWidth, screen.availHeight, screen.colorDepth, devicePixelRatio].join(',');
  // A changed screen must not be caught out by the media queries or the window.
  d.screen_consistent = String(matchMedia('(device-width: ' + screen.width + 'px)').matches && matchMedia('(device-height: ' + screen.height + 'px)').matches && outerWidth <= screen.width && outerHeight <= screen.height);
  d.plugins = fnvs([...navigator.plugins].map(p => p.name).join('|'));
  d.timezone = Intl.DateTimeFormat().resolvedOptions().timeZone;
  out.document = d;
  try { out.dedicated = await ask(new Worker('worker.js')); } catch (e) { out.dedicated = {error: String(e)}; }
  try { const sw = new SharedWorker('shared.js'); sw.port.start(); out.shared = await ask(null, sw.port); } catch (e) { out.shared = {error: String(e)}; }
  try {
    const reg = await navigator.serviceWorker.register('sw.js'); await navigator.serviceWorker.ready;
    const w = reg.active || reg.waiting || reg.installing;
    out.service = await new Promise((res) => { const t = setTimeout(() => res({error: 'timeout'}), 8000);
      navigator.serviceWorker.onmessage = (e) => { clearTimeout(t); res(e.data); }; w.postMessage('go'); });
  } catch (e) { out.service = {error: String(e)}; }
  return JSON.stringify(out);
}
</script>"""

FILES = {
    "/page.html": ("text/html", PAGE_HTML),
    "/common.js": ("text/javascript", COMMON_JS),
    "/worker.js": ("text/javascript", "importScripts('common.js'); onmessage = async () => postMessage(await workerSurfaces());"),
    "/shared.js": ("text/javascript", "importScripts('common.js'); onconnect = (e) => { const p = e.ports[0]; p.onmessage = async () => { const mine = await workerSurfaces(); try { const w = new Worker('worker.js'); mine.__nested = await new Promise((res) => { const t = setTimeout(() => res({error: 'timeout'}), 6000); w.onmessage = (m) => { clearTimeout(t); res(m.data); }; w.postMessage('go'); }); } catch (x) { mine.__nested = {error: String(x)}; } p.postMessage(mine); }; };"),
    "/sw.js": ("text/javascript", "importScripts('common.js'); self.addEventListener('install', () => self.skipWaiting()); self.addEventListener('activate', (e) => e.waitUntil(clients.claim())); onmessage = async (e) => e.source.postMessage(await workerSurfaces());"),
}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        hit = FILES.get(self.path.split("?")[0])
        if not hit:
            self.send_response(404)
            self.end_headers()
            return
        body = hit[1].encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", hit[0] + "; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


ARMS = [
    dict(name="zephyrus", exe=R.ZEPH, port=9431, zeph=True, prefs=None),
    dict(name="truth", exe=R.ZEPH, port=9432, zeph=True, prefs=None,
         no_features=["ZephyrusPrivacyFingerprintRandomization"]),
    dict(name="brave", exe=R.BRAVE, port=9433, zeph=False, prefs=None),
]

CONTEXTS = ("document", "dedicated", "shared", "service", "nested")


def read_page(cdp, session, origin):
    cdp.go(origin + "/page.html", 15)
    time.sleep(0.5)
    got = cdp.call("Runtime.evaluate", session, wait=40, expression="collect()", awaitPromise=True,
                   returnByValue=True)
    try:
        out = json.loads(got["result"]["value"])
        # A dedicated worker started by the shared worker, reported as its own context.
        out["nested"] = (out.get("shared") or {}).pop("__nested", {"error": "none"})
        return out
    except (KeyError, TypeError, ValueError):
        return {"document": {"error": str(got)[:200]}}


def session_of(arm, work):
    """One browser session: both origins, each read twice."""
    p, _ = R.launch(arm, work)
    time.sleep(18)
    try:
        cdp = R.Cdp(arm["port"])
        s = cdp.the_tab()
        out = {}
        for origin in ("http://127.0.0.1:%d" % PORT, "http://localhost:%d" % PORT):
            out[origin] = [read_page(cdp, s, origin), read_page(cdp, s, origin)]
        return out
    finally:
        subprocess.run(["taskkill", "/PID", str(p.pid), "/T", "/F"], capture_output=True)
        time.sleep(2)


def report(data):
    a, b = "http://127.0.0.1:%d" % PORT, "http://localhost:%d" % PORT
    truth = data["truth"][0][a][0]
    for arm in ("zephyrus", "brave"):
        if arm not in data:
            continue
        s1, s2 = data[arm]
        doc = s1[a][0]["document"]
        print("\n%s" % arm)
        print("  %-22s %-7s %-7s %-10s %-11s %s" % ("surface (document)", "hidden", "steady", "per-origin", "per-session", "workers: dedicated / shared / service / dedicated-in-shared"))
        for key in doc:
            if key == "error":
                continue
            v = doc.get(key)
            hidden = v != truth["document"].get(key)
            steady = v == s1[a][1]["document"].get(key)
            per_origin = v != s1[b][0]["document"].get(key)
            per_session = v != s2[a][0]["document"].get(key)
            cells = []
            for ctx in CONTEXTS[1:]:
                w = s1[a][0].get(ctx) or {}
                if "error" in w:
                    cells.append("n/a")
                elif key not in w or w[key] is None:
                    cells.append("-")
                elif w[key] == v:
                    cells.append("agrees")
                elif w[key] == (truth.get(ctx) or {}).get(key) and hidden:
                    cells.append("TRUE VALUE")
                else:
                    cells.append("differs")
            print("  %-22s %-7s %-7s %-10s %-11s %s" % (
                key, "yes" if hidden else "no", "yes" if steady else "NO", "yes" if per_origin else "no",
                "yes" if per_session else "no", " / ".join(cells)))
        for ctx in CONTEXTS[1:]:
            w = s1[a][0].get(ctx) or {}
            if "error" in w:
                print("  (%s worker: %s)" % (ctx, w["error"][:80]))


def main():
    work = sys.argv[1] if len(sys.argv) > 1 else r"D:\zeph_perf\fpruler"
    os.makedirs(work, exist_ok=True)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    data = {}
    for arm in ARMS:
        data[arm["name"]] = [session_of(arm, work), session_of(arm, work)]
    json.dump(data, open(os.path.join(work, "fp_%s.json" % time.strftime("%Y%m%d_%H%M%S")), "w"), indent=1)
    report(data)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "report":
        report(json.load(open(sys.argv[2])))
    else:
        main()

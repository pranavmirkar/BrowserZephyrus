// Targeted probe for the §6.5 risk the corpus run cannot see by itself.
//
// A clean A/B run says "no site complained". It does NOT say "the withheld
// extensions were exercised" — a page that loads and renders may simply never
// have asked for the twelve extensions §6.5 withholds. Absence of complaint from
// code that never ran is not evidence.
//
// So ask the real sites directly, on the real renderer: what does
// getSupportedExtensions() return in each arm, which of the withheld twelve
// actually disappeared, and does getExtension() agree with the list.
//
//   node probe_webgl.mjs --arm=off
//   node probe_webgl.mjs --arm=on

import { spawn } from 'node:child_process';

const CHROME = process.env.ZCHROME || 'D:/chromium/src/out/Release/chrome.exe';
const PORT = Number(process.env.ZPORT || 9570);
const ARM = process.argv.includes('--arm=on') ? 'on' : 'off';
const sleep = ms => new Promise(r => setTimeout(r, ms));

const PAGES = ['https://example.com/'];  // one real origin is enough

const WITHHELD = [
  'WEBGL_debug_shaders', 'WEBGL_compressed_texture_s3tc_srgb',
  'WEBGL_compressed_texture_etc', 'WEBGL_compressed_texture_etc1',
  'WEBGL_compressed_texture_astc', 'WEBGL_compressed_texture_pvrtc',
  'WEBGL_compressed_texture_atc', 'EXT_disjoint_timer_query',
  'EXT_disjoint_timer_query_webgl2', 'KHR_parallel_shader_compile',
  'OVR_multiview2', 'WEBGL_multi_draw',
];

const flags = [
  `--user-data-dir=D:/zprobe-${ARM}`, '--no-first-run',
  '--no-default-browser-check', `--remote-debugging-port=${PORT}`,
  'about:blank',
];
if (ARM === 'on') {
  const mask = process.env.ZSURFACES;
  const params = mask
    ? `ZephyrusPrivacyFingerprintRandomization:surfaces/${mask}`
    : 'ZephyrusPrivacyFingerprintRandomization';
  flags.unshift(`--enable-features=ZephyrusPrivacyIntelligence,${params}`);
}
const proc = spawn(CHROME, flags, { detached: true, stdio: 'ignore' });
proc.unref();

let wsUrl;
for (let i = 0; i < 60 && !wsUrl; i++) {
  try {
    const v = await (await fetch(`http://127.0.0.1:${PORT}/json/version`)).json();
    wsUrl = v.webSocketDebuggerUrl;
  } catch {}
  if (!wsUrl) await sleep(500);
}
const ws = new WebSocket(wsUrl);
await new Promise(r => ws.onopen = r);
let id = 0; const pending = new Map();
ws.onmessage = e => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
};
const send = (method, params = {}, sessionId, ms = 20000) => new Promise(res => {
  const i = ++id;
  const to = setTimeout(() => { if (pending.delete(i)) res({ timedOut: true }); }, ms);
  pending.set(i, m => { clearTimeout(to); res(m.result ?? {}); });
  ws.send(JSON.stringify({ id: i, method, params, ...(sessionId ? { sessionId } : {}) }));
});

const EXPR = `(() => {
  const c = document.createElement('canvas');
  const gl = c.getContext('webgl2') || c.getContext('webgl');
  if (!gl) return JSON.stringify({ webgl: false });
  const listed = gl.getSupportedExtensions() || [];
  const withheld = ${JSON.stringify(WITHHELD)};
  const disagree = [];
  for (const n of new Set([...listed, ...withheld])) {
    const handedOut = gl.getExtension(n) !== null;
    if (handedOut !== listed.includes(n)) {
      disagree.push(n + (handedOut ? ' (hidden but handed out)' : ' (listed but withheld)'));
    }
  }
  const cc = document.createElement('canvas'); cc.width=64; cc.height=64;
  const cx = cc.getContext('2d');
  cx.fillStyle='#4477aa'; cx.fillRect(0,0,64,64);
  cx.fillStyle='#f60'; cx.fillRect(8,8,20,20);
  const dd = cx.getImageData(0,0,64,64).data;
  let ch=2166136261>>>0;
  for (let i=0;i<dd.length;i++){ch^=dd[i];ch=Math.imul(ch,16777619)>>>0;}
  return JSON.stringify({
    canvasHash: ch.toString(16),
    webgl: true,
    version: gl.getParameter(gl.VERSION),
    count: listed.length,
    withheldPresent: withheld.filter(n => listed.includes(n)),
    listed,
    disagree,
  });
})()`;

const out = {};
for (const url of PAGES) {
  const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
  await send('Page.enable', {}, sessionId);
  if (url !== 'about:blank') {
    await send('Page.navigate', { url }, sessionId);
    await sleep(9000);
  }
  const r = await send('Runtime.evaluate',
    { expression: EXPR, returnByValue: true }, sessionId);
  out[url] = r?.result?.value ? JSON.parse(r.result.value) : { error: 'no result' };
  await send('Target.closeTarget', { targetId });
}

console.log(JSON.stringify({ arm: ARM, surfaces: process.env.ZSURFACES || 'all', pages: out }, null, 1));
ws.close();
try { process.kill(proc.pid); } catch {}
process.exit(0);

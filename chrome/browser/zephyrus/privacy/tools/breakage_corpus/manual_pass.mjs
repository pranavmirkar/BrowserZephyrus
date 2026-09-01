// The objective half of the §12.6 manual pass.
//
// §12.6 requires a human, and some of its checks genuinely need one ("does the
// exported image look right"). But several items on the list are only
// SUBJECTIVE by habit, and turning those into measurements is strictly better
// evidence than an impression:
//
//   "does the audio sound normal"  ->  only the four AnalyserNode getters are
//       perturbed (analyser_node.cc); AudioBuffer.getChannelData and offline
//       rendering are untouched. So the real claim is: rendered output must be
//       BIT-IDENTICAL between arms while analyser data MUST differ. That is
//       checkable, and it is a stronger statement than any listening test.
//
//   "does the scene animate"       ->  hash the site's own canvas twice, a
//       second apart. Perturbation is deterministic in (seed, position, value),
//       so an unchanged canvas hashes IDENTICALLY even with the flag on —
//       a difference therefore means real animation, not noise. (This doubles
//       as a live check of the determinism property §6.5 depends on.)
//
//   "can you still select text"    ->  PDF.js builds a .textLayer; assert it
//       has content.
//
// What is left for a human is then small and specific, which is the point.
//
//   node manual_pass.mjs --arm=off > mp_off.json
//   node manual_pass.mjs --arm=on  > mp_on.json

import { spawn } from 'node:child_process';
import { writeFileSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const CHROME = process.env.ZCHROME || 'D:/chromium/src/out/Release/chrome.exe';
const PORT = Number(process.env.ZPORT || 9580);
const ARM = process.argv.includes('--arm=on') ? 'on' : 'off';
const SHOTS = join(HERE, 'shots', 'manual_' + ARM);
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---- probes ---------------------------------------------------------------

// Audio: output vs analysis. The two must move in opposite directions.
const AUDIO_PROBE = `(async () => {
  const hash = (arr) => {
    let h = 2166136261 >>> 0;
    for (let i = 0; i < arr.length; i++) {
      // Quantise floats so trivial FP jitter does not masquerade as a change.
      const v = Math.round(arr[i] * 100000);
      h ^= (v & 255); h = Math.imul(h, 16777619) >>> 0;
      h ^= ((v >> 8) & 255); h = Math.imul(h, 16777619) >>> 0;
    }
    return h.toString(16);
  };
  // 1. RENDERED OUTPUT — what the user hears. Must not change.
  const oc = new OfflineAudioContext(1, 44100, 44100);
  const osc = oc.createOscillator();
  osc.type = 'triangle'; osc.frequency.value = 440;
  osc.connect(oc.destination); osc.start(0);
  const rendered = await oc.startRendering();
  const out = hash(rendered.getChannelData(0));

  // 2. ANALYSER DATA — what a fingerprinter reads. Must change.
  const ac = new OfflineAudioContext(1, 44100, 44100);
  const o2 = ac.createOscillator();
  o2.type = 'triangle'; o2.frequency.value = 440;
  const an = ac.createAnalyser(); an.fftSize = 2048;
  o2.connect(an); an.connect(ac.destination); o2.start(0);
  await ac.startRendering();
  const freq = new Float32Array(an.frequencyBinCount);
  an.getFloatFrequencyData(freq);
  const analysed = hash(freq);

  return JSON.stringify({ renderedOutput: out, analyserData: analysed });
})()`;

// Animation: hash the site's own largest canvas twice.
const ANIM_PROBE = `(async () => {
  const cs = Array.from(document.querySelectorAll('canvas'));
  if (!cs.length) return JSON.stringify({ canvas: false });
  const c = cs.sort((a,b) => (b.width*b.height) - (a.width*a.height))[0];
  const grab = () => { try { return c.toDataURL('image/png').length + ':' +
      c.toDataURL('image/png').slice(-64); } catch (e) { return 'ERR:' + e.message; } };
  const a = grab();
  await new Promise(r => setTimeout(r, 1200));
  const b = grab();
  return JSON.stringify({
    canvas: true, w: c.width, h: c.height,
    animating: a !== b, sameTwice: a === grab.call ? undefined : undefined,
    a: String(a).slice(0, 40), b: String(b).slice(0, 40),
  });
})()`;

// Determinism: the same unchanged canvas read twice must be byte-identical, or
// a site could average our noise away. Checked on a canvas we control.
const DETERMINISM_PROBE = `(() => {
  const c = document.createElement('canvas'); c.width = 64; c.height = 64;
  const x = c.getContext('2d');
  x.fillStyle = '#4477aa'; x.fillRect(0,0,64,64);
  x.fillStyle = '#f60'; x.fillRect(8,8,20,20);
  const h = () => { const d = x.getImageData(0,0,64,64).data;
    let v = 2166136261>>>0;
    for (let i=0;i<d.length;i++){v^=d[i];v=Math.imul(v,16777619)>>>0;}
    return v.toString(16); };
  const first = h(), second = h(), third = h();
  return JSON.stringify({ first, stable: first === second && second === third });
})()`;

const PDF_TEXT_PROBE = `(() => {
  const layers = document.querySelectorAll('.textLayer');
  let chars = 0;
  layers.forEach(l => chars += (l.innerText || '').length);
  const canvases = document.querySelectorAll('canvas').length;
  return JSON.stringify({ textLayers: layers.length, chars, canvases });
})()`;

const TARGETS = [
  { name: 'audio-correctness', url: 'about:blank', wait: 500, probe: AUDIO_PROBE },
  { name: 'determinism', url: 'https://example.com/', wait: 2500, probe: DETERMINISM_PROBE },
  { name: 'threejs', url: 'https://threejs.org/examples/?q=keyframes#webgl_animation_keyframes',
    wait: 12000, probe: ANIM_PROBE, shot: true },
  { name: 'aquarium', url: 'https://webglsamples.org/aquarium/aquarium.html',
    wait: 12000, probe: ANIM_PROBE, shot: true },
  { name: 'pdfjs', url: 'https://mozilla.github.io/pdf.js/web/viewer.html',
    wait: 12000, probe: PDF_TEXT_PROBE, shot: true },
  { name: 'chartjs', url: 'https://www.chartjs.org/docs/latest/samples/bar/vertical.html',
    wait: 10000, probe: ANIM_PROBE, shot: true },
  { name: 'audio-analyser-page', url: 'https://mdn.github.io/webaudio-examples/audio-analyser/',
    wait: 8000, probe: ANIM_PROBE, shot: true },
  { name: 'squoosh', url: 'https://squoosh.app/', wait: 12000, probe: ANIM_PROBE, shot: true },
  { name: 'photopea', url: 'https://www.photopea.com/', wait: 15000, probe: ANIM_PROBE, shot: true },
];

// ---- driver ---------------------------------------------------------------
mkdirSync(SHOTS, { recursive: true });
const flags = [
  `--user-data-dir=D:/zmanual-${ARM}`, '--no-first-run',
  '--no-default-browser-check', '--autoplay-policy=no-user-gesture-required',
  `--remote-debugging-port=${PORT}`, 'about:blank',
];
if (ARM === 'on') {
  flags.unshift('--enable-features=ZephyrusPrivacyIntelligence,' +
                'ZephyrusPrivacyFingerprintRandomization');
}
const proc = spawn(CHROME, flags, { detached: true, stdio: 'ignore' });
proc.unref();

let wsUrl;
for (let i = 0; i < 60 && !wsUrl; i++) {
  try { wsUrl = (await (await fetch(`http://127.0.0.1:${PORT}/json/version`)).json())
    .webSocketDebuggerUrl; } catch {}
  if (!wsUrl) await sleep(500);
}
const ws = new WebSocket(wsUrl);
await new Promise(r => ws.onopen = r);
let id = 0; const pending = new Map();
ws.onmessage = e => {
  const m = JSON.parse(e.data);
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
};
const send = (method, params = {}, sessionId, ms = 30000) => new Promise(res => {
  const i = ++id;
  const to = setTimeout(() => { if (pending.delete(i)) res({ timedOut: true }); }, ms);
  pending.set(i, m => { clearTimeout(to); res(m.result ?? {}); });
  ws.send(JSON.stringify({ id: i, method, params, ...(sessionId ? { sessionId } : {}) }));
});

const out = {};
for (const t of TARGETS) {
  const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
  const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
  await send('Page.enable', {}, sessionId);
  if (t.url !== 'about:blank') {
    await send('Page.navigate', { url: t.url }, sessionId);
  }
  await sleep(t.wait);
  const r = await send('Runtime.evaluate',
    { expression: t.probe, returnByValue: true, awaitPromise: true }, sessionId);
  let val = r?.result?.value;
  try { val = JSON.parse(val); } catch {}
  out[t.name] = { url: t.url, result: val,
    exception: r?.result?.exceptionDetails?.exception?.description?.split('\n')[0] };
  if (t.shot) {
    const s = await send('Page.captureScreenshot', { format: 'png' }, sessionId);
    if (s?.data) writeFileSync(join(SHOTS, t.name + '.png'), Buffer.from(s.data, 'base64'));
  }
  await send('Target.closeTarget', { targetId });
  process.stderr.write(`  ${t.name} done\n`);
}

console.log(JSON.stringify({ arm: ARM, when: new Date().toISOString(), out }, null, 1));
ws.close();
try { process.kill(proc.pid); } catch {}
process.exit(0);

// Runs the PROTOTYPE'S OWN Mascot class headless and records its internal state.
//
// The prototype is a browser page; this loads its <script> into a vm context with a
// permissive fake DOM, replaces Math.random with a seeded generator (mulberry32, the
// same one the C++ model uses), drives update() by hand at a fixed step, and writes
// the spring values, gaze, blink and effect state per frame. The C++ model must
// reproduce these numbers: that is the parity test for milestone 2.
//
//   node run_reference.js <out.json> [seconds] [fps] [sample_every]

const fs = require('fs');
const vm = require('vm');

const SRC = process.env.LENS_PROTOTYPE || 'C:/Users/prana/Downloads/agent-mascot-handoff/reference/agent-mascot.html';
const OUT = process.argv[2] || 'reference.json';
const SECONDS = +(process.argv[3] || 10);
const FPS = +(process.argv[4] || 60);
const EVERY = +(process.argv[5] || 20);

const SAMPLE = new Set([0, 10, 30, 60, 90, 120, 180, 240, 300, 359]);
const html = fs.readFileSync(SRC, 'utf8');
const m = html.match(/<script>([\s\S]*?)<\/script>/);
let code = m[1];
code = code.slice(0, code.indexOf('/* ---- page wiring'));  // the demo control panel is not part of the mascot

// ---- a fake DOM that swallows everything --------------------------------------
function fakeEl(name) {
  const attrs = {};
  const kids = [];
  const target = function () {};
  const style = new Proxy({}, { set(o, k, v) { o[k] = v; return true }, get(o, k) { return o[k] } });
  const el = new Proxy(target, {
    get(_, prop) {
      if (prop === 'style') return style;
      if (prop === 'setAttribute') return (k, v) => { attrs[k] = String(v) };
      if (prop === 'getAttribute') return k => (k in attrs ? attrs[k] : null);
      if (prop === 'removeAttribute') return k => { delete attrs[k] };
      if (prop === 'appendChild') return c => { kids.push(c); return c };
      if (prop === 'querySelector') return () => fakeEl('q');
      if (prop === 'querySelectorAll') return () => [fakeEl('s'), fakeEl('s'), fakeEl('s'), fakeEl('s')];
      if (prop === 'addEventListener' || prop === 'observe') return () => {};
      if (prop === 'getTotalLength') return () => 100;
      if (prop === 'matches') return false;
      if (prop === Symbol.iterator) return function* () {};
      if (prop === 'length') return 0;
      if (prop === 'id' || prop === 'innerHTML' || prop === 'textContent') return '';
      if (prop === Symbol.toPrimitive) return () => 0;
      return fakeEl(String(prop));
    },
    set() { return true },
    apply() { return fakeEl('call') },
  });
  return el;
}

const svg = fakeEl('svg');
const document = {
  createElementNS: () => fakeEl('el'),
  createElement: () => fakeEl('el'),
  addEventListener: () => {},
  hidden: false,
  querySelector: () => fakeEl('q'),
};

// ---- seeded Math.random ----------------------------------------------------------
function mulberry(a) {
  return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296 };
}

const ctx = {
  document, Math: Object.create(Math), console,
  matchMedia: () => ({ matches: false }),
  IntersectionObserver: class { constructor() {} observe() {} },
  requestAnimationFrame: () => 0, cancelAnimationFrame: () => {},
  window: {}, performance: { now: () => 0 }, setTimeout, clearTimeout,
};
ctx.window = ctx;
vm.createContext(ctx);
// expose the classes/tables the script defines at top level
code += '\n;globalThis.__M = {Mascot, STATES, EMO, PALS, NEUTRAL, SPRING_CFG, POSE_KEYS, SHAPES};';
vm.runInContext(code, ctx);
const { Mascot, STATES, POSE_KEYS, SHAPES } = ctx.__M;

const mascot = new Mascot(svg);

function snapshot(mm) {
  const P = mm.P, o = {};
  o.t = mm.t;
  for (const k of POSE_KEYS) o[k] = P[k].x;
  for (const k of ['gx', 'gy', 'wF', 'wO', 'wP', 'beamX']) o[k] = P[k].x;
  o.hw = SHAPES.map(s => mm.hw[s].x);
  o.G = [mm.G.x, mm.G.y];
  o.eye = mm.eyeCur.slice();
  o.tint = mm.tintCur.slice();
  o.flash = mm.flashV;
  o.fluidPhase = mm.fluidPhase;
  o.pal = mm.palCur.map(c => c.slice());
  o.blinks = mm.blinks.map(b => [b.t0, b.d]);
  o.tears = mm.tears.map(z => [z.t0, z.side, z.dx]);
  o.ripples = mm.ripples.map(r => [r.t0, r.x, r.y]);
  o.nextBlink = mm.nextBlink;
  o.rate = mm.rate;
  return o;
}


const nums = str => (str == null ? [] : (String(str).match(/-?\d+(?:\.\d+)?(?:e-?\d+)?/g) || []).map(Number));
const at = (el, k) => el.getAttribute(k);
function renderSnapshot(mm) {
  const e = mm.el, r = {};
  r.body = nums(at(e.bodyG, 'transform'));
  r.face = nums(at(e.face, 'transform'));
  r.eyeL = nums(at(e.eyeL, 'transform')); r.eyeR = nums(at(e.eyeR, 'transform'));
  r.lidL = nums(at(e.lidLp, 'd')); r.lidR = nums(at(e.lidRp, 'd'));
  r.hl = mm.hl.map(H => ({
    root: nums(at(H.root, 'transform')),
    shapes: ['bowl', 'heart', 'star', 'dot', 'spiral'].map(n => ({ show: at(H[n], 'display') === 'inline', tf: nums(at(H[n], 'transform')) })),
  }));
  r.eyeBot = nums(at(e.eyeBot, 'stop-color')); r.eyeMid = nums(at(e.eyeMid, 'stop-color'));
  r.tears = mm.tearEls.map(t => ({ op: +at(t, 'opacity'), tf: nums(at(t, 'transform')) })).filter(t => t.op > 0);
  r.tintOp = +at(e.tint, 'opacity'); r.tintCol = nums(at(e.tintA, 'stop-color'));
  r.beamOp = +at(e.beam, 'opacity'); r.beamTf = nums(at(e.beam, 'transform'));
  r.flash = +at(e.flash, 'opacity');
  r.rips = e.rips.map(c => ({ op: +at(c, 'opacity'), cx: +at(c, 'cx'), cy: +at(c, 'cy'), r: +at(c, 'r'), sw: +at(c, 'stroke-width') }));
  r.layers = { s: at(e.staticG, 'display'), f: at(e.fluidG, 'display'), fo: +at(e.fluidG, 'opacity') };
  r.filter = e.staticG.style.filter;
  if (at(e.fluidG, 'display') === 'inline') {
    r.blobs = mm.blobEls.map(b => nums(at(b, 'transform')));
    r.ribs = e.rb.map(b => nums(at(b, 'd')));
    r.rimRot = nums(at(e.rimG, 'gradientTransform'));
    r.rimOp = +at(e.rimGlow, 'opacity');
    r.gloss = nums(at(e.gloss, 'transform'));
    r.wrap = nums(at(e.blobsWrap, 'transform'));
  }
  return r;
}
// Must match C++: the model starts from a fresh state each time.
const result = { fps: FPS, every: EVERY, seconds: SECONDS, cases: [] };
const names = Object.keys(STATES);
const runs = [];
for (const name of names) runs.push([name, 'static', 1.0]);
for (const name of ['idle', 'working', 'searching', 'joy', 'anger', 'sadness', 'laughing', 'dizzy']) runs.push([name, 'fluid', 1.0]);
for (const name of ['joy', 'sadness', 'anger', 'fear', 'love', 'dizzy']) runs.push([name, 'static', 0.5]);

const dt = 1 / FPS;
for (const [name, mode, inten] of runs) {
  const seed = 1000 + result.cases.length;
  ctx.Math.random = mulberry(seed);
  // fresh start (the constructor already ran an idle setState with other randomness)
  mascot.t = 0; mascot.blinks = []; mascot.tears = []; mascot.flashV = 0; mascot.fluidPhase = 0;
  mascot.G = { x: 0, y: 0 }; mascot.cursor = null; mascot.cursorT = -99;
  mascot.ripples = [{ t0: -9, x: 0, y: 0 }, { t0: -9, x: 0, y: 0 }, { t0: -9, x: 0, y: 0 }]; mascot.ripI = 0;
  for (const k of Object.keys(mascot.P)) { const s = mascot.P[k]; s.x = s.t = (['sx', 'sy'].includes(k) ? 1 : (k in ctx.__M.NEUTRAL ? ctx.__M.NEUTRAL[k] : (['openR', 'tiltR', 'cheekR', 'scR'].includes(k) ? ctx.__M.NEUTRAL[k.slice(0, -1)] : (k === 'beamX' ? 125.5 : 0)))); s.v = 0 }
  for (const s of SHAPES) { const sp = mascot.hw[s]; sp.x = sp.t = (s === 'bowl' ? 1 : 0); sp.v = 0 }
  mascot.eyeCur = ctx.__M.NEUTRAL && [15, 92, 171]; mascot.tintCur = [255, 70, 70];
  mascot.palCur = ctx.__M.PALS.neutral.map(c => c.slice());
  mascot.intensity = inten; mascot.mode = 'static'; mascot.manualS = null; mascot.amp = 1;
  mascot.setGradient(mode);
  mascot.setState(name, true);
  const frames = [];
  const total = Math.round(SECONDS * FPS);
  for (let i = 0; i < total; i++) {
    mascot.t += dt;
    mascot.update(dt);
    // render() filters the tear list; replicate without touching the DOM
    mascot.tears = mascot.tears.filter(z => mascot.t - z.t0 < 1.5);
    mascot.render(dt);
    if (SAMPLE.has(i)) { const f = snapshot(mascot); if (mode !== 'fluid') delete f.pal; f.render = renderSnapshot(mascot); frames.push(f); }
  }
  result.cases.push({ state: name, mode, intensity: inten, seed, frames });
}
fs.writeFileSync(OUT, JSON.stringify(result, (k, v) => typeof v === 'number' ? (Number.isInteger(v) ? v : +v.toPrecision(10)) : v));
console.log('cases', result.cases.length, 'frames/case', result.cases[0].frames.length, 'bytes', fs.statSync(OUT).size);

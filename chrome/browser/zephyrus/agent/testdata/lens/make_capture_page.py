"""Builds capture.html: the prototype's mascot, driven deterministically.

The page contains the prototype's own <svg> markup and <script> (minus the demo
control panel), replaces requestAnimationFrame so nothing runs by itself, then
does exactly what run_reference.js does: reset to a fresh pose, seed Math.random,
enter the state from the URL, and step update()/render() 60 times a second for
`frames` frames. A screenshot taken after load is therefore a specific frame of the
prototype, the same frame the native model produces from the same seed.

  capture.html?state=joy&fill=static&frames=108&seed=4242&i=1
"""
import re
import os
HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.environ.get('LENS_WORK', os.path.join(HERE, 'work'))   # screenshots and sheets go here
os.makedirs(WORK, exist_ok=True)

SRC = os.environ.get('LENS_PROTOTYPE', 'C:/Users/prana/Downloads/agent-mascot-handoff/reference/agent-mascot.html')
OUT = os.path.join(WORK, 'capture.html')

html = open(SRC, encoding='utf-8').read()
svg = re.search(r'<svg id="mascot".*?</svg>', html, re.S).group(0)
svg = svg.replace('<svg id="mascot"', '<svg id="mascot" width="622" height="632"', 1)
script = re.search(r'<script>([\s\S]*?)</script>', html).group(1)
script = script[:script.index('/* ---- page wiring')]

capture = r"""
const q = new URLSearchParams(location.search);
const state = q.get('state'), fill = q.get('fill') || 'static';
const frames = +(q.get('frames') || 108), seed = +(q.get('seed') || 4242), inten = +(q.get('i') || 1);
function seededRandom(a) { return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296 } }
const svgEl = document.getElementById('mascot');
const m = new Mascot(svgEl);
Math.random = seededRandom(seed);
m.t = 0; m.blinks = []; m.tears = []; m.flashV = 0; m.fluidPhase = 0;
m.G = { x: 0, y: 0 }; m.cursor = null; m.cursorT = -99;
m.ripples = [{ t0: -9, x: 0, y: 0 }, { t0: -9, x: 0, y: 0 }, { t0: -9, x: 0, y: 0 }]; m.ripI = 0;
for (const k of Object.keys(m.P)) {
  const s = m.P[k];
  s.x = s.t = (['sx', 'sy'].includes(k) ? 1 : (k in NEUTRAL ? NEUTRAL[k] : (['openR', 'tiltR', 'cheekR', 'scR'].includes(k) ? NEUTRAL[k.slice(0, -1)] : (k === 'beamX' ? 125.5 : 0))));
  s.v = 0;
}
for (const s of SHAPES) { const sp = m.hw[s]; sp.x = sp.t = (s === 'bowl' ? 1 : 0); sp.v = 0 }
m.eyeCur = NEUTRAL_EYE.slice(); m.tintCur = [255, 70, 70];
m.palCur = PALS.neutral.map(c => c.slice()); m.palDirty = true;
m.intensity = inten; m.mode = 'static'; m.manualS = null; m.amp = 1; m.autoLite = false; m.lite = false;
m.setGradient(fill);
m.setState(state, true);
if (q.get('lite')) m.goLite();
const dt = 1 / 60;
for (let i = 0; i < frames; i++) {
  m.t += dt; m.update(dt);
  m.tears = m.tears.filter(z => m.t - z.t0 < 1.5);
  m.render(dt);
}
document.title = 'done ' + state;
"""

page = ('<!doctype html><meta charset="utf-8"><title>capture</title>'
        '<style>html,body{margin:0;background:#0e0e14}svg{display:block}</style>'
        + svg +
        '<script>window.requestAnimationFrame=()=>0;window.cancelAnimationFrame=()=>{};</script>'
        '<script>' + script + '</script>'
        '<script>' + capture + '</script>')
open(OUT, 'w', encoding='utf-8').write(page)
print('wrote', OUT, len(page))

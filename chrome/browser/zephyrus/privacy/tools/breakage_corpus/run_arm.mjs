// Runs ONE arm of the §12.6 breakage corpus and writes a JSON result file.
//
//   node run_arm.mjs --arm=off --out=off.json
//   node run_arm.mjs --arm=on  --out=on.json
//
// Two arms are two separate browser launches, which means the fingerprint
// session secret differs between them. Canvas hashes are therefore NOT
// comparable across arms and nothing here tries to compare them: the arms are
// compared on BREAKAGE SIGNALS. (The one place a hash is used is the
// flag-effect canary below, which only asks whether the value CHANGED between
// arms, not what it is.)
//
// Randomization needs BOTH features. IsFingerprintRandomizationEnabled() is
// IsCollectionEnabled() && the randomization flag, so enabling only the latter
// silently does nothing and every site "passes" — the worst possible outcome
// for a gate like this. Hence the canary, which fails the run loudly.

import { spawn } from 'node:child_process';
import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { scanChallenges, scanHttp, scanConsole } from './detectors.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const CHROME = process.env.ZCHROME ||
  'D:/chromium/src/out/Release/chrome.exe';
const PORT = Number(process.env.ZPORT || 9555);
const PER_SITE_MS = Number(process.env.ZSITE_MS || 20000);

const args = Object.fromEntries(
  process.argv.slice(2).map(a => {
    const [k, v] = a.replace(/^--/, '').split('=');
    return [k, v === undefined ? true : v];
  }));
const ARM = args.arm === 'on' ? 'on' : 'off';
const OUT = args.out || `${ARM}.json`;
const ONLY = args.only ? String(args.only) : null;
const SHOTS = join(HERE, 'shots', ARM);

const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---------------------------------------------------------------- corpus ----
function loadCorpus() {
  const sites = [];
  if (!ONLY || ONLY === 'functional') {
    const fc = JSON.parse(readFileSync(join(HERE, 'functional_corpus.json'), 'utf8'));
    for (const s of fc.sites) {
      sites.push({ url: s.url, group: 'functional', surfaces: s.surfaces, manual: s.manual });
    }
  }
  if (!ONLY || ONLY === 'tranco') {
    const csv = readFileSync(join(HERE, 'tranco_top50.csv'), 'utf8');
    for (const line of csv.trim().split(/\r?\n/)) {
      const [rank, host] = line.split(',');
      if (!host) continue;
      sites.push({ url: `https://${host.trim()}/`, group: 'tranco', rank: Number(rank) });
    }
  }
  return sites;
}

// ------------------------------------------------------------------ cdp -----
class Cdp {
  constructor(ws) { this.ws = ws; this.id = 0; this.pending = new Map(); this.handlers = []; }
  static async attach(wsUrl) {
    const ws = new WebSocket(wsUrl);
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('ws')); });
    const c = new Cdp(ws);
    ws.onmessage = ev => {
      const m = JSON.parse(ev.data);
      if (m.id && c.pending.has(m.id)) { c.pending.get(m.id)(m); c.pending.delete(m.id); }
      else if (m.method) { for (const h of c.handlers) h(m); }
    };
    return c;
  }
  on(fn) { this.handlers.push(fn); }
  send(method, params = {}, ms = 15000) {
    return new Promise(res => {
      const i = ++this.id;
      const to = setTimeout(() => { if (this.pending.delete(i)) res({ timedOut: true }); }, ms);
      this.pending.set(i, m => { clearTimeout(to); res(m.result ?? {}); });
      this.ws.send(JSON.stringify({ id: i, method, params }));
    });
  }
  close() { try { this.ws.close(); } catch {} }
}

// --------------------------------------------------------------- browser ----
function launch() {
  const flags = [
    `--user-data-dir=D:/zcorpus-${ARM}`,
    '--no-first-run', '--no-default-browser-check',
    '--disable-background-networking',
    `--remote-debugging-port=${PORT}`,
    'about:blank',
  ];
  if (ARM === 'on') {
    // ZSURFACES selects the §6.5 per-surface bitmask so a finding is
    // attributable to ONE surface instead of to "the feature". Bits (see
    // zephyrus_fingerprint_seed.h): canvas 1, audio 2, webgl 4, navigator 8,
    // screen 16. Unset means all of them.
    const mask = process.env.ZSURFACES;
    const params = mask
      ? `ZephyrusPrivacyFingerprintRandomization:surfaces/${mask}`
      : 'ZephyrusPrivacyFingerprintRandomization';
    flags.unshift(`--enable-features=ZephyrusPrivacyIntelligence,${params}`);
  }
  const p = spawn(CHROME, flags, { detached: true, stdio: 'ignore' });
  p.unref();
  return p;
}

async function browserWs() {
  for (let i = 0; i < 60; i++) {
    try {
      const v = await (await fetch(`http://127.0.0.1:${PORT}/json/version`)).json();
      if (v.webSocketDebuggerUrl) return v.webSocketDebuggerUrl;
    } catch {}
    await sleep(500);
  }
  throw new Error('devtools never came up');
}

// ------------------------------------------------------------- one site -----
async function visit(browser, site) {
  const rec = {
    url: site.url, group: site.group, rank: site.rank,
    surfaces: site.surfaces, manual: site.manual,
    loaded: false, crashed: false, title: '', finalUrl: '',
    requests: [], consoleErrors: [], exceptions: [], canvasHash: null,
    error: null,
  };

  const { targetId } = await browser.send('Target.createTarget', { url: 'about:blank' });
  if (!targetId) { rec.error = 'no target'; return rec; }

  let cdp;
  try {
    const { sessionId } = await browser.send('Target.attachToTarget',
      { targetId, flatten: true });
    // Flat sessions multiplex over the browser socket; wrap send to tag them.
    const raw = browser;
    const send = (method, params = {}, ms = 15000) => new Promise(res => {
      const i = ++raw.id;
      const to = setTimeout(() => { if (raw.pending.delete(i)) res({ timedOut: true }); }, ms);
      raw.pending.set(i, m => { clearTimeout(to); res(m.result ?? {}); });
      raw.ws.send(JSON.stringify({ id: i, method, params, sessionId }));
    });
    cdp = { send, sessionId };

    const onEvent = m => {
      if (m.sessionId !== sessionId) return;
      if (m.method === 'Network.responseReceived') {
        rec.requests.push({ url: m.params.response.url, status: m.params.response.status });
      } else if (m.method === 'Network.loadingFailed') {
        rec.requests.push({ url: m.params.request?.url || '', status: 0,
                            failed: m.params.errorText });
      } else if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') {
        rec.consoleErrors.push((m.params.args || [])
          .map(a => a.value ?? a.description ?? a.type).join(' ').slice(0, 300));
      } else if (m.method === 'Runtime.exceptionThrown') {
        rec.exceptions.push(String(
          m.params.exceptionDetails?.exception?.description ||
          m.params.exceptionDetails?.text || '').split('\n')[0].slice(0, 300));
      } else if (m.method === 'Inspector.targetCrashed') {
        rec.crashed = true;
      }
    };
    browser.on(onEvent);

    await send('Page.enable');
    await send('Network.enable');
    await send('Runtime.enable');
    await send('Inspector.enable');

    await send('Page.navigate', { url: site.url }, PER_SITE_MS);
    await sleep(Math.min(PER_SITE_MS, 9000));

    const t = await send('Runtime.evaluate',
      { expression: 'document.title', returnByValue: true }, 8000);
    rec.title = t?.result?.value ?? '';
    const u = await send('Runtime.evaluate',
      { expression: 'location.href', returnByValue: true }, 8000);
    rec.finalUrl = u?.result?.value ?? '';
    const body = await send('Runtime.evaluate', {
      expression: '(document.body?document.body.innerText:"").slice(0,20000)',
      returnByValue: true,
    }, 8000);
    rec.pageText = body?.result?.value ?? '';
    const frames = await send('Runtime.evaluate', {
      expression: 'Array.from(document.querySelectorAll("iframe")).map(f=>f.src).slice(0,40)',
      returnByValue: true,
    }, 8000);
    rec.frameUrls = frames?.result?.value ?? [];

    // Flag-effect probe. Same drawing on every site, so its hash is a property
    // of (origin, session secret) — it MUST differ between arms once the flag
    // is on. Cheap enough to run everywhere, and it turns "the run was clean"
    // into a claim that can be checked instead of assumed.
    const probe = await send('Runtime.evaluate', {
      expression: `(() => { try {
        const c = document.createElement('canvas'); c.width=64; c.height=64;
        const x = c.getContext('2d');
        x.fillStyle='#4477aa'; x.fillRect(0,0,64,64);
        x.fillStyle='#f60';    x.fillRect(8,8,20,20);
        const d = x.getImageData(0,0,64,64).data;
        let h=2166136261>>>0;
        for (let i=0;i<d.length;i++){h^=d[i];h=Math.imul(h,16777619)>>>0;}
        return h.toString(16);
      } catch(e) { return 'ERR:'+e.message; } })()`,
      returnByValue: true,
    }, 8000);
    rec.canvasHash = probe?.result?.value ?? null;
    rec.loaded = !!rec.finalUrl && rec.finalUrl !== 'about:blank';

    const shot = await send('Page.captureScreenshot', { format: 'png' }, 15000);
    if (shot?.data) {
      const name = site.url.replace(/[^a-z0-9]+/gi, '_').slice(0, 80) + '.png';
      writeFileSync(join(SHOTS, name), Buffer.from(shot.data, 'base64'));
      rec.screenshot = name;
    }

    browser.handlers = browser.handlers.filter(h => h !== onEvent);
  } catch (e) {
    rec.error = String(e.message || e);
  } finally {
    await browser.send('Target.closeTarget', { targetId }).catch(() => {});
  }

  rec.signals = [
    ...scanChallenges(rec),
    ...scanHttp({ requests: rec.requests, mainUrl: rec.finalUrl }),
    ...scanConsole(rec),
  ];
  // Keep the file small: the raw request list is large and only the signals matter.
  rec.requestCount = rec.requests.length;
  delete rec.requests;
  delete rec.pageText;
  return rec;
}

// ------------------------------------------------------------------ main ----
mkdirSync(SHOTS, { recursive: true });
let sites = loadCorpus();
// Smoke-test escape hatch: ZLIMIT=2 exercises the whole path in seconds.
if (process.env.ZLIMIT) sites = sites.slice(0, Number(process.env.ZLIMIT));
console.log(`arm=${ARM} sites=${sites.length} chrome=${CHROME}`);

const proc = launch();
const browser = await Cdp.attach(await browserWs());
await browser.send('Target.setDiscoverTargets', { discover: true });

const results = [];
for (const [i, site] of sites.entries()) {
  process.stdout.write(`[${i + 1}/${sites.length}] ${site.url} `);
  const rec = await visit(browser, site);
  results.push(rec);
  const n = rec.signals.length;
  console.log(rec.crashed ? 'CRASHED' : rec.loaded ? `ok (${n} signals)` : 'NOLOAD');
}

writeFileSync(join(HERE, OUT), JSON.stringify({
  arm: ARM,
  surfaces: process.env.ZSURFACES || 'all',
  chrome: CHROME,
  when: new Date().toISOString(),
  trancoListId: 'K9LYW',
  results,
}, null, 1));

browser.close();
try { process.kill(proc.pid); } catch {}
console.log(`wrote ${OUT}`);
process.exit(0);

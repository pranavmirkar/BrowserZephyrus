// Diffs two §12.6 arms and prints the verdict.
//
//   node compare.mjs off.json on.json
//
// The whole design rests on one rule: a signal present in BOTH arms is the
// site's own behaviour and is not evidence about Zephyrus. Only signals that
// appear in the ON arm alone count. Measured example — jsfiddle.net requests
// /cdn-cgi/challenge-platform with the flag off, so its raw signal count is 1
// in a perfectly healthy run.

import { readFileSync } from 'node:fs';

const [offPath, onPath] = process.argv.slice(2);
const off = JSON.parse(readFileSync(offPath, 'utf8'));
const on = JSON.parse(readFileSync(onPath, 'utf8'));

const byUrl = arm => Object.fromEntries(arm.results.map(r => [r.url, r]));
const A = byUrl(off), B = byUrl(on);

// ------------------------------------------------------- validity first -----
// A run whose flag never took effect reports zero breakage and looks like a
// pass. Check that before reading anything else.
let changed = 0, comparable = 0;
for (const url of Object.keys(B)) {
  const a = A[url], b = B[url];
  if (!a || !a.canvasHash || !b.canvasHash) continue;
  if (String(a.canvasHash).startsWith('ERR')) continue;
  comparable++;
  if (a.canvasHash !== b.canvasHash) changed++;
}
const valid = comparable > 0 && changed === comparable;

console.log('='.repeat(72));
console.log('§12.6 BREAKAGE CORPUS');
console.log('='.repeat(72));
console.log(`off arm : ${off.when}  (${off.results.length} sites)`);
console.log(`on  arm : ${on.when}  (${on.results.length} sites)`);
console.log(`tranco  : ${on.trancoListId}`);
console.log();
console.log(`CANARY  : canvas value changed on ${changed}/${comparable} comparable sites`);
if (!valid) {
  console.log();
  console.log('*** RUN INVALID ***');
  console.log('The randomization flag did not take effect everywhere it should.');
  console.log('Randomization needs BOTH ZephyrusPrivacyIntelligence and');
  console.log('ZephyrusPrivacyFingerprintRandomization; with only the second the');
  console.log('feature silently does nothing and every site "passes".');
  console.log('Do not read the findings below as evidence of anything.');
} else {
  console.log('          -> flag verified in effect end to end.');
}
console.log();

// ------------------------------------------------------------- findings -----
const findings = [];
for (const url of Object.keys(B)) {
  const a = A[url], b = B[url];
  if (!a) continue;

  // Hard failures first: these are breakage regardless of signal matching.
  if (b.crashed && !a.crashed) {
    findings.push({ url, severity: 'CRASH', what: 'renderer crashed only with the flag on' });
  }
  if (a.loaded && !b.loaded) {
    findings.push({ url, severity: 'NOLOAD', what: 'page loaded with the flag off but not on' });
  }

  const aKeys = new Set((a.signals || []).map(s => s.kind + '|' + s.marker));
  for (const s of b.signals || []) {
    const k = s.kind + '|' + s.marker;
    if (aKeys.has(k)) continue;              // present in both: the site's own
    const severity = s.kind.startsWith('challenge') ? 'CHALLENGE'
      : s.kind.startsWith('http') ? 'HTTP'
      : 'CONSOLE';
    findings.push({ url, severity, what: `${s.kind}: ${s.marker}`, detail: s.detail });
  }
}

const order = { CRASH: 0, NOLOAD: 1, CHALLENGE: 2, HTTP: 3, CONSOLE: 4 };
findings.sort((x, y) => order[x.severity] - order[y.severity]);

const counts = {};
for (const f of findings) counts[f.severity] = (counts[f.severity] || 0) + 1;

console.log('FINDINGS (present with the flag ON and not OFF)');
console.log('-'.repeat(72));
if (!findings.length) {
  console.log('none.');
} else {
  for (const f of findings) {
    console.log(`[${f.severity}] ${f.url}`);
    console.log(`         ${f.what}`);
    if (f.detail) console.log(`         ${String(f.detail).slice(0, 110)}`);
  }
}
console.log();
console.log('SUMMARY:', Object.keys(counts).length
  ? Object.entries(counts).map(([k, v]) => `${k}=${v}`).join('  ')
  : 'no differences');

// A site that failed to load in BOTH arms tested nothing. Reporting the corpus
// size without this makes a run look stronger than it is.
const deadBoth = Object.keys(B).filter(u => A[u] && !A[u].loaded && !B[u].loaded);
console.log(`SITES THAT TESTED NOTHING (no load in either arm): ${deadBoth.length}`);
for (const u of deadBoth) console.log('   ', u);

const manual = (on.results || []).filter(r => r.manual);
console.log();
console.log(`MANUAL PASS REQUIRED for ${manual.length} sites (§12.6 mandates it;`);
console.log('automation cannot tell that an exported file is corrupt):');
for (const r of manual) console.log('   ', r.url);

console.log();
console.log(valid && !findings.length
  ? 'VERDICT: no automated breakage detected. NOT sufficient alone — the manual\n         pass above is required before the flag may default on.'
  : valid
    ? 'VERDICT: differences found. Triage above before the flag may default on.'
    : 'VERDICT: INVALID RUN — fix the flags and re-run.');

# §12.6 Breakage corpus — run 1

**Date:** 2026-08-19
**Build:** `out/Release` (official, DCHECKs off — the real user experience)
**Corpus:** Tranco `K9LYW` top-50 + 14-site functional set = 64 sites
**Arms:** flag off vs `--enable-features=ZephyrusPrivacyIntelligence,ZephyrusPrivacyFingerprintRandomization`

## Verdict

**No automated breakage detected.** This is **not** sufficient to default the flag
on. §12.6 requires a manual pass, and two risks below need a decision first.

## Validity

The most dangerous outcome for a gate like this is a clean run produced by a flag
that never took effect. Guarded by a canary: the same 64x64 drawing hashed on
every site.

- flag off: identical baseline hash everywhere (`213b5085`) — no perturbation
- flag on: **63/63 comparable sites changed**, and differ per origin

`compare.mjs` refuses to print a verdict unless every comparable site flips.

One site could not be probed: `appsflyersdk.com` returns
`ERR:c.getContext is not a function` — the page replaces `document.createElement`.
That is the site's own behaviour, present in both arms.

## Automated results

| | off | on |
|---|---|---|
| loaded | 64/64 | 64/64 |
| renderer crashes | 0 | 0 |
| sites that tested nothing | 0 | 0 |
| findings present ON-only | — | **0** |

Signals do occur in both arms and cancel: jsfiddle.net requests
`/cdn-cgi/challenge-platform` and outlook.com raises two console errors **with the
flag off**. That is why only ON-only signals count — a raw signal count would
have reported a false positive on the first site examined.

## The result the corpus alone could not give

A clean A/B run says "no site complained". It does not say the withheld
extensions were ever exercised — and absence of complaint from code that never
ran is not evidence. Probed directly on real sites (`probe_webgl.mjs`):

- WebGL 2.0, **32 extensions with the flag off, 26 with it on**
- 6 of the withheld twelve exist on this GPU, and **all 6 are removed**:
  `WEBGL_debug_shaders`, `WEBGL_compressed_texture_s3tc_srgb`,
  `EXT_disjoint_timer_query_webgl2`, `KHR_parallel_shader_compile`,
  `OVR_multiview2`, `WEBGL_multi_draw`
- `getSupportedExtensions()` and `getExtension()` **agree on every name** — the
  §93 test's property holds on real sites, not just in the browsertest

So the mechanism is confirmed live. Two consequences are worth a decision, and
neither is a crash, which is why the A/B run could not surface them:

1. **`KHR_parallel_shader_compile` is a performance extension.** Removing it
   makes shader compilation block instead of proceeding in parallel. On
   shader-heavy WebGL this means longer load and visible jank — a real cost that
   no breakage detector will ever report. Note this interacts with task #94
   (Speedometer): a perf regression sourced from a *privacy* flag would be easy
   to misattribute.
2. **`OVR_multiview2` is WebXR stereo rendering.** Withholding it plausibly
   affects VR content rather than merely slowing it.

The other four are low risk: `WEBGL_debug_shaders` is debug-only,
`WEBGL_multi_draw` and `EXT_disjoint_timer_query_webgl2` are feature-detected
optimisations with normal fallbacks, and `WEBGL_compressed_texture_s3tc_srgb`
falls back to uncompressed at a memory cost.

## Per-surface runs

The §6.5 `surfaces` bitmask is verified working, which is what makes any future
finding attributable to one surface instead of to "the feature" (bits:
canvas 1, audio 2, webgl 4, navigator 8, screen 16):

| mask | canvas 2D | WebGL extensions |
|---|---|---|
| all | perturbed | 26 — withheld |
| `1` canvas only | perturbed | **32 — all six restored** |
| `4` webgl only | **unperturbed baseline `213b5085`** | 26 — withheld |

A full five-singleton sweep was **deliberately not run**, and the reasoning is
recorded so it can be challenged: the all-on arm was clean, and enabling a
SUBSET of perturbations cannot break what the full set did not. The one case
where that argument is not airtight is partial-mask inconsistency — perturbing
one surface while leaving a related one truthful could in principle create a
mismatch that all-on does not. So the mask actually worth running is the one we
would ship, not five singletons.

**Ship candidate, mask 27 (everything except WebGL), full corpus:** 64/64
loaded, canary 63/63, **zero findings**. So if the two WebGL extensions below
are judged unacceptable, clearing that one bit is a verified-clean option rather
than a hopeful one.

## Still required before the flag may default on

1. **Manual pass** — largely DONE, see `MANUAL_PASS.md`. Audio output proved
   bit-identical while analyser data differs; noise proved deterministic across
   repeat reads; PDF.js text extraction identical; three.js, Aquarium and
   Chart.js inspected and correct. **Two items genuinely remain, and they are
   the ones that matter most** — the only cases where `getImageData` is
   load-bearing rather than a fingerprint probe:
   - **photopea.com** — automation tested NOTHING (the site now serves a landing
     page; the editor boots only behind a click, and its `#{json}` hash API did
     not launch it). Open an image, apply a filter, export a PNG, confirm it opens.
   - **squoosh.app** — app shell only, no image processed, so the worker canvas
     pipeline never ran. Drop an image, compress, confirm the output is intact.
2. **Decide on the two WebGL extensions above**, or ship with the WebGL bit
   cleared from the `surfaces` mask.
3. Per-surface capability — DONE and verified above.

## Gap: the export-size cost is untested by this corpus

Task #91 accepted the toDataURL export-size cost (flat images inflate ~8-11x)
and wrote: "Revisit ONLY if the breakage corpus turns up a site that actually
fails because of it." **This corpus cannot turn that up.** Nothing in it exports
a large, mostly-flat canvas and sends it anywhere, so the designated trigger for
revisiting a shipped trade-off is pointed at a test that does not exercise it.

Confirmed live 2026-08-20: the same drawing exported to PNG is 454 B with the
flag off and 1942 B with it on (~4.3x on a flat 64x64), consistent with #91.

To close this, add corpus entries that actually exercise it: a signature pad, a
chart library's "download PNG" button, or a screenshot tool — anything that
exports a large flat canvas AND uploads it, where the failure mode is a 413 or a
quota error rather than anything visible.

## Known limits of this run

- **Shared workers are unperturbed** (open gap, #93). Canvas-in-SharedWorker
  apps therefore cannot show breakage here; that is not evidence of safety.
- No pixel diffing. Screenshots are captured to `shots/{off,on}/` for human
  review instead. Canvas noise is <=1 step per subpixel and never touches alpha,
  so it is invisible by construction — an automated pixel diff would cost
  decoding work to confirm what the design already guarantees.
- Logged-in flows are untested; the corpus is anonymous browsing only.
- Single run on one GPU (AMD iGPU, DirectComposition disabled on this hardware).
  The extension set is GPU-dependent: another GPU exposes a different subset of
  the withheld twelve.

## Reproducing

```
node run_arm.mjs --arm=off --out=off.json
node run_arm.mjs --arm=on  --out=on.json
node compare.mjs off.json on.json
node probe_webgl.mjs --arm=off ; node probe_webgl.mjs --arm=on
```

Re-download the corpus with list id `K9LYW`, never "latest" — see `PIN.txt`.

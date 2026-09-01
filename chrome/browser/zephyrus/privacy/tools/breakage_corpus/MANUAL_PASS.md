# §12.6 manual pass — run 1

**Date:** 2026-08-19 · **Build:** `out/Release` · both arms

§12.6 requires a human. But several items on that list are subjective only by
habit, and turning those into measurements is strictly better evidence than an
impression. So this splits into: what was **proved**, what was **inspected**, and
what genuinely still needs a person.

## Proved (objective, `manual_pass.mjs`)

### Audio — the strongest result here
"Does the audio sound normal" is really two claims, and only the four
`AnalyserNode` getters are perturbed (`analyser_node.cc`); `getChannelData` and
offline rendering are untouched. So:

| | off | on | |
|---|---|---|---|
| rendered output (what you hear) | `b2d0864d` | `b2d0864d` | **identical — audio unaltered** |
| analyser data (what a tracker reads) | `8f3aecb` | `cbb781c` | **differs — fingerprint broken** |

A listening test could not have established either half this precisely.

### Determinism — the anti-averaging guarantee, live
Three consecutive reads of an unchanged canvas: **stable in both arms**
(off `213b5085`, on `de47cd76`). A site cannot average our noise away.

### PDF.js — text extraction unaffected
Identical in both arms: 2 text layers, **11,900 characters**, 3 canvases. Text
selection and glyph extraction are unchanged.

## Inspected (screenshots, reviewed)

- **three.js keyframes** — renders identically in both arms, full textures, no
  corruption. Stats overlay: **239 fps off vs 235 fps on** — within noise. This
  is the scene most exposed to the six withheld extensions and it is unaffected.
- **WebGL Aquarium** — renders correctly with `WEBGL_compressed_texture_s3tc_srgb`
  withheld: 500 fish, all textures intact, 160 fps. *Caveat: the off arm did not
  capture in time, so this is not an A/B comparison, only evidence that it works
  with the flag on.*
- **Chart.js** — renders correctly: axes, gridlines, bars, legend, text all
  clean. The two arms show **different bar values**, which is Chart.js
  generating random sample data per load, not breakage — confirmed by reading
  both screenshots (the axes auto-scaled to different ranges).

## Done by Pranav, 2026-08-20 — both PASS

Run on out/Release with randomization verified active in that window (canvas
probe returned `2e2d06b6` against the unperturbed baseline `213b5085`, checked
BEFORE testing so a silently-off flag could not waste the exercise).

- **Photopea** — opened an image, applied a blur, exported. Blur applied
  correctly; exported image correct. The read -> filter -> export pipeline is
  intact with noise active.
- **Squoosh** — compressed ~10MB to 2-3MB with no visible degradation. This is
  the real-world confirmation of document/worker pixel agreement: Squoosh does
  its canvas work in a WORKER, so a worker disagreeing with its page would show
  up here as corruption.

Both are the cases where `getImageData` is LOAD-BEARING rather than a
fingerprint probe. §12.6's manual pass is satisfied.

### One thing visual inspection structurally cannot establish

Perturbation is <=1 step per subpixel by design, so "looks fine" is the expected
result whether or not the exported bytes changed. Measured directly instead
(same drawing, flag off vs on):

| | off | on |
|---|---|---|
| `toDataURL` bytes | hash `1b1624f6`, 454 B | hash `a21414ca`, **1942 B** |

So an exported file DOES carry the noise, and a flat image inflates ~4.3x here.
That is **known and accepted** — see task #91, which measured 8.2x on a flat
300x150 and 11.5x on a chart and deliberately accepted the cost.

**But #91 defers the revisit trigger to this corpus** ("revisit ONLY if the
breakage corpus turns up a site that actually fails because of it") and the
corpus as built CANNOT turn it up: no entry exports a large, mostly-flat canvas
and uploads it. See the gap noted in `REPORT.md`.

## Previously not tested — now resolved above

Both are the "real editing pipeline" cases, and both failed to exercise anything:

1. **Photopea** — `photopea.com` now serves a marketing landing page; the editor
   boots only behind a click, and the documented `#{json}` hash API did not
   launch it either (verified: `canvases: []`). **Run 1 tested nothing here.**
   Needed: open an image, apply a filter, export a PNG, confirm it opens.
2. **Squoosh** — loaded the app shell (drop-target screen) but processed no
   image, so the worker canvas pipeline was never exercised. Needed: drop an
   image, compress, confirm the output is not corrupted.

These two matter more than the rest of the list combined, because they are the
only cases where `getImageData` is **load-bearing** rather than a fingerprint
probe — where noise would land in a file the user keeps.

Optional third: listen to the MDN audio-analyser page. The objective test above
already proves output is bit-identical, so this is confirmation, not evidence.

## What this does and does not license

It supports: audio output is safe, noise is deterministic, WebGL rendering
survives the withheld extensions on this GPU, canvas 2D chart rendering is clean,
PDF text is unaffected.

It does **not** yet license defaulting the flag on. Outstanding: the two editor
pipelines above, the `KHR_parallel_shader_compile` / `OVR_multiview2` decision
from `REPORT.md`, and the per-surface runs.

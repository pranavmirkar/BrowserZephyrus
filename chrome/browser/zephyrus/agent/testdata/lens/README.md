# lens_reference.json

Expected numbers for the lens mascot, produced by running the PROTOTYPE'S OWN
`Mascot` class (the handoff's `reference/agent-mascot.html`) headless under node.

`run_reference.js` loads the prototype's `<script>` into a vm context with a
permissive fake DOM, replaces `Math.random` with mulberry32 (the generator
`SeededLensRandom` uses), drives `update()` and `render()` by hand at 60 fps for
6 s per case, and records the springs, gaze, blinks, tears, ripples and every
number `render()` wrote. `lens_reference_unittest.cc` makes the C++ model
reproduce them from the same seed.

Regenerate (only if the prototype changes; the output is checked in):

    LENS_PROTOTYPE=path/to/agent-mascot.html \
      node run_reference.js ../lens_reference.json 6 60 30

45 cases: all 31 prototype states in Static, 8 of them in Fluid, 6 emotions at
intensity 0.5; 10 sampled frames each.

# Visual parity (milestone 3)

`lens_painter_unittest` can write one PNG per state (`LENS_DUMP_DIR`, and
`LENS_DUMP_FRAMES` for the moment). These scripts render the SAME frame from the
prototype in headless Chrome and diff the two (needs Pillow and numpy):

    export LENS_WORK=/some/dir        # default: ./work
    python make_capture_page.py       # capture.html: the prototype, driven deterministically
    python capture_all.py             # prototype PNGs, 31 states -> $LENS_WORK/ref
    LENS_DUMP_DIR=$LENS_WORK/native zephyrus_agent_unittests --gtest_filter='LensPainterTest.Dump*'
    python compare.py                 # per-state numbers, side-by-side sheets in $LENS_WORK/sheets
    python effects_parity.py          # frames where ripple, flash, beam and tears are visible

Result when this was written (2026-10-07): 31 states, mean difference 0.35 of 255,
silhouette overlap 99.65%, 0.03% of pixels differing clearly (edge anti-aliasing);
12 effect frames, mean difference at most 0.6.

# Fluid and lite parity (milestone 4)

    python capture_all.py fluid                       # prototype, Fluid fill -> $LENS_WORK/ref_fluid
    LENS_DUMP_FILL=fluid LENS_DUMP_DIR=$LENS_WORK/native_fluid zephyrus_agent_unittests --gtest_filter='LensPainterTest.Dump*'
    python compare.py ref_fluid native_fluid

The capture page takes `?lite=1`, which calls the prototype's own `goLite()`; the native
side takes `LENS_DUMP_LITE=1`. Results (2026-10-08): Fluid, 31 states, mean difference
0.75 of 255 and silhouette overlap 99.3%; lite, 8 states, mean 0.84 and overlap 97.7%
(thin 1.8 px rim lines are the whole difference).

Cost per frame (software raster, one core, this machine; `LENS_BENCH=1`
`--gtest_filter=LensPainterTest.CostPerFrame`): see docs/mascot-lens-plan.md.

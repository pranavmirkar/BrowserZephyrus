# New mascot skin for Zep ("lens"): plan (2026-10-07)

Source: `C:\Users\prana\Downloads\agent-mascot-handoff\` (SPEC.md, reference/agent-mascot.html,
reference/mascot_rep.svg, tools/capture_states.py). The prototype is the source of truth for
how it looks and moves. Local work only while the hackathon freeze holds.

## Decisions (user, 2026-10-07)

1. **Native port** (C++/Skia) for the roaming Zep overlay, not an embedded web page.
2. **Skin setting**: today's pixel Zep stays, selectable; it remains the default until switched.
3. **Fill modes in v1: Static and Fluid.** Opal and Space are out of v1 (the spec itself
   calls them the heaviest and untested at small sizes).

## What changes and what does not

Today's Zep is a pixel character drawn from rectangles (`mascot_rig`), with arms, props and
18 moods. The new character has no limbs and no props: it speaks only through its two eyes
and by morphing its body (6 activities + 25 emotions, spring-driven). So a skin is not a
re-colour: it is a second **model** (what the character is doing) and a second **painter**
(how it is drawn) behind the same overlay, pointer path and bubble.

Kept as they are: `ZephyrusAgentMascotOverlay` (window-wide, click-through), pointer path and
the input queue, the speech bubble, hands-free and voice lock.

## Layout (all new, nothing existing is restructured)

```
chrome/browser/zephyrus/agent/
  lens_math.h            clamp, lerp, easings, beat, Rgb
  lens_random.h          injectable RNG (seedable; default uses base::RandDouble)
  lens_spring.{h,cc}     damped spring, semi-implicit Euler, <= 1/120 s substeps
  lens_geometry.{h,cc}   body control points (u,k), pivots, layer-weight opacities
  lens_eyes.{h,cc}       lid path, blink curve
  lens_states.{h,cc}, lens_states_activities.cc   activities, EMO table, emoUpdate   (done)
  lens_model.{h,cc}      the Mascot class minus the DOM: Update(dt), ComputeFrame()   (done)
  lens_unittest.cc, lens_model_unittest.cc, lens_reference_unittest.cc   (done)
chrome/browser/ui/views/frame/
  zephyrus_lens_painter.{h,cc}   LensFrame -> Skia (static + fluid + eyes + fx) (milestone 3-4)
```

The model returns a plain `LensFrame` (numbers only); the painter turns it into Skia. That keeps
the 25 emotions testable without a window and keeps the painter replaceable (a lite painter for
small sizes, or Opal/Space later).

## Milestones

| # | What | Status |
|---|---|---|
| 1 | spring, geometry, eyes, layer weights (pure) | DONE 2026-10-07: spec section 10 tests plus exact numbers from the prototype |
| 2 | activities, 25 emotions, the model, and the browser-function states | DONE 2026-10-07 (below) |
| 3 | painter: body (Static fill), eyes, highlights, tears, ripples, tint, flash, beam | DONE 2026-10-07 (below) |
| 4 | Fluid layer painting, reversible lite mode, cost measured | DONE 2026-10-08 (below) |
| 5 | integration: pref zephyrus.agent.mascot_skin, settings control, mood mapping, bubble | old Zep unchanged by default |
| 6 | performance and small sizes | numbers recorded; lite fallback reversible |

### Milestone 2: what was built and how it is checked

- `LensModel` (lens_model.h/.cc) is the prototype's Mascot class without its DOM.
  `Update(dt)` advances springs, gaze, blinks, tears, ripples and colours.
  `ComputeFrame()` returns every number a painter needs (`LensFrame`), including the
  fluid fill's blob, ribbon, rim and gloss geometry, computed only while that layer is
  visible.
- 45 states: the 6 activities and 25 emotions ported from the prototype with the same
  numbers AND the same order of random draws, plus 14 added for browser functions.
- **Parity with the prototype.** `lens_reference_unittest` replays 45 runs of the real
  prototype (recorded under node with a seeded generator): 450 frames covering every
  spring, the gaze, blinks, tears, ripples, colours and the numbers render() wrote.
  Springs match to 1e-6; rendered numbers to the prototype's own rounding. Three
  deliberate deviations (a state constant, two random draws swapped, a render formula)
  are each caught.
- `lens_model_unittest`: every state in Static and Fluid for 10 s with finite, bounded
  frames; every state at intensity 0.3 and 1 for a minute; repeatable by seed; switching
  between ANY two of the 45 states never cuts the body or the lid pose; intensity,
  reduced motion, cursor following, tear pool, ripples, fluid crossfade; 14 behaviour
  tests for the new states (each fails if its behaviour is broken).

### Milestone 3: the Skia painter, and how it is checked

`LensPainter` (lens_painter.h/.cc) draws a `LensFrame` with plain Skia, element for element
from the prototype's SVG: the two-layer Static gradient, the lid clip, the eye dome gradient,
the five highlight shapes, tears, tint, beam, flash and click ripples, with the CSS
saturate/brightness filter as an offscreen colour-matrix layer. No views: it takes an
`SkCanvas`, so the overlay can hand it a gfx::Canvas later.

Checked against the prototype rendered in headless Chrome, the same frame from the same seed:

| Check | Result |
|---|---|
| 31 prototype states, 1.8 s in | mean difference 0.35 of 255; silhouettes overlap 99.65%; 0.03% of pixels differ clearly (1-px edge anti-aliasing) |
| 12 frames where effects are visible (ripple, flash, beam, tears, tint) | mean difference at most 0.6 of 255 |
| every one of the 45 states | paints a body inside its window, identical pixels on a second render |

Tools and how to run them: `agent/testdata/lens/README.md`. The Fluid layer is not painted yet
(a fluid body shows the static fill meanwhile), which is milestone 4.

What a still frame cannot judge: the 14 added states are told apart by MOTION as much as pose
(typing, reading, scrolling, speaking, downloading, loading and switching look alike frozen).
Sleeping, held, greeting, listening, relieved and handoff already read as themselves in a still.
Judging the rest needs the live overlay.

### Milestone 4: the Fluid fill, lite mode, and what it costs

- `PaintLens` now draws the Fluid fill as the prototype does: dark glass base, seven orbiting
  colour fields (screen-blended cores), three waveform ribbons (a blurred glow and a bright
  core), the shading, a rotating gradient rim (blurred) and a specular line, and the gloss.
  Static and Fluid cross-fade through the same layer-opacity maths the unit tests already prove.
  A hidden layer does no work: with Fluid off the frame carries no fluid geometry at all.
- **Lite mode is reversible.** `LensPaintOptions::lite` drops the two blurs, the ribbon glow
  and thins the rim, as the prototype's `goLite()` does, but the caller can switch it back.
  `LensQualityGovernor` decides: frame interval averaging above 27 ms for 3 s while a heavy fill
  shows turns it on; under 18 ms for 6 s turns it off; never twice within 4 s; stalls (hidden
  window) are ignored; Static is never affected. All pure and unit tested (8 tests).
- **Parity with the prototype, same frame, headless Chrome:** Fluid 31 states mean difference
  0.75 of 255, silhouette overlap 99.3%; lite 8 states mean 0.84, overlap 97.7%. Images match by
  eye; what differs is 1-px edges and the thin lite rim.

**Cost per frame** (software raster on one core of the development machine, so a ceiling: the
browser composites on the GPU where it can; model + ComputeFrame is about 5 microseconds):

| Fill | 96 px | 160 px | 311 px | 622 px |
|---|---|---|---|---|
| Static | 0.20 ms | 0.34 ms | 0.66 ms | 1.8 ms |
| Fluid | 0.85 ms | 1.8 ms | 4.6 ms | 13.6 ms |
| Fluid, lite | 0.56 ms | 1.1 ms | 2.9 ms | 8.5 ms |

At 60 fps one frame is 16.7 ms. So a roaming Zep at 96-160 px costs about 1-2% of a core in
Static and 5-11% in Fluid; Fluid at a large size (622 px) would eat most of a frame in software.
Recommendations for milestone 5, none of them built yet: default to Static for the roaming
cursor-sized Zep; offer Fluid at small sizes or in a larger placement (panel, settings preview);
paint at 30 fps while idle (the spec's own suggestion, halves everything above); stop painting
when the window is hidden or minimised.

### Milestone 5: in the browser (2026-10-08)

Setting: Agent model dialog, "Mascot look": Zep (pixel character, the default), Lens, Lens with flowing colour. Prefs `zephyrus.agent.mascot_skin` ("zep"|"lens") and `zephyrus.agent.mascot_lens_fill` ("static"|"fluid"); anything unrecognised reads as Zep (`ReadMascotLook`). The approved mood table is `LensStateForMood` (`lens_mood_map`); happy is "done" for 2.4 s, then joy.

The overlay keeps everything it had (walking, held, home/parked, bubble, pointer, listening light) and branches only where the drawing differs. Zep's rig stays the mood state machine; each tick the lens model is set to the state for the rig's mood and stepped. The lens has no hand, so the point it acts on is the middle of the side it faces (or the top of the body while held); a press squashes the body and draws a fading ring where the page received it. Eyes follow the person's pointer in attentive moods and the agent's target while it acts; walking leans the body. Pace: 33 ms idle (30 fps), 16 ms moving, 100 ms asleep, 1 s hidden. The quality governor switches Fluid to lite on a slow machine.

Checked: browser launched with each skin pref; idle and a scripted task capture (thinking/reading eyes, bubble above the body); the dialog row reads the saved pref. 325 agent tests pass (mood map, look prefs, squash). Checked live afterwards (2026-10-09): a real-mouse drag (press on the body, drag, release; `D:\zeph_perf\m5_real_drag.ps1`, PrintWindow frames) and an agent click (`--zephyrus-test-compact=mascot-click`, dev-only, `StartClickDemoForTesting`). Found and fixed: (1) picking the lens up made it jump about half its height DOWN, half out of the window, because it hung from the cursor by its top; it now keeps the point where it was grabbed (`grab_offset_`) and stays on the window while held. (2) the press ring was white only and vanished on a light page; it is now white over a dark ring. After: no jump on press, the lens follows the cursor with its swing and the held squash, is put down where released and settles (relieved, then idle), and the click shows a squash, a ripple and an expanding fading ring beside the body on both dark and light pages.

### Milestone 6: how it reads small, and what it costs (2026-10-08)

CORRECTION (2026-10-09) to the first version of this paragraph, which said the lens costs the same as Zep and that the overlay costs about 9 points whichever skin is drawn: several of those "lens" runs had really started as Zep, and the run-to-run spread on this machine was far larger than the differences (the same Zep build measured 4.7, 8.1 and 12.1 % within an hour).

What is solid: the cost is PAINT-driven. With the overlay's paint requests switched off the browser idled at 0.7 % of a core, at one paint a second 2 to 3 %, at ten a second 5 to 13 %, almost all of it the GPU process compositing the window for each damaged frame. Counting paints in the browser (temporary counters, removed): Zep repainted on EVERY idle tick (48 of 48 per 5 s). The cause is that its breath is less than one device pixel, so each row of the drawing crosses a pixel edge at a different moment and the picture really does change nearly every tick. Fix: while idle or asleep the breath is snapped to whole device pixels (`MascotRig::SetPixelSnap`, set by the overlay from scale and DPI), the overlay compares frames in device pixels (`QuantizeToDevicePixels`, same arithmetic as OnPaint), and the lens idles at 20 fps (30 when doing something). Result: Zep 48 -> about 13 paints per 5 s (idle), and on the clean build alternating runs read off 0.7 to 1.1 %, Zep 0.6 to 1.1 %, lens 0.8 to 1.0 % of a core. The lens could not be measured against the OLD Zep under the same conditions after the fact, so the gain from the fix is shown by paint counts and by the earlier same-day Zep runs (4.7 to 12 %), not by a clean A/B.

Also found: the look prefs were first named `zephyrus.agent.mascot.skin`, which is stored INSIDE the existing boolean `zephyrus.agent.mascot`; renamed to the siblings `zephyrus.agent.mascot_skin` and `zephyrus.agent.mascot_lens_fill`.

Small sizes (sheets of the 18 mapped states at 104 px, four moments, dark and light backgrounds): states that differ in SHAPE or COLOUR read clearly (sleeping dim and flat, sleepy, joy's closed arcs, surprise's big eyes, confused's tilt, held's squash, listening's teal and wide stance, done). States that differ only by what the EYES do (reading, scrolling, typing, searching, working, relieved) look alike in a still at 100 px, because an eye is about 4 px there; they are told apart by motion (a sweep along a line, a rise down a page, quick small steps). So the bubble text and the press ring carry the meaning of those, not the body alone. Idle sways between lean +-0.9, which is the prototype's idle, so a calm activity state looks UPRIGHT next to it.

### The 14 added states (no prototype exists, so only behaviour tests cover them)

| State | Browser function | What it does with only eyes and body |
|---|---|---|
| typing | filling a field | lowered focused lids, quick small gaze steps along the line, a squash for each Enter |
| reading | page.read | a slow sweep along each line, a small step down, a long return |
| scrolling | scrolling | the gaze rides down the page and snaps back to the top |
| listening | voice input | wide eyes, head tilt, teal tint, an irregular breathing pulse standing in for voice level |
| speaking | an answer read aloud | the whole body talks in a syllable rhythm, warm tint |
| asking | needs approval or a choice | eyes on the person, then a glance at the question, one eye a little bigger |
| handoff | sign-in, CAPTCHA, card form | looks politely away and leans out of the way, glances back now and then |
| downloading | a file arriving | looks down, slow nod, a swelling pulse |
| loading | waiting for a page | the gaze circles like a spinner |
| sleeping | idle for a long time | lids shut, slow deep breath, dim, never blinks |
| greeting | hello | sways like a wave, then settles into a smile |
| held | picked up by the cursor | wide eyes, pendulum swing, trembling |
| relieved | put back down | a long slow blink, the body sinks and settles |
| switching | tab change | glances left, then right, then settles on the new page |

They are right on paper and in the numbers; whether they READ right on screen has to be
judged in milestone 3, when there is something to look at.

## Mapping Zep's moods to the lens states (proposal; needs your OK before milestone 5)

With the added states the mapping is nearly one to one:

| Zep mood | Lens state |
|---|---|
| idle | idle |
| sleeping | sleeping |
| yawning | sleepy |
| waving | greeting |
| listening | listening |
| thinking | thinking |
| reading | reading |
| searching | searching |
| typing | typing |
| clicking | clicking |
| scrolling | scrolling |
| asking | asking |
| happy | joy (and done for a finished task) |
| confused | confused |
| alert | surprise |
| working | working |
| held | held |
| relieved | relieved |

Agent events with no Zep mood today can use the rest of the set: sign-in or CAPTCHA
`handoff`, a download `downloading`, a navigation `loading`, a tab change `switching`,
speaking an answer `speaking`, a tool error `error` and repeated failures `anxious`.
Props (keyboard, book, pointing arm) have no equivalent; a click is shown the way the
prototype shows it: the squint, the press squash and the ripple.

## Parity method

`capture_states.py` screenshots the reference page for every state in every mode. For the native
build, a test binary renders the same states from a seeded model to PNG; a script diffs the two
sets. Blink and gaze are random, so both sides are seeded or pinned (the model takes an
injectable RNG for exactly this).

## Open questions (do not block milestones 1-4)

- Size of the roaming Zep on screen (the spec says below about 96 px is untested).
- Whether emotion intensity is fixed per event or user-set (spec section 11).
- Whether the lens keeps the name "Zep" in the bubble and panel text.

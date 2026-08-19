// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_ZEPHYRUS_CANVAS_NOISE_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_ZEPHYRUS_CANVAS_NOISE_H_

#include <array>
#include <cstdint>

#include "base/containers/span.h"
#include "third_party/blink/renderer/platform/platform_export.h"

namespace blink {

// Zephyrus §6.5 canvas perturbation.
//
// **Why the algorithm lives in Blink rather than with the rest of the privacy
// feature.** The pixels only exist here, and Blink cannot depend on //chrome.
// It is deliberately ONE copy: a second implementation on the browser side
// would drift, and two implementations of a fingerprinting countermeasure that
// disagree is worse than one that is merely imperfect. The seed it consumes is
// still derived in the browser (see chrome/browser/zephyrus/privacy/
// fingerprint_seed.h) — only the 32 derived bytes cross the process boundary.
//
// **Randomize, do not block.** §6.5 is explicit that blocking `toDataURL`
// breaks image editors, chart libraries and games. The pixels come back —
// slightly wrong, consistently wrong.
//
// **Never per call.** The output is a pure function of the seed and the pixels:
// no counter, no clock, no call count. Two reads of an unchanged canvas are
// byte-identical. This is not an optimisation. A site that gets two different
// answers has detected us AND can average the difference away to recover the
// truth, which would leave the user perturbed, detectable and unprotected.
//
// **IDEMPOTENT: f(f(v)) == f(v). This is a hard requirement, not a nicety.**
// A canvas can be read, exported and re-read — toDataURL() -> decode ->
// drawImage -> getImageData is an ordinary thing for a page to do, and on a
// real browser a PNG round-trip is LOSSLESS. An earlier version perturbed at
// both ends, so the round trip drifted on 51 of 3072 subpixels. A browser whose
// lossless format is not lossless is trivially detectable, and that is a worse
// outcome than the entropy it was protecting.
//
// So a selected subpixel has its LOW BIT FORCED to a per-position target rather
// than being nudged in a per-position direction. Forcing has a fixed point;
// nudging does not.
//
// **The cost, stated plainly.** The perturbation is now a function of position
// alone, which makes the noise map learnable: a site can draw a known flat
// colour, read it back, and recover which positions are forced and to what.
// The earlier value-mixed design resisted that but could not be idempotent,
// because any function whose output depends on its own input cannot generally
// be applied twice without moving.
//
// Learning the map is NOT the same as recovering the pixels. Knowing that
// position p is forced to low bit b tells an attacker that the true value
// either already ended in b or was one step away — the low bit of every
// selected subpixel is destroyed, and no amount of probing restores it. The
// canvas hash still differs from the true device's. We trade a harder-to-learn
// map for a browser that cannot be spotted by a two-line round-trip check.
//
// **KNOWN LIMITATION.** This raises the cost of recovery; it does not make it
// impossible. A site willing to probe every (position, value) pair it cares
// about can still map the function, because the function must be deterministic
// or averaging defeats it instead. §6.5 chose determinism knowingly. The UI is
// allowed to say "these surfaces were randomized" and never "this device
// cannot be identified".

// Fraction of colour subpixels perturbed, as 1-in-N. Small enough to be
// invisible — every altered subpixel moves by at most one 8-bit step, below the
// threshold of perception and smaller than a JPEG round-trip — and dense enough
// that a canvas fingerprint, which hashes the whole buffer, changes with
// near-certainty.
inline constexpr uint32_t kZephyrusCanvasPerturbOneIn = 32;

// Perturbs tightly packed 8-bit RGBA in place.
//
// Alpha is never touched: it drives compositing rather than colour, so a change
// there is visible as a seam where two canvases meet, and fingerprinting
// scripts read the colour channels anyway.
// `rect_width` is the buffer width in pixels, and (`origin_x`, `origin_y`) its
// top-left corner IN CANVAS COORDINATES.
//
// These are not a convenience, they are the correctness fix. The first version
// keyed noise on the byte offset within the returned buffer, so one pixel got
// DIFFERENT noise depending on how it was read: getImageData(0,0,10,10) and
// getImageData(0,0,20,20) place pixel (5,5) at different offsets, and
// toDataURL at a third. A page reading overlapping rects, or the same canvas
// through two APIs, saw contradictory values for a single pixel — something no
// real GPU can produce, and trivially detectable. It made four of the top 200
// sites serve bot challenges.
//
// Keying on canvas position makes every read of a pixel agree, whatever rect or
// API asked for it.
PLATFORM_EXPORT void ApplyZephyrusCanvasNoise(
    base::span<const uint8_t, 32> seed,
    base::span<uint8_t> rgba,
    int rect_width,
    int origin_x,
    int origin_y);

// The perturbed value of a single subpixel. Exposed so tests can characterise
// the function directly, and so another surface with a different memory layout
// can reuse this derivation instead of inventing a second one that drifts.
PLATFORM_EXPORT uint8_t ZephyrusPerturbSubpixel(uint64_t canvas_key,
                                                int x,
                                                int y,
                                                int channel,
                                                uint8_t value);

// Derives the canvas surface key from the seed. Separate per surface so that
// reading the canvas noise teaches a page nothing about the WebGL or audio
// answers.
PLATFORM_EXPORT uint64_t ZephyrusCanvasKey(base::span<const uint8_t, 32> seed);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_GRAPHICS_ZEPHYRUS_CANVAS_NOISE_H_

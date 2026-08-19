// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/graphics/zephyrus_canvas_noise.h"

namespace blink {

namespace {

// SplitMix64. Chosen over a cryptographic hash for one reason: this runs once
// per colour subpixel on buffers that reach tens of millions of bytes, and
// HMAC there would turn a canvas read into a visible pause.
//
// Weaker than HMAC, and that is acceptable HERE but nowhere upstream. What
// protects the user is the SEED, which is HMAC-derived in the browser from a
// secret no page can observe. This function only spreads that secret across
// positions and values; recovering its state would reveal the noise for the one
// origin the attacker is already running on — not the secret, and not any other
// origin's noise.
constexpr uint64_t Mix(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

// Domain separation for the canvas surface, folded in as a constant. The seed
// is already secret, so separating surfaces does not itself need a keyed hash —
// it only has to make one surface's key unrelated to another's.
constexpr uint64_t kCanvasSurfaceTag = 0x63616e7661733031ULL;  // "canvas01"

}  // namespace

uint64_t ZephyrusCanvasKey(base::span<const uint8_t, 32> seed) {
  // Fold all 32 bytes in. Using only the first eight would discard three
  // quarters of the entropy the browser went to the trouble of deriving.
  uint64_t key = kCanvasSurfaceTag;
  for (size_t i = 0; i < seed.size(); i += 8) {
    uint64_t chunk = 0;
    for (size_t j = 0; j < 8; ++j) {
      chunk = (chunk << 8) | seed[i + j];
    }
    key = Mix(key ^ chunk);
  }
  return key;
}

uint8_t ZephyrusPerturbSubpixel(uint64_t canvas_key,
                                int x,
                                int y,
                                int channel,
                                uint8_t value) {
  // Position in the CANVAS, so any rect or API reading this pixel agrees.
  //
  // The hash deliberately does NOT include the value. That is what makes the
  // function IDEMPOTENT: f(f(v)) == f(v). See the header for why that property
  // is worth more than the value-mixing it replaces.
  const uint64_t pos = (static_cast<uint64_t>(x) * 0x9e3779b185ebca87ULL) ^
                       (static_cast<uint64_t>(y) * 0xc2b2ae3d27d4eb4fULL) ^
                       (static_cast<uint64_t>(channel) * 0x165667b19e3779f9ULL);
  const uint64_t h = Mix(canvas_key ^ Mix(pos));

  if (h % kZephyrusCanvasPerturbOneIn != 0) {
    return value;  // Untouched positions are trivially fixed points.
  }

  // At a selected position the low bit is FORCED to a per-position target
  // instead of the value being nudged in a per-position direction.
  //
  // Forcing is what buys idempotency: the output always has low bit == target,
  // so running the function again finds the bit already correct and returns the
  // value unchanged. A nudge would move the pixel again on every pass, which is
  // precisely the drift that made a lossless PNG round-trip lossy.
  const uint8_t target = static_cast<uint8_t>((h >> 8) & 1);
  if ((value & 1) == target) {
    return value;
  }
  // Either direction flips the low bit; step down only at the ceiling so the
  // result is still at most one 8-bit step away and never wraps.
  return value == 255 ? 254 : static_cast<uint8_t>(value + 1);
}

void ApplyZephyrusCanvasNoise(base::span<const uint8_t, 32> seed,
                              base::span<uint8_t> rgba,
                              int rect_width,
                              int origin_x,
                              int origin_y) {
  if (rect_width <= 0) {
    return;
  }
  const uint64_t canvas_key = ZephyrusCanvasKey(seed);
  const size_t pixels = rgba.size() / 4;
  for (size_t p = 0; p < pixels; ++p) {
    const int x = origin_x + static_cast<int>(p % rect_width);
    const int y = origin_y + static_cast<int>(p / rect_width);
    const size_t base = p * 4;
    rgba[base] = ZephyrusPerturbSubpixel(canvas_key, x, y, 0, rgba[base]);
    rgba[base + 1] =
        ZephyrusPerturbSubpixel(canvas_key, x, y, 1, rgba[base + 1]);
    rgba[base + 2] =
        ZephyrusPerturbSubpixel(canvas_key, x, y, 2, rgba[base + 2]);
    // Alpha deliberately untouched.
  }
}

}  // namespace blink

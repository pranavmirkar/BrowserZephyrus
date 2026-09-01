// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §6.5 canvas perturbation. §16's Phase 4 acceptance asks three things of this
// function — stable within an origin-session, unlinkable across origins, and an
// averaging attack over 1,000 samples failing to recover the true values — and
// all three are checkable here without a browser.
//
// The seeds below stand in for what the browser derives per (origin, session);
// the derivation itself is covered by fingerprint_seed_unittest.cc on the
// browser side. What matters here is only that different seeds produce
// different noise and that one seed always produces the same noise.

#include "third_party/blink/renderer/platform/graphics/zephyrus_canvas_noise.h"

#include <array>
#include <cmath>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace blink {
namespace {

std::array<uint8_t, 32> Seed(uint8_t fill) {
  std::array<uint8_t, 32> s;
  s.fill(fill);
  return s;
}

// Varied content, so no test accidentally characterises one flat colour.
std::vector<uint8_t> Buffer(size_t pixels) {
  std::vector<uint8_t> b(pixels * 4);
  for (size_t i = 0; i < b.size(); ++i) {
    b[i] = static_cast<uint8_t>((i * 37) & 0xff);
  }
  return b;
}

// STABLE. Everything rests on this: reading an unchanged canvas twice must give
// identical bytes, or the site both detects us and can average the noise away.
TEST(ZephyrusCanvasNoiseTest, TwoReadsOfTheSameCanvasAreIdentical) {
  const auto seed = Seed(0x11);
  std::vector<uint8_t> first = Buffer(512);
  std::vector<uint8_t> second = first;
  ApplyZephyrusCanvasNoise(seed, first, static_cast<int>(first.size() / 4), 0, 0);
  ApplyZephyrusCanvasNoise(seed, second, static_cast<int>(second.size() / 4), 0, 0);
  EXPECT_EQ(first, second);
}

// The §16 averaging attack, run for real. Because the function is
// deterministic, all 1,000 reads return the same bytes, so the mean sits on the
// perturbed value and never converges on the truth. A per-call design would
// fail here — which is exactly why the test is written this way round.
TEST(ZephyrusCanvasNoiseTest, AveragingOneThousandReadsDoesNotRecoverTheTruth) {
  const auto seed = Seed(0x11);
  const std::vector<uint8_t> truth = Buffer(256);

  std::vector<double> sums(truth.size(), 0.0);
  for (int i = 0; i < 1000; ++i) {
    std::vector<uint8_t> sample = truth;
    ApplyZephyrusCanvasNoise(seed, sample, static_cast<int>(sample.size() / 4), 0, 0);
    for (size_t j = 0; j < sample.size(); ++j) {
      sums[j] += sample[j];
    }
  }

  bool found_perturbed = false;
  for (size_t j = 0; j < truth.size(); ++j) {
    if (j % 4 == 3) {
      continue;  // alpha is never touched
    }
    const double mean = sums[j] / 1000.0;
    if (std::abs(mean - truth[j]) > 0.001) {
      found_perturbed = true;
      EXPECT_NEAR(mean, std::round(mean), 0.001)
          << "averaging revealed per-call variation at " << j;
    }
  }
  EXPECT_TRUE(found_perturbed) << "nothing was perturbed; test is vacuous";
}

// UNLINKABLE. Two origins must not share a perturbation, or the noise pattern
// itself becomes the cross-site identifier.
TEST(ZephyrusCanvasNoiseTest, DifferentSeedsPerturbDifferently) {
  std::vector<uint8_t> a = Buffer(512);
  std::vector<uint8_t> b = a;
  ApplyZephyrusCanvasNoise(Seed(0x11), a, static_cast<int>(a.size() / 4), 0, 0);
  ApplyZephyrusCanvasNoise(Seed(0x22), b, static_cast<int>(b.size() / 4), 0, 0);
  EXPECT_NE(a, b);
}

// Below visual significance: nothing may move by more than one 8-bit step.
TEST(ZephyrusCanvasNoiseTest, NoSubpixelMovesByMoreThanOne) {
  const std::vector<uint8_t> truth = Buffer(4096);
  std::vector<uint8_t> noised = truth;
  ApplyZephyrusCanvasNoise(Seed(0x11), noised, static_cast<int>(noised.size() / 4), 0, 0);
  for (size_t i = 0; i < truth.size(); ++i) {
    EXPECT_LE(std::abs(static_cast<int>(noised[i]) - truth[i]), 1)
        << "at subpixel " << i;
  }
}

// Alpha drives compositing, not colour. Perturbing it shows as a seam where two
// canvases meet, and buys nothing.
TEST(ZephyrusCanvasNoiseTest, AlphaIsNeverTouched) {
  const std::vector<uint8_t> truth = Buffer(4096);
  std::vector<uint8_t> noised = truth;
  ApplyZephyrusCanvasNoise(Seed(0x11), noised, static_cast<int>(noised.size() / 4), 0, 0);
  for (size_t i = 3; i < truth.size(); i += 4) {
    EXPECT_EQ(truth[i], noised[i]) << "alpha perturbed at " << i;
  }
}

// Clamping, not wrapping. A wrap would turn black into white on one subpixel in
// eight — a visible defect rather than a perturbation.
TEST(ZephyrusCanvasNoiseTest, ExtremesClampInsteadOfWrapping) {
  std::vector<uint8_t> black(4096 * 4, 0);
  std::vector<uint8_t> white(4096 * 4, 255);
  ApplyZephyrusCanvasNoise(Seed(0x11), black, static_cast<int>(black.size() / 4), 0, 0);
  ApplyZephyrusCanvasNoise(Seed(0x11), white, static_cast<int>(white.size() / 4), 0, 0);
  for (size_t i = 0; i < black.size(); ++i) {
    EXPECT_LE(black[i], 1u) << "black wrapped at " << i;
    EXPECT_GE(white[i], 254u) << "white wrapped at " << i;
  }
}

// A canvas fingerprint hashes the whole buffer, so the perturbation only works
// if it reliably lands somewhere in a realistically sized read.
TEST(ZephyrusCanvasNoiseTest, ARealisticCanvasIsMeaningfullyPerturbed) {
  const std::vector<uint8_t> truth = Buffer(16 * 16);
  std::vector<uint8_t> noised = truth;
  ApplyZephyrusCanvasNoise(Seed(0x11), noised, static_cast<int>(noised.size() / 4), 0, 0);
  int changed = 0;
  for (size_t i = 0; i < truth.size(); ++i) {
    changed += (truth[i] != noised[i]);
  }
  // Derived from the density rather than hardcoded, so tuning
  // kZephyrusCanvasPerturbOneIn does not silently invalidate this bound.
  // 16*16 pixels * 3 colour channels, 1-in-N selected, and roughly half of
  // those already carry the target bit.
  constexpr int kColourSubpixels = 16 * 16 * 3;
  constexpr int kExpected = kColourSubpixels / kZephyrusCanvasPerturbOneIn / 2;
  EXPECT_GT(changed, kExpected / 3);
  EXPECT_LT(changed, kExpected * 3);
}

// IDEMPOTENCE, the property this function exists to guarantee. A page can
// export a canvas and read it back; on a real browser a PNG round-trip is
// lossless, so perturbing twice must equal perturbing once.
TEST(ZephyrusCanvasNoiseTest, PerturbationIsIdempotent) {
  const uint64_t key = ZephyrusCanvasKey(Seed(0x11));
  for (int x = 0; x < 64; ++x) {
    for (int v = 0; v < 256; ++v) {
      const uint8_t once =
          ZephyrusPerturbSubpixel(key, x, 0, 0, static_cast<uint8_t>(v));
      const uint8_t twice = ZephyrusPerturbSubpixel(key, x, 0, 0, once);
      EXPECT_EQ(once, twice) << "drifted at x=" << x << " v=" << v;
    }
  }
}

// The same at buffer level: a whole canvas re-perturbed must not move.
TEST(ZephyrusCanvasNoiseTest, ReapplyingToABufferChangesNothing) {
  const auto seed = Seed(0x11);
  std::vector<uint8_t> once = Buffer(1024);
  ApplyZephyrusCanvasNoise(seed, once, 32, 0, 0);
  std::vector<uint8_t> twice = once;
  ApplyZephyrusCanvasNoise(seed, twice, 32, 0, 0);
  EXPECT_EQ(once, twice) << "a re-read drifts; PNG round-trips would not be "
                            "lossless and the browser is detectable";
}

// Idempotence must not have been bought by doing nothing: selected subpixels
// still lose their low bit, so the canvas hash still differs from the truth.
TEST(ZephyrusCanvasNoiseTest, LowBitIsStillDestroyedAtSelectedPositions) {
  const uint64_t key = ZephyrusCanvasKey(Seed(0x11));
  int forced = 0;
  for (int x = 0; x < 4096; ++x) {
    // A value and its neighbour differ in the low bit; if the position is
    // selected they must collapse onto the same output.
    if (ZephyrusPerturbSubpixel(key, x, 0, 0, 100) ==
        ZephyrusPerturbSubpixel(key, x, 0, 0, 101)) {
      ++forced;
    }
  }
  // Same derivation: 4096 positions at 1-in-N.
  EXPECT_GT(forced, 4096 / static_cast<int>(kZephyrusCanvasPerturbOneIn) / 2)
      << "nothing is being perturbed";
}

// The whole seed must participate; using only its first bytes would discard
// three quarters of the entropy the browser derived.
TEST(ZephyrusCanvasNoiseTest, EveryByteOfTheSeedAffectsTheKey) {
  std::array<uint8_t, 32> a = Seed(0x11);
  for (size_t i = 0; i < a.size(); ++i) {
    std::array<uint8_t, 32> b = a;
    b[i] ^= 0xff;
    EXPECT_NE(ZephyrusCanvasKey(a), ZephyrusCanvasKey(b))
        << "seed byte " << i << " is ignored";
  }
}

// The bug that made four of the top 200 sites serve bot challenges: one pixel
// read through different rects must get the SAME noise. Keyed on buffer offset
// it did not, and a page comparing overlapping reads saw a pixel contradict
// itself — impossible on real hardware.
TEST(ZephyrusCanvasNoiseTest, OverlappingReadsAgreeOnAPixel) {
  const auto seed = Seed(0x11);
  std::vector<uint8_t> whole(4 * 4 * 4, 128);
  ApplyZephyrusCanvasNoise(seed, whole, 4, 0, 0);

  std::vector<uint8_t> part(2 * 2 * 4, 128);
  ApplyZephyrusCanvasNoise(seed, part, 2, 2, 2);

  // Canvas pixel (2,2): index 10 in the 4x4 read, index 0 in the 2x2 read.
  for (int ch = 0; ch < 3; ++ch) {
    EXPECT_EQ(whole[10 * 4 + ch], part[0 * 4 + ch])
        << "pixel (2,2) disagrees between reads, channel " << ch;
  }
}

// Empty and partial buffers must not read past the end.
TEST(ZephyrusCanvasNoiseTest, DegenerateBuffersAreSafe) {
  std::vector<uint8_t> empty;
  ApplyZephyrusCanvasNoise(Seed(0x11), empty, static_cast<int>(empty.size() / 4), 0, 0);
  EXPECT_TRUE(empty.empty());

  std::vector<uint8_t> partial(3, 200);  // not a whole pixel
  const std::vector<uint8_t> before = partial;
  ApplyZephyrusCanvasNoise(Seed(0x11), partial, static_cast<int>(partial.size() / 4), 0, 0);
  EXPECT_EQ(before, partial) << "a partial pixel must be left alone";
}

}  // namespace
}  // namespace blink

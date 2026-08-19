// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §6.5's attempt heuristic. The tests are mostly about what must NOT be called
// fingerprinting: §16 budgets zero false positives, and every surface on the
// list has a legitimate use that would otherwise be accused.

#include "chrome/browser/zephyrus/privacy/fingerprint_attempt.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

AttemptInputs Inputs(uint32_t surfaces, base::TimeDelta elapsed) {
  AttemptInputs in;
  in.distinct_surfaces = surfaces;
  in.elapsed = elapsed;
  return in;
}

TEST(FingerprintAttemptTest, ThreeSurfacesInTheWindowIsAnAttempt) {
  EXPECT_EQ(TrackerStatus::kDetected,
            ClassifyFingerprintAttempt(Inputs(3, base::Seconds(1))));
}

TEST(FingerprintAttemptTest, MoreThanThreeStillQualifies) {
  EXPECT_EQ(TrackerStatus::kDetected,
            ClassifyFingerprintAttempt(Inputs(6, base::Seconds(4))));
}

// A charting library reading a canvas, and nothing else. The single most
// important non-accusation: this must never be reported as fingerprinting.
TEST(FingerprintAttemptTest, OneSurfaceIsOnlyPotential) {
  EXPECT_EQ(TrackerStatus::kPotential,
            ClassifyFingerprintAttempt(Inputs(1, base::Seconds(1))));
}

TEST(FingerprintAttemptTest, TwoSurfacesIsOnlyPotential) {
  EXPECT_EQ(TrackerStatus::kPotential,
            ClassifyFingerprintAttempt(Inputs(2, base::Seconds(1))));
}

// The window is what stops the heuristic degenerating. A long-lived app — a
// document editor, a game — will touch three surfaces eventually; spread over
// minutes that is use, not a sweep.
TEST(FingerprintAttemptTest, ThreeSurfacesSpreadOutIsOnlyPotential) {
  EXPECT_EQ(TrackerStatus::kPotential,
            ClassifyFingerprintAttempt(Inputs(3, base::Seconds(30))));
}

TEST(FingerprintAttemptTest, TheWindowBoundaryIsInclusive) {
  EXPECT_EQ(TrackerStatus::kDetected,
            ClassifyFingerprintAttempt(Inputs(3, kAttemptWindow)));
  EXPECT_EQ(TrackerStatus::kPotential,
            ClassifyFingerprintAttempt(
                Inputs(3, kAttemptWindow + base::Milliseconds(1))));
}

// Sufficient alone, and deliberately not subject to the window or the count:
// reading back a canvas that was never shown to the user has no innocent
// reading.
TEST(FingerprintAttemptTest, AnUnpaintedCanvasReadIsAnAttemptByItself) {
  AttemptInputs in = Inputs(1, base::Hours(1));
  in.canvas_read_never_painted = true;
  EXPECT_EQ(TrackerStatus::kDetected, ClassifyFingerprintAttempt(in));
}

TEST(FingerprintAttemptTest, NothingTouchedIsPotential) {
  EXPECT_EQ(TrackerStatus::kPotential,
            ClassifyFingerprintAttempt(Inputs(0, base::Seconds(0))));
}

// The heuristic reports what was ATTEMPTED. Whether anything was perturbed is a
// different fact, and must not be inferred from this one.
TEST(FingerprintAttemptTest, NeverClaimsRandomized) {
  for (uint32_t n = 0; n < 8; ++n) {
    for (int secs : {0, 1, 5, 60}) {
      const TrackerStatus s =
          ClassifyFingerprintAttempt(Inputs(n, base::Seconds(secs)));
      EXPECT_NE(TrackerStatus::kRandomized, s);
      EXPECT_NE(TrackerStatus::kBlocked, s);
      EXPECT_NE(TrackerStatus::kAllowed, s);
    }
  }
}

}  // namespace
}  // namespace zephyrus_privacy

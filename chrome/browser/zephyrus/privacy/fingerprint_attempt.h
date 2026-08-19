// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_ATTEMPT_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_ATTEMPT_H_

#include <stdint.h>

#include "base/time/time.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"

namespace zephyrus_privacy {

// §6.5's attempt heuristic: the rule that separates "this page is
// fingerprinting you" from "this page touched an API that fingerprinters also
// touch".
//
// **Why a heuristic is needed at all.** Every surface on the §6.5 list has
// legitimate uses. A charting library reads canvas pixels; a game reads the
// WebGL renderer to pick a quality preset; a video call reads the device list.
// Reporting each of those as a fingerprinting attempt would fill the user's
// timeline with accusations against ordinary sites, and §16 budgets zero false
// positives on the claims this product makes.
//
// **What separates the two is CONCENTRATION.** A fingerprinting script sweeps
// several unrelated surfaces in one burst, because its value comes from
// combining them. A chart library reads a canvas and nothing else.
//
// Below the bar the event is still recorded, as POTENTIAL — §2.1's "heuristic
// match; not certain" — and POTENTIAL is excluded from every headline count, so
// an uncertain signal can never inflate a number the user reads.

// §6.5: "≥ 3 of the above surfaces touched within 5 s of load."
inline constexpr uint32_t kAttemptSurfaceThreshold = 3;
inline constexpr base::TimeDelta kAttemptWindow = base::Seconds(5);

struct AttemptInputs {
  // Distinct surfaces touched, never call count. A loop calling one API a
  // thousand times has learned one thing, and counting calls would let a page
  // inflate its own reading.
  uint32_t distinct_surfaces = 0;

  // How long after the burst began the current surface was touched.
  base::TimeDelta elapsed;

  // §6.5's other trigger, on its own: "a canvas read of a surface never painted
  // to screen".
  //
  // This one needs no corroboration because it has no innocent reading. Drawing
  // to a canvas and never showing it, then reading the pixels back, is not what
  // a chart or an image editor does — the whole point of those is that the user
  // sees the result. It is precisely what a fingerprinting script does, because
  // it wants the GPU's rendering of known input, not a picture.
  bool canvas_read_never_painted = false;
};

// DETECTED when the evidence supports the claim, POTENTIAL when it does not.
//
// Never returns kRandomized: whether anything was perturbed is a separate fact
// about what the browser DID, and conflating it with what the page ATTEMPTED
// would let the report claim a protection on the strength of a detection.
TrackerStatus ClassifyFingerprintAttempt(const AttemptInputs& inputs);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_FINGERPRINT_ATTEMPT_H_

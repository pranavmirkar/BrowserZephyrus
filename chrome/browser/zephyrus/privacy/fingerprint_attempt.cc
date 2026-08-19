// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/fingerprint_attempt.h"

namespace zephyrus_privacy {

TrackerStatus ClassifyFingerprintAttempt(const AttemptInputs& inputs) {
  // Sufficient on its own — see the header for why an unpainted canvas read has
  // no innocent reading.
  if (inputs.canvas_read_never_painted) {
    return TrackerStatus::kDetected;
  }

  // Both conditions, deliberately. Three surfaces spread over a long session is
  // an ordinary site gradually using ordinary APIs; three in a five-second
  // burst is a sweep. Dropping the window would make any sufficiently
  // long-lived page eventually qualify, which is how a heuristic quietly turns
  // into "everything is fingerprinting".
  if (inputs.distinct_surfaces >= kAttemptSurfaceThreshold &&
      inputs.elapsed <= kAttemptWindow) {
    return TrackerStatus::kDetected;
  }

  // §2.1: "heuristic match; not certain". Recorded, but excluded from every
  // headline count.
  return TrackerStatus::kPotential;
}

}  // namespace zephyrus_privacy

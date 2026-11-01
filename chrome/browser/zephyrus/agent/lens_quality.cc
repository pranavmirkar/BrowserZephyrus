// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_quality.h"

namespace zephyrus::agent {

void LensQualityGovernor::Reset() {
  *this = LensQualityGovernor();
}

void LensQualityGovernor::OnFrame(double interval, bool heavy) {
  if (!(interval > 0) || interval > 0.25) {
    // A stall (the window was hidden, the machine slept) says nothing about how
    // fast frames are when it is running.
    return;
  }
  // The prototype's smoothing: dtAvg += (dt - dtAvg) * 0.04.
  average_ += (interval - average_) * 0.04;
  since_change_ += interval;

  if (!heavy) {
    // Static has no blurs to drop, so there is nothing to decide, and slow
    // frames while it is showing are not a reason to hold the others back.
    slow_for_ = 0;
    fast_for_ = 0;
    return;
  }
  if (!lite_) {
    slow_for_ = average_ > kSlowAverage ? slow_for_ + interval : 0;
    if (slow_for_ >= kSlowFor && since_change_ >= kMinHold) {
      lite_ = true;
      since_change_ = 0;
      slow_for_ = 0;
      fast_for_ = 0;
    }
  } else {
    fast_for_ = average_ < kFastAverage ? fast_for_ + interval : 0;
    if (fast_for_ >= kFastFor && since_change_ >= kMinHold) {
      lite_ = false;
      since_change_ = 0;
      slow_for_ = 0;
      fast_for_ = 0;
    }
  }
}

}  // namespace zephyrus::agent

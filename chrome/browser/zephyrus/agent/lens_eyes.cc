// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_eyes.h"

#include <algorithm>

#include "chrome/browser/zephyrus/agent/lens_math.h"

namespace zephyrus::agent {

double LensLid::CenterGap() const {
  // The lower lid is a quadratic from (-16, yb) via (0, yc) to (16, yb); at
  // x = 0 it is at (yb + 2 yc + yb) / 4. The upper lid at x = 0 is the midpoint
  // of a and b.
  const double lower_mid = (2.0 * yb + 2.0 * yc) / 4.0;
  const double upper_mid = (a + b) / 2.0;
  return lower_mid - upper_mid;
}

LensLid ComputeLid(double open,
                   double tilt,
                   double cheek,
                   double blink,
                   bool left_eye) {
  const double inner = left_eye ? 1.0 : -1.0;
  const double o = LensClamp(open, 0.0, 1.05) * (1.0 - blink);
  const double yl = -(0.8 + 11.8 * o);
  LensLid lid;
  lid.a = yl - tilt * inner;
  lid.b = yl + tilt * inner;
  const double c = LensClamp(cheek, 0.0, 1.1);
  lid.yb = 6.0 - 8.2 * c;
  const double ym = std::max(6.0 - 15.0 * c, yl + 0.9);
  lid.yc = 2.0 * ym - lid.yb;
  return lid;
}

double LensBlinkValue(const LensBlink& blink, double t) {
  const double p = (t - blink.t0) / blink.d;
  if (p < 0.0) {
    return 0.0;
  }
  if (p < 0.075) {
    return LensEaseOut(p / 0.075);
  }
  if (p < 0.11) {
    return 1.0;
  }
  if (p < 0.25) {
    return 1.0 - LensEaseInOut((p - 0.11) / 0.14);
  }
  return 0.0;
}

double LensBlinksValue(const std::vector<LensBlink>& blinks, double t) {
  double best = 0.0;
  for (const LensBlink& b : blinks) {
    best = std::max(best, LensBlinkValue(b, t));
  }
  return best;
}

}  // namespace zephyrus::agent

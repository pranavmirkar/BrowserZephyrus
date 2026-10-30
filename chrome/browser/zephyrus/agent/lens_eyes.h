// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_EYES_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_EYES_H_

#include <vector>

namespace zephyrus::agent {

// The visible part of an eye is its dome clipped by a "lid" shape, in
// eye-local coordinates (origin at the centre of the dome's flat base, up is
// negative):
//
//   M -16 a   L 16 b   L 16 yb   Q 0 yc  -16 yb   Z
//
// The straight top edge from `a` (left end) to `b` (right end) is the UPPER
// lid; the quadratic along the bottom, with control height `yc`, is the LOWER
// lid, which arches up into a crescent for a smile (cheek raise).
struct LensLid {
  double a = 0;
  double b = 0;
  double yb = 0;
  double yc = 0;

  // The height of the visible gap at the middle of the eye, in eye units: where
  // the lower lid's curve passes minus the upper lid's centre. Positive means
  // there is something to see.
  double CenterGap() const;
};

// `open` 0..1.05 (upper lid height), `tilt` the slant of the upper lid
// (positive lowers the inner end, a frown; negative raises it), `cheek` 0..1.1
// (lower lid arch), `blink` 0..1 (1 = closed). `left_eye` selects which end is
// the inner one.
LensLid ComputeLid(double open,
                   double tilt,
                   double cheek,
                   double blink,
                   bool left_eye);

// One blink: starts at `t0`, lasts `d` seconds (a slow blink has d > 1).
struct LensBlink {
  double t0 = 0;
  double d = 1;
};

// How closed the eye is at time `t` because of one blink: it closes in 7.5% of
// `d`, holds until 11%, and is open again by 25%. The right eye is asked at
// `t - 0.012`, so it lags the left by 12 ms.
double LensBlinkValue(const LensBlink& blink, double t);
double LensBlinksValue(const std::vector<LensBlink>& blinks, double t);

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_EYES_H_

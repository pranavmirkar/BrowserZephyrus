// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MATH_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MATH_H_

#include <algorithm>
#include <cmath>

// Small numeric helpers shared by the "lens" mascot model. Pure functions, no
// state. Written to match the reference prototype's helpers exactly
// (agent-mascot.html: clamp, lerp, easeOut, easeInOut, beat), because the
// native port is checked against the prototype and a different easing curve is
// a visible difference.

namespace zephyrus::agent {

inline constexpr double kLensTau = 6.283185307179586;

inline double LensClamp(double x, double lo, double hi) {
  return std::min(hi, std::max(lo, x));
}

inline double LensLerp(double a, double b, double t) {
  return a + (b - a) * t;
}

inline double LensEaseOut(double t) {
  const double m = 1.0 - t;
  return 1.0 - m * m * m;
}

inline double LensEaseInOut(double t) {
  if (t < 0.5) {
    return 2.0 * t * t;
  }
  const double m = -2.0 * t + 2.0;
  return 1.0 - m * m / 2.0;
}

// A heartbeat: a sharp pulse and a smaller one just after it. `x` is in beats.
// std::fmod keeps the sign of its first argument, as JavaScript's % does.
inline double LensBeat(double x) {
  const double f = std::fmod(x, 1.0);
  const double a = (f - 0.05) / 0.05;
  const double b = (f - 0.27) / 0.06;
  return std::exp(-a * a) + 0.65 * std::exp(-b * b);
}

struct LensRgb {
  double r = 0;
  double g = 0;
  double b = 0;
};

inline LensRgb LensMix(const LensRgb& a, const LensRgb& b, double t) {
  return {LensLerp(a.r, b.r, t), LensLerp(a.g, b.g, t), LensLerp(a.b, b.b, t)};
}

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_MATH_H_

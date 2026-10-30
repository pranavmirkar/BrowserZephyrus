// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_GEOMETRY_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_GEOMETRY_H_

#include <array>
#include <optional>

// The lens mascot's body, in the artwork's own coordinate space (the original
// SVG): x from 0 to 251, y from 134.408 to 385.605, eyes on the base line
// y = 260.007. Everything here is pure and unit tested; the painter only turns
// the numbers into Skia.

namespace zephyrus::agent {

inline constexpr double kLensBodyLeft = 0.0;
inline constexpr double kLensBodyRight = 251.0;
inline constexpr double kLensBodyTop = 134.408;
inline constexpr double kLensBodyBottom = 385.605;
inline constexpr double kLensEyeBaseY = 260.007;
inline constexpr double kLensCenterX = 125.5;

struct LensPoint {
  double x = 0;
  double y = 0;
};

using LensCubic = std::array<LensPoint, 4>;

// The body path is two cubic Beziers joined by two straight vertical edges:
//   M0 134.408 C a1 a2 251 134.408  L251 385.605  C b1 b2 0 385.605  Z
// `a` runs along the top (left to right), `b` along the bottom (right to
// left). The curves cross twice, and those crossings are the pivots of the
// original design.
struct LensBodyControls {
  LensCubic a;
  LensCubic b;
};

// u = 1 is the original right-slanted shape, u = 0 its mirror image. Control
// points interpolate between the two; u is clamped to [-0.06, 1.06] so a spring
// can overshoot slightly, and control x values are clamped to [0, 251] so the
// curves never poke outside the side edges. `k` scales every control point's
// y offset about the eye line (breathing, puffed and deflated moods).
LensBodyControls ComputeBodyControls(double u, double k);

LensPoint CubicAt(const LensCubic& p, double t);

struct LensPivots {
  LensPoint left;
  LensPoint right;
};

// Where the two curves cross: the leftmost and rightmost crossing, found on a
// 36-segment polyline of each. Empty if they do not cross twice.
std::optional<LensPivots> FindLensPivots(double u, double k);

// How the body-fill layers stack, bottom to top: Static, Fluid, Opal, Space.
// The weights of the layers sum to 1. The returned OPACITIES are what each
// layer is drawn at so that, composited in that order, each contributes exactly
// its weight.
struct LensLayerOpacities {
  double stat_weight = 1;  // the static layer's own weight (drawn opaque)
  double fluid = 0;
  double opal = 0;
  double space = 0;
  bool show_static = true;
  bool show_fluid = false;
  bool show_opal = false;
  bool show_space = false;
};
LensLayerOpacities ComputeLayerOpacities(double w_fluid,
                                         double w_opal,
                                         double w_space);

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_GEOMETRY_H_

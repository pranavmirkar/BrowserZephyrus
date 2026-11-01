// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_geometry.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "chrome/browser/zephyrus/agent/lens_math.h"

namespace zephyrus::agent {

LensBodyControls ComputeBodyControls(double u, double k) {
  u = LensClamp(u, -0.06, 1.06);
  const auto L = [u](double a, double b) { return LensLerp(a, b, u); };
  const auto Y = [k](double y) {
    return kLensEyeBaseY + (y - kLensEyeBaseY) * k;
  };
  const auto X = [](double x) {
    return LensClamp(x, kLensBodyLeft, kLensBodyRight);
  };
  LensBodyControls c;
  c.a = {{{0.0, kLensBodyTop},
          {X(L(47.6865, 0.0)), Y(L(282.23, 569.607))},
          {X(L(251.0, 203.313)), Y(L(569.607, 282.23))},
          {251.0, kLensBodyTop}}};
  c.b = {{{251.0, kLensBodyBottom},
          {X(L(203.313, 251.0)), Y(L(237.783, -49.594))},
          {X(L(0.0, 47.6865)), Y(L(-49.594, 237.783))},
          {0.0, kLensBodyBottom}}};
  return c;
}

LensPoint CubicAt(const LensCubic& p, double t) {
  const double m = 1.0 - t;
  const double w0 = m * m * m;
  const double w1 = 3.0 * m * m * t;
  const double w2 = 3.0 * m * t * t;
  const double w3 = t * t * t;
  return {w0 * p[0].x + w1 * p[1].x + w2 * p[2].x + w3 * p[3].x,
          w0 * p[0].y + w1 * p[1].y + w2 * p[2].y + w3 * p[3].y};
}

std::optional<LensPivots> FindLensPivots(double u, double k) {
  const LensBodyControls c = ComputeBodyControls(u, k);
  constexpr int kN = 36;
  std::vector<LensPoint> a;
  std::vector<LensPoint> b;
  for (int i = 0; i <= kN; ++i) {
    a.push_back(CubicAt(c.a, static_cast<double>(i) / kN));
    b.push_back(CubicAt(c.b, static_cast<double>(i) / kN));
  }
  std::vector<LensPoint> hits;
  for (int i = 0; i < kN; ++i) {
    for (int j = 0; j < kN; ++j) {
      const LensPoint p = a[i];
      const LensPoint q = b[j];
      const double rx = a[i + 1].x - p.x;
      const double ry = a[i + 1].y - p.y;
      const double sx = b[j + 1].x - q.x;
      const double sy = b[j + 1].y - q.y;
      const double d = rx * sy - ry * sx;
      if (std::abs(d) < 1e-9) {
        continue;
      }
      const double t = ((q.x - p.x) * sy - (q.y - p.y) * sx) / d;
      const double w = ((q.x - p.x) * ry - (q.y - p.y) * rx) / d;
      if (t >= 0 && t <= 1 && w >= 0 && w <= 1) {
        hits.push_back({p.x + t * rx, p.y + t * ry});
      }
    }
  }
  if (hits.size() < 2) {
    return std::nullopt;
  }
  std::sort(hits.begin(), hits.end(),
            [](const LensPoint& m, const LensPoint& n) { return m.x < n.x; });
  return LensPivots{hits.front(), hits.back()};
}

LensLayerOpacities ComputeLayerOpacities(double w_fluid,
                                         double w_opal,
                                         double w_space) {
  const double wF = LensClamp(w_fluid, 0, 1);
  const double wO = LensClamp(w_opal, 0, 1);
  const double wP = LensClamp(w_space, 0, 1);
  const double wS = std::max(0.0, 1.0 - wF - wO - wP);
  LensLayerOpacities out;
  out.stat_weight = wS;
  out.show_static = wS > 0.001;
  out.show_fluid = wF > 0.001;
  out.show_opal = wO > 0.001;
  out.show_space = wP > 0.001;
  // Each opacity is chosen so the layer contributes exactly its weight once the
  // layers above it have been composited:
  //   space = wP
  //   opal  = wO / (1 - wP)
  //   fluid = wF / (wF + wS)
  // Static, underneath, is always opaque and is hidden when wS < 0.001.
  out.space = wP;
  const double above_opal = 1.0 - wP;
  out.opal = wO / (above_opal != 0.0 ? above_opal : 1.0);
  const double base = wF + wS;
  out.fluid = wF / (base != 0.0 ? base : 1.0);
  return out;
}

}  // namespace zephyrus::agent

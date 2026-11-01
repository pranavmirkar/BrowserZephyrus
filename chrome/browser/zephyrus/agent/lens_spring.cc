// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_spring.h"

#include <algorithm>
#include <cmath>

namespace zephyrus::agent {

void LensSpring::Step(double dt) {
  if (!(dt > 0)) {
    return;
  }
  const int n = std::max(1, static_cast<int>(std::ceil(dt * 120.0)));
  const double h = dt / n;
  for (int i = 0; i < n; ++i) {
    const double a = w * w * (t - x) - 2.0 * z * w * v;
    v += a * h;
    x += v * h;
  }
}

}  // namespace zephyrus::agent

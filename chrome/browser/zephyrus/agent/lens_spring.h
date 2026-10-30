// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_SPRING_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_SPRING_H_

namespace zephyrus::agent {

// A damped spring chasing a target: a = w^2 (target - x) - 2 z w v.
//
// Everything about the lens mascot's motion is one of these. A state never
// animates anything directly; each frame it writes a new TARGET and the springs
// chase it, which is why moving between two states is never a cut.
//
// Semi-implicit Euler in steps of at most 1/120 s, so a long frame (the caller
// caps it at 50 ms) cannot make a stiff spring blow up. `z` = 1 is critically
// damped (no overshoot); below 1 it overshoots a little, which is what makes a
// pose "pop".
struct LensSpring {
  LensSpring() = default;
  LensSpring(double x0, double omega, double zeta)
      : x(x0), t(x0), w(omega), z(zeta) {}

  void Step(double dt);

  double x = 0;  // position
  double v = 0;  // velocity
  double t = 0;  // target
  double w = 14;
  double z = 1;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_SPRING_H_

// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_POINTER_PATH_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_POINTER_PATH_H_

#include <stdint.h>

#include "base/time/time.h"
#include "ui/gfx/geometry/point_f.h"

namespace zephyrus::agent {

// How a hand moves a pointer, as opposed to how a program teleports one.
//
// Two things separate them and both are here. A hand travels along a slightly
// curved line rather than a straight one, and it does not move at constant
// speed: it accelerates, cruises and eases into the target, taking longer for
// a longer reach (Fitts's law) but never in proportion. A pointer that jumps,
// or that slides at a fixed speed along a ruler-straight line, is what a
// script looks like -- to a person watching, and to any page that watches for
// one.
//
// Pure arithmetic on purpose. It has no clock, no view and no events, so its
// shape can be tested directly and the same path drives the real input events
// and the mascot that is drawn as the pointer.

// The smooth-step of a human reach: zero speed and zero acceleration at both
// ends, `t` in [0, 1] in and out. Measured human movements fit it closely.
double MinimumJerk(double t);

class PointerPath {
 public:
  // `seed` picks which way the path bows and by how much, so the same reach
  // is not the same curve twice, and two runs of a test are the same run.
  PointerPath(gfx::PointF from, gfx::PointF to, uint32_t seed);

  base::TimeDelta duration() const { return duration_; }

  // The pointer at `elapsed` into the move. Exactly `to` from `duration()` on,
  // exactly `from` at zero: whatever the curve does in between, the click lands
  // where it was aimed.
  gfx::PointF At(base::TimeDelta elapsed) const;

 private:
  gfx::PointF from_;
  gfx::PointF to_;
  gfx::PointF control1_;
  gfx::PointF control2_;
  base::TimeDelta duration_;
};

// How long the mascot needs to WALK `distance` dip, from a standing start to
// standing at the destination: its top speed, plus the moment it takes to get
// up to it and to settle. Used to decide, before setting off, whether it can be
// there when the thing it is going to do happens -- see
// ZephyrusAgentMascotOverlay::GoTo.
base::TimeDelta EstimateWalkTime(float distance);

// How long a scroll of `distance` pixels takes to roll, in the same spirit: a
// flick for a short one, a longer glide for a page, never instant.
base::TimeDelta ScrollDuration(float distance);

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_POINTER_PATH_H_

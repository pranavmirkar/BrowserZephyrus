// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/pointer_path.h"

#include <algorithm>
#include <cmath>

namespace zephyrus::agent {

namespace {

// A small deterministic generator. Not for anything that needs to be
// unpredictable: it only has to make two paths differ.
float Unit(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return static_cast<float>((state >> 8) & 0xFFFF) / 65535.0f;
}

gfx::PointF Bezier(gfx::PointF p0,
                   gfx::PointF p1,
                   gfx::PointF p2,
                   gfx::PointF p3,
                   float t) {
  const float u = 1.0f - t;
  const float a = u * u * u;
  const float b = 3.0f * u * u * t;
  const float c = 3.0f * u * t * t;
  const float d = t * t * t;
  return gfx::PointF(a * p0.x() + b * p1.x() + c * p2.x() + d * p3.x(),
                     a * p0.y() + b * p1.y() + c * p2.y() + d * p3.y());
}

}  // namespace

double MinimumJerk(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return t * t * t * (10.0 + t * (-15.0 + 6.0 * t));
}

PointerPath::PointerPath(gfx::PointF from, gfx::PointF to, uint32_t seed)
    : from_(from), to_(to) {
  const float dx = to.x() - from.x();
  const float dy = to.y() - from.y();
  const float distance = std::hypot(dx, dy);

  // Fitts: time grows with the LOG of distance over target size. 30px stands
  // for a typical target. A short hop is a flick and a reach across the whole
  // window is a third of a second: quick enough that nothing waits on it, slow
  // enough to read as a hand and not a jump.
  const double millis = std::clamp(
      100.0 + 60.0 * std::log2(1.0 + distance / 30.0), 90.0, 340.0);
  duration_ = base::Milliseconds(static_cast<int64_t>(millis));

  // The bow: both control points pushed to the same side of the straight line,
  // by a few percent of the distance. A hand arcs, and which way depends on the
  // wrist. Nothing for a very short reach, where a curve would only look like
  // a wobble.
  uint32_t state = seed ? seed : 1u;
  const float side = Unit(state) < 0.5f ? -1.0f : 1.0f;
  const float bow = distance < 24.0f
                        ? 0.0f
                        : side * distance * (0.04f + 0.06f * Unit(state));
  const float nx = distance > 0 ? -dy / distance : 0.0f;
  const float ny = distance > 0 ? dx / distance : 0.0f;
  control1_ = gfx::PointF(from.x() + dx * 0.30f + nx * bow,
                          from.y() + dy * 0.30f + ny * bow);
  control2_ = gfx::PointF(from.x() + dx * 0.72f + nx * bow * 0.6f,
                          from.y() + dy * 0.72f + ny * bow * 0.6f);
}

gfx::PointF PointerPath::At(base::TimeDelta elapsed) const {
  if (elapsed <= base::TimeDelta()) {
    return from_;
  }
  if (elapsed >= duration_) {
    return to_;
  }
  const double progress = elapsed.InSecondsF() / duration_.InSecondsF();
  return Bezier(from_, control1_, control2_, to_,
                static_cast<float>(MinimumJerk(progress)));
}

base::TimeDelta EstimateWalkTime(float distance) {
  // Must match the overlay's locomotion: top speed 800 dip a second, reached by
  // easing in over about a fifth of a second.
  constexpr float kTopSpeed = 800.0f;
  constexpr double kEaseSeconds = 0.2;
  return base::Seconds(std::abs(distance) / kTopSpeed + kEaseSeconds);
}

base::TimeDelta ScrollDuration(float distance) {
  const double millis = std::clamp(
      130.0 + 55.0 * std::log2(1.0 + std::abs(distance) / 120.0), 130.0, 380.0);
  return base::Milliseconds(static_cast<int64_t>(millis));
}

}  // namespace zephyrus::agent

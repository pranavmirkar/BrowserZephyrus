// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_icon_motion.h"

#include <cmath>
#include <numbers>
#include <utility>

#include "chrome/browser/ui/views/frame/zephyrus_m3.h"

namespace zephyrus {

IconMotion::IconMotion(base::RepeatingClosure on_change,
                       base::TimeDelta click_duration)
    : on_change_(std::move(on_change)),
      hover_(this),
      press_(this),
      click_(this) {
  hover_.SetTweenType(gfx::Tween::LINEAR);
  hover_.SetSlideDuration(base::Milliseconds(260));
  press_.SetTweenType(gfx::Tween::LINEAR);
  press_.SetSlideDuration(base::Milliseconds(100));
  click_.SetTweenType(gfx::Tween::LINEAR);
  click_.SetSlideDuration(click_duration);
}

IconMotion::~IconMotion() = default;

void IconMotion::SetHovered(bool hovered) {
  Retarget(hover_, hovered);
}

void IconMotion::SetPressed(bool pressed) {
  Retarget(press_, pressed);
}

void IconMotion::Click() {
  if (gfx::Animation::ShouldRenderRichAnimation()) {
    click_.Reset(0.0);
    click_.Show();
  }
}

float IconMotion::Hover() const {
  return zephyrus::m3::Curve(zephyrus::m3::Spring::kFastSpatial)
      .Solve(hover_.GetCurrentValue());
}

float IconMotion::Press() const {
  return zephyrus::m3::Curve(zephyrus::m3::Spring::kFastEffects)
      .Solve(press_.GetCurrentValue());
}

float IconMotion::Pop() const {
  return std::sin(std::numbers::pi_v<float> *
                  static_cast<float>(click_.GetCurrentValue()));
}

float IconMotion::Turn() const {
  return zephyrus::m3::Curve(zephyrus::m3::Spring::kDefaultSpatial)
      .Solve(click_.GetCurrentValue());
}

bool IconMotion::IsAnimating() const {
  return hover_.is_animating() || press_.is_animating() ||
         click_.is_animating();
}

void IconMotion::AnimationProgressed(const gfx::Animation* animation) {
  on_change_.Run();
}

void IconMotion::AnimationEnded(const gfx::Animation* animation) {
  on_change_.Run();
}

void IconMotion::Retarget(gfx::SlideAnimation& animation, bool show) {
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    animation.Reset(show ? 1.0 : 0.0);
    on_change_.Run();
  } else if (show) {
    animation.Show();
  } else {
    animation.Hide();
  }
}

}  // namespace zephyrus

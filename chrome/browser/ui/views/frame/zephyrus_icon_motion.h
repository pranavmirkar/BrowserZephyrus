// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ICON_MOTION_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ICON_MOTION_H_

#include "base/functional/callback.h"
#include "base/time/time.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"

namespace zephyrus {

// The motion of one small icon, in the way of the M3 Expressive icon button: a
// spring on hover, a dip while pressed, and a pop (or a turn) on click.
//
// It owns the three animations and nothing else. Whatever draws the icon reads
// Hover() / Press() / Pop() / Turn() when it draws (or applies them as a
// transform) and is told through `on_change` to draw again whenever one moves.
// Reduced motion is honoured here: the state is taken at once and no click
// motion plays.
class IconMotion : public gfx::AnimationDelegate {
 public:
  explicit IconMotion(base::RepeatingClosure on_change,
                      base::TimeDelta click_duration = base::Milliseconds(260));
  IconMotion(const IconMotion&) = delete;
  IconMotion& operator=(const IconMotion&) = delete;
  ~IconMotion() override;

  void SetHovered(bool hovered);
  void SetPressed(bool pressed);
  void Click();

  // 0..1, springing (it overshoots a little).
  float Hover() const;
  float Press() const;
  // 0 -> 1 over a click; sin(pi * this) is the pop.
  float Pop() const;
  // 0 -> 1 over a click, springing past 1 and settling: a full turn.
  float Turn() const;

  // True while any of the three still has somewhere to go.
  bool IsAnimating() const;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  void Retarget(gfx::SlideAnimation& animation, bool show);

  base::RepeatingClosure on_change_;
  gfx::SlideAnimation hover_;
  gfx::SlideAnimation press_;
  gfx::SlideAnimation click_;
};

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ICON_MOTION_H_

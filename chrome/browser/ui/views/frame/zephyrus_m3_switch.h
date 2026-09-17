// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_SWITCH_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_SWITCH_H_

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/views/controls/button/button.h"

namespace zephyrus::m3 {

// An M3 SWITCH, drawn to Material's SwitchTokens rather than restyled from
// views::ToggleButton, whose track and thumb proportions are Chromium's own.
//
//   track   52x32, full-round
//   off     track surfaceContainerHighest with a 2dp `outline` border,
//           16dp thumb in `outline`
//   on      track `primary`, 24dp thumb in `onPrimary`
//   pressed the thumb grows to 28dp in either state
//   hover   the thumb takes onSurfaceVariant (off) / primaryContainer (on),
//           under a 40dp state layer
//
// The view is 60x40 rather than 52x32, because the 40dp state layer around the
// thumb reaches 4dp past the track on every side at either end of its travel.
//
// Behaves like views::ToggleButton: a click flips the state first and then runs
// the pressed callback, so the callback reads the NEW value. SetIsOn() is
// programmatic and does not run it.
// views::Button is ALREADY an AnimationDelegateViews and already owns the paint
// entry point, so this follows views::ToggleButton exactly: draw in
// PaintButtonContents (Button::OnPaint is final), and chain AnimationProgressed
// to the base for the hover animation Button runs itself.
class Switch : public views::Button {
  METADATA_HEADER(Switch, views::Button)

 public:
  explicit Switch(PressedCallback callback = PressedCallback());
  Switch(const Switch&) = delete;
  Switch& operator=(const Switch&) = delete;
  ~Switch() override;

  bool GetIsOn() const { return is_on_; }
  void SetIsOn(bool is_on);

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void StateChanged(ButtonState old_state) override;
  void NotifyClick(const ui::Event& event) override;
  void OnThemeChanged() override;

  void AnimationProgressed(const gfx::Animation* animation) override;

 protected:
  void UpdateAccessibleCheckedState() override;

 private:

  bool is_on_ = false;
  // 0 at off, 1 at on, and briefly past either end: the spatial curve
  // overshoots, which is the switch's small bounce.
  double position_ = 0.0;
  double from_ = 0.0;
  gfx::LinearAnimation slide_;
};

}  // namespace zephyrus::m3

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_SWITCH_H_

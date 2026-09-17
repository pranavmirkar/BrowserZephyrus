// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_m3_switch.h"

#include <algorithm>
#include <utility>

#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/highlight_path_generator.h"

namespace zephyrus::m3 {
namespace {

// SwitchTokens.
constexpr float kTrackWidth = 52.0f;
constexpr float kTrackHeight = 32.0f;
constexpr float kTrackOutlineWidth = 2.0f;
constexpr float kHandleOff = 16.0f;
constexpr float kHandleOn = 24.0f;
constexpr float kHandlePressed = 28.0f;
constexpr float kStateLayer = 40.0f;

// The thumb's centre sits this far in from the track's end in either state:
// half the track height, so a 24dp thumb has 4dp of track around it when on.
constexpr float kHandleInset = kTrackHeight / 2;

// How far the 40dp state layer reaches past the 32dp track.
constexpr int kOverhang = static_cast<int>((kStateLayer - kTrackHeight) / 2);

// A switch is flicked, not travelled. Short, on the spatial curve so the thumb
// lands with M3 Expressive's small overshoot.
constexpr base::TimeDelta kSlideDuration = base::Milliseconds(200);

double Lerp(double a, double b, double t) {
  return a + (b - a) * t;
}

}  // namespace

Switch::Switch(PressedCallback callback)
    : views::Button(std::move(callback)), slide_(this) {
  slide_.SetDuration(kSlideDuration);
  GetViewAccessibility().SetRole(ax::mojom::Role::kSwitch);
  UpdateAccessibleCheckedState();
  // The focus ring follows the TRACK and its full-round shape, not the larger
  // view that exists only to hold the state layer.
  views::InstallRoundRectHighlightPathGenerator(
      this, gfx::Insets(kOverhang), static_cast<int>(kTrackHeight / 2));
  SetInstallFocusRingOnFocus(true);
}

Switch::~Switch() = default;

void Switch::SetIsOn(bool is_on) {
  if (is_on_ == is_on) {
    return;
  }
  is_on_ = is_on;
  UpdateAccessibleCheckedState();
  slide_.Stop();
  // Nothing on screen to animate: land at the end of the travel. This is what
  // a switch built with its initial value goes through.
  if (!GetWidget() || !IsDrawn()) {
    position_ = is_on_ ? 1.0 : 0.0;
    SchedulePaint();
    return;
  }
  // From wherever the thumb is, so flipping mid-slide reverses smoothly.
  from_ = position_;
  slide_.Start();
}

gfx::Size Switch::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(static_cast<int>(kTrackWidth) + 2 * kOverhang,
                   static_cast<int>(kStateLayer));
}

void Switch::PaintButtonContents(gfx::Canvas* canvas) {
  // Every colour is a role, and a role needs a Widget.
  if (!GetWidget()) {
    return;
  }
  const gfx::PointF centre = gfx::RectF(GetLocalBounds()).CenterPoint();
  const gfx::RectF track(centre.x() - kTrackWidth / 2,
                         centre.y() - kTrackHeight / 2, kTrackWidth,
                         kTrackHeight);
  const bool enabled = GetEnabled();
  const bool pressed = enabled && GetState() == STATE_PRESSED;
  const bool hovered = enabled && GetState() == STATE_HOVERED;
  const bool focused = enabled && HasFocus();
  const bool active = pressed || hovered || focused;
  // Colours change at the MIDDLE of the travel, so a thumb in flight is always
  // one state or the other and never a blend of the two.
  const bool on_look = position_ >= 0.5;
  const SkColor on_surface = Role(*this, kColorZephyrusOnSurface);

  cc::PaintFlags flags;
  flags.setAntiAlias(true);

  // ---- Track --------------------------------------------------------------
  flags.setStyle(cc::PaintFlags::kFill_Style);
  if (!enabled) {
    flags.setColor(on_look
                       ? SkColorSetA(on_surface, 0x1F)
                       : SkColorSetA(Role(*this,
                                          kColorZephyrusSurfaceContainerHighest),
                                     0x1F));
  } else {
    flags.setColor(Role(*this, on_look ? kColorZephyrusPrimary
                                       : kColorZephyrusSurfaceContainerHighest));
  }
  canvas->DrawRoundRect(track, kTrackHeight / 2, flags);

  if (!on_look) {
    // Off carries an outline, drawn INSIDE the track so the switch does not
    // grow by the stroke when it turns off.
    gfx::RectF outline = track;
    outline.Inset(kTrackOutlineWidth / 2);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kTrackOutlineWidth);
    flags.setColor(enabled ? Role(*this, kColorZephyrusOutline)
                           : SkColorSetA(on_surface, 0x1F));
    canvas->DrawRoundRect(outline, outline.height() / 2, flags);
  }

  // ---- Thumb geometry -----------------------------------------------------
  const gfx::PointF thumb(
      static_cast<float>(Lerp(track.x() + kHandleInset,
                              track.right() - kHandleInset, position_)),
      track.CenterPoint().y());
  // The overshoot moves the thumb, but must not shrink or swell it past the
  // two sizes, so size uses the clamped position.
  const float diameter =
      pressed ? kHandlePressed
              : static_cast<float>(Lerp(kHandleOff, kHandleOn,
                                        std::clamp(position_, 0.0, 1.0)));

  // ---- State layer, under the thumb ---------------------------------------
  if (active) {
    const SkAlpha opacity = pressed   ? kPressed
                            : hovered ? kHover
                                      : kFocus;
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(StateLayer(
        on_look ? Role(*this, kColorZephyrusPrimary) : on_surface, opacity));
    canvas->DrawCircle(thumb, kStateLayer / 2, flags);
  }

  // ---- Thumb --------------------------------------------------------------
  SkColor handle;
  if (!enabled) {
    handle = on_look ? Role(*this, kColorZephyrusSurface)
                     : SkColorSetA(on_surface, 0x61);
  } else if (on_look) {
    handle = Role(*this, active ? kColorZephyrusPrimaryContainer
                                : kColorZephyrusOnPrimary);
  } else {
    handle = Role(*this, active ? kColorZephyrusOnSurfaceVariant
                                : kColorZephyrusOutline);
  }
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(handle);
  canvas->DrawCircle(thumb, diameter / 2, flags);
}

void Switch::StateChanged(ButtonState old_state) {
  views::Button::StateChanged(old_state);
  SchedulePaint();
}

void Switch::NotifyClick(const ui::Event& event) {
  // Flip first, so the callback reads the value the user just chose -- the same
  // order views::ToggleButton uses.
  SetIsOn(!is_on_);
  views::Button::NotifyClick(event);
}

void Switch::OnThemeChanged() {
  views::Button::OnThemeChanged();
  SchedulePaint();
}

void Switch::AnimationProgressed(const gfx::Animation* animation) {
  if (animation != &slide_) {
    // Button's own hover animation.
    views::Button::AnimationProgressed(animation);
    return;
  }
  position_ = Lerp(from_, is_on_ ? 1.0 : 0.0,
                   Curve(Spring::kFastSpatial)
                       .Solve(animation->GetCurrentValue()));
  SchedulePaint();
}

void Switch::UpdateAccessibleCheckedState() {
  GetViewAccessibility().SetCheckedState(is_on_
                                             ? ax::mojom::CheckedState::kTrue
                                             : ax::mojom::CheckedState::kFalse);
}

BEGIN_METADATA(Switch)
END_METADATA

}  // namespace zephyrus::m3

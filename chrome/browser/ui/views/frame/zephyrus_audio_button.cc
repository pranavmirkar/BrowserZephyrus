// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_audio_button.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/highlight_path_generator.h"

ZephyrusAudioButton::ZephyrusAudioButton(PressedCallback callback, int diameter)
    : views::Button(std::move(callback)), diameter_(diameter) {
  SetFocusBehavior(FocusBehavior::NEVER);
  SetAnimateOnStateChange(false);
  views::InstallCircleHighlightPathGenerator(this);
  SetVisible(false);
}

ZephyrusAudioButton::~ZephyrusAudioButton() = default;

void ZephyrusAudioButton::SetAudio(bool audible, bool muted) {
  audible_ = audible;
  muted_ = muted;
  const std::u16string name = muted ? u"Unmute tab" : u"Mute tab";
  GetViewAccessibility().SetName(name);
  SetTooltipText(name);
  UpdateTimer();
  SchedulePaint();
}

void ZephyrusAudioButton::SetInk(SkColor ink) {
  ink_ = ink;
  SchedulePaint();
}

gfx::Size ZephyrusAudioButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(diameter_, diameter_);
}

void ZephyrusAudioButton::VisibilityChanged(views::View* starting_from,
                                            bool is_visible) {
  views::Button::VisibilityChanged(starting_from, is_visible);
  UpdateTimer();
}

void ZephyrusAudioButton::OnPaintBackground(gfx::Canvas* canvas) {
  const bool hovered = GetState() == STATE_HOVERED;
  const bool pressed = GetState() == STATE_PRESSED;
  if (!hovered && !pressed) {
    return;
  }
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(zephyrus::m3::StateLayer(
      ink_, pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover));
  // A circle of the control's own diameter, even where a layout has stretched
  // the view taller than it is wide.
  const gfx::RectF bounds(GetLocalBounds());
  canvas->DrawCircle(bounds.CenterPoint(),
                     std::min(bounds.width(), bounds.height()) / 2.f, flags);
}

void ZephyrusAudioButton::PaintButtonContents(gfx::Canvas* canvas) {
  if (muted_) {
    const gfx::ImageSkia icon =
        ui::ImageModel::FromVectorIcon(vector_icons::kVolumeOffIcon, ink_, 14)
            .Rasterize(GetColorProvider());
    canvas->DrawImageInt(icon, (width() - icon.width()) / 2,
                         (height() - icon.height()) / 2);
    return;
  }
  // Three bars, each at its own phase of the same wave.
  constexpr float kBarWidth = 2.f;
  constexpr float kBarGap = 2.f;
  constexpr float kMaxHeight = 10.f;
  constexpr float kMinHeight = 3.f;
  const float total = 3 * kBarWidth + 2 * kBarGap;
  const float left = (width() - total) / 2.f;
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(ink_);
  for (int i = 0; i < 3; ++i) {
    const float wave =
        0.5f + 0.5f * std::sin(phase_ + static_cast<float>(i) * 2.1f);
    const float bar =
        animating() ? kMinHeight + (kMaxHeight - kMinHeight) * wave : 7.f;
    const float x = left + i * (kBarWidth + kBarGap);
    const float y = (height() - bar) / 2.f;
    canvas->DrawRoundRect(gfx::RectF(x, y, kBarWidth, bar), kBarWidth / 2.f,
                          flags);
  }
}

void ZephyrusAudioButton::UpdateTimer() {
  const bool want = audible_ && !muted_ && GetVisible() &&
                    gfx::Animation::ShouldRenderRichAnimation();
  if (want && !timer_.IsRunning()) {
    timer_.Start(FROM_HERE, base::Milliseconds(70),
                 base::BindRepeating(&ZephyrusAudioButton::Tick,
                                     base::Unretained(this)));
  } else if (!want) {
    timer_.Stop();
  }
}

void ZephyrusAudioButton::Tick() {
  phase_ += 0.9f;
  SchedulePaint();
}

BEGIN_METADATA(ZephyrusAudioButton)
END_METADATA

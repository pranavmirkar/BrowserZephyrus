// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/liquid_glass.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "base/check.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/view.h"

namespace views {

namespace {

// Background that paints the visible surface of the glass -- everything that
// sits *on top* of the blurred/refracted backdrop: a soft tint gradient, a
// specular sheen hugging the top edge, and a bright rim stroke.
class LiquidGlassBackground : public Background {
 public:
  explicit LiquidGlassBackground(const LiquidGlassParams& params)
      : params_(params) {
    SetColor(params.tint_color);
  }
  LiquidGlassBackground(const LiquidGlassBackground&) = delete;
  LiquidGlassBackground& operator=(const LiquidGlassBackground&) = delete;
  ~LiquidGlassBackground() override = default;

  // Background:
  void Paint(gfx::Canvas* canvas, View* view) const override {
    const gfx::RectF bounds(view->GetLocalBounds());
    if (bounds.IsEmpty()) {
      return;
    }
    const float radius = params_.corner_radius;

    // 1) Tint fill. A gentle vertical gradient -- a touch brighter at the top
    //    edge -- reads as light pooling on the glass surface. The gradient
    //    stays within the tint's own alpha so translucency is preserved.
    {
      const SkColor4f base = SkColor4f::FromColor(params_.tint_color);
      const float base_a = base.fA;
      SkColor4f top = base;
      top.fA = std::min(1.0f, base_a * 1.35f);
      SkColor4f bottom = base;
      bottom.fA = base_a * 0.75f;

      const SkPoint pts[2] = {
          SkPoint::Make(bounds.x(), bounds.y()),
          SkPoint::Make(bounds.x(), bounds.bottom()),
      };
      const SkColor4f colors[2] = {top, bottom};
      const SkScalar pos[2] = {0.f, 1.f};

      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setShader(cc::PaintShader::MakeLinearGradient(
          pts, colors, pos, 2, SkTileMode::kClamp));
      canvas->DrawRoundRect(bounds, radius, flags);
    }

    // 2) Specular sheen: bright white fading to transparent across the top of
    //    the glass, the highlight you get from a light source above.
    if (params_.highlight_opacity > 0.f) {
      const float a = std::clamp(params_.highlight_opacity, 0.f, 1.f);
      const SkColor4f white_a{1.f, 1.f, 1.f, a};
      const SkColor4f transparent{1.f, 1.f, 1.f, 0.f};

      const SkPoint pts[2] = {
          SkPoint::Make(bounds.x(), bounds.y()),
          SkPoint::Make(bounds.x(), bounds.bottom()),
      };
      const SkColor4f colors[3] = {white_a, transparent, transparent};
      // Fade out by ~45% of the height.
      const SkScalar pos[3] = {0.f, 0.45f, 1.f};

      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setShader(cc::PaintShader::MakeLinearGradient(
          pts, colors, pos, 3, SkTileMode::kClamp));
      canvas->DrawRoundRect(bounds, radius, flags);
    }

    // 3) Rim: a thin bright stroke tracing the rounded edge, so the glass has
    //    a defined boundary against the content behind it. Inset by half the
    //    stroke width so it stays inside the bounds.
    if (params_.rim_opacity > 0.f) {
      const float a = std::clamp(params_.rim_opacity, 0.f, 1.f);
      constexpr float kRimWidth = 1.f;
      gfx::RectF rim_bounds = bounds;
      rim_bounds.Inset(kRimWidth / 2.f);

      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(kRimWidth);
      flags.setColor(SkColorSetA(SK_ColorWHITE, static_cast<U8CPU>(a * 0xFF)));
      canvas->DrawRoundRect(rim_bounds, std::max(0.f, radius - kRimWidth / 2.f),
                            flags);
    }
  }

  std::optional<gfx::RoundedCornersF> GetRoundedCornerRadii() const override {
    return gfx::RoundedCornersF(params_.corner_radius);
  }

 private:
  const LiquidGlassParams params_;
};

// Interaction motion tuning (see the Liquid Glass animation rationale). All
// values are transform/opacity only, so they run on the compositor.
constexpr float kHoverScale = 1.02f;
constexpr float kPressScale = 0.97f;
constexpr base::TimeDelta kHoverDuration = base::Milliseconds(150);
constexpr base::TimeDelta kPressDuration = base::Milliseconds(120);
constexpr base::TimeDelta kReleaseDuration = base::Milliseconds(220);
constexpr base::TimeDelta kEnterDuration = base::Milliseconds(260);

// A transform that scales the view about its center (identity when scale == 1).
gfx::Transform ScaleAboutCenter(const View* view, float scale) {
  if (scale == 1.f) {
    return gfx::Transform();
  }
  return gfx::GetScaleTransform(gfx::Rect(view->size()).CenterPoint(), scale);
}

// Animates the view's layer to `scale` about its center. Falls back to an
// instant set when the OS requests reduced motion.
void AnimateLayerScale(View* view,
                       float scale,
                       base::TimeDelta duration,
                       gfx::Tween::Type tween) {
  ui::Layer* layer = view->layer();
  if (!layer) {
    return;
  }
  const gfx::Transform target = ScaleAboutCenter(view, scale);
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    layer->SetTransform(target);
    return;
  }
  AnimationBuilder().Once().SetDuration(duration).SetTransform(layer, target,
                                                              tween);
}

}  // namespace

// static
LiquidGlassParams LiquidGlassParams::Light() {
  LiquidGlassParams p;
  p.tint_color = SkColorSetARGB(0x24, 0xFF, 0xFF, 0xFF);
  p.highlight_opacity = 0.55f;
  p.rim_opacity = 0.35f;
  return p;
}

// static
LiquidGlassParams LiquidGlassParams::Dark() {
  LiquidGlassParams p;
  // A darker, cooler tint with a slightly stronger blur suits dark chrome.
  p.blur_sigma = 22.f;
  p.tint_color = SkColorSetARGB(0x40, 0x1A, 0x1D, 0x24);
  p.highlight_opacity = 0.30f;
  p.rim_opacity = 0.22f;
  return p;
}

void ApplyLiquidGlass(View* view, const LiquidGlassParams& params) {
  DCHECK(view);

  view->SetPaintToLayer();
  ui::Layer* layer = view->layer();
  DCHECK(layer);

  // Let the backdrop show through, and clip the glass to rounded corners.
  layer->SetFillsBoundsOpaquely(false);
  layer->SetRoundedCornerRadius(gfx::RoundedCornersF(params.corner_radius));
  // Fast rounded corners avoid an extra render pass for the (common) case of a
  // single uniform-radius rect.
  layer->SetIsFastRoundedCorner(true);

  // Frosted backdrop.
  layer->SetBackgroundBlur(params.blur_sigma);

  // Edge refraction (lens). No-op on software compositing; see header.
  if (params.refraction_zoom > 1.f) {
    layer->SetBackgroundZoom(params.refraction_zoom, params.refraction_inset);
  } else {
    layer->SetBackgroundZoom(1.f, 0);
  }

  // Surface treatment (tint + sheen + rim).
  view->SetBackground(std::make_unique<LiquidGlassBackground>(params));
}

void RemoveLiquidGlass(View* view) {
  DCHECK(view);
  view->SetBackground(nullptr);
  if (ui::Layer* layer = view->layer()) {
    layer->SetBackgroundBlur(0.f);
    layer->SetBackgroundZoom(1.f, 0);
  }
}

void AnimateLiquidGlassIn(View* view, base::TimeDelta delay) {
  DCHECK(view);
  ui::Layer* layer = view->layer();
  if (!layer) {
    return;
  }

  const bool rich = gfx::Animation::ShouldRenderRichAnimation();

  // Establish the starting state up front so there is no first-frame flash at
  // full opacity before the animation begins.
  layer->SetOpacity(0.f);
  if (rich) {
    layer->SetTransform(ScaleAboutCenter(view, 0.96f));
  }

  if (rich) {
    AnimationBuilder()
        .Once()
        .At(delay)
        .SetDuration(kEnterDuration)
        .SetOpacity(layer, 1.f, gfx::Tween::LINEAR_OUT_SLOW_IN)
        .SetTransform(layer, gfx::Transform(), gfx::Tween::LINEAR_OUT_SLOW_IN);
  } else {
    // Reduced motion: fade only, no movement.
    AnimationBuilder()
        .Once()
        .At(delay)
        .SetDuration(base::Milliseconds(160))
        .SetOpacity(layer, 1.f, gfx::Tween::LINEAR);
  }
}

LiquidGlassView::LiquidGlassView(LiquidGlassParams params) : params_(params) {
  ApplyLiquidGlass(this, params_);
}

LiquidGlassView::~LiquidGlassView() = default;

void LiquidGlassView::SetGlassParams(const LiquidGlassParams& params) {
  params_ = params;
  ApplyLiquidGlass(this, params_);
}

void LiquidGlassView::SetPressedCallback(base::RepeatingClosure callback) {
  pressed_callback_ = std::move(callback);
}

float LiquidGlassView::TargetScale() const {
  if (pressed_) {
    return kPressScale;
  }
  return hovered_ ? kHoverScale : 1.f;
}

void LiquidGlassView::OnMouseEntered(const ui::MouseEvent& event) {
  hovered_ = true;
  if (!pressed_) {
    AnimateLayerScale(this, TargetScale(), kHoverDuration, gfx::Tween::EASE_OUT);
  }
}

void LiquidGlassView::OnMouseExited(const ui::MouseEvent& event) {
  hovered_ = false;
  if (!pressed_) {
    AnimateLayerScale(this, TargetScale(), kHoverDuration, gfx::Tween::EASE_OUT);
  }
}

bool LiquidGlassView::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton()) {
    return false;
  }
  pressed_ = true;
  AnimateLayerScale(this, kPressScale, kPressDuration, gfx::Tween::EASE_OUT);
  // Consume so we receive the matching release.
  return true;
}

void LiquidGlassView::OnMouseReleased(const ui::MouseEvent& event) {
  const bool was_pressed = pressed_;
  pressed_ = false;
  // Settle back to the rest/hover scale with a gentle rebound -- the liquid
  // "gel" easing back into place.
  AnimateLayerScale(this, TargetScale(), kReleaseDuration,
                    gfx::Tween::FAST_OUT_SLOW_IN_3);
  if (was_pressed && pressed_callback_ &&
      GetLocalBounds().Contains(event.location())) {
    pressed_callback_.Run();
  }
}

BEGIN_METADATA(LiquidGlassView)
END_METADATA

}  // namespace views

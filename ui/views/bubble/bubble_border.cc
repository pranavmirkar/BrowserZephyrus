// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#include "ui/views/bubble/bubble_border.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/no_destructor.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "third_party/skia/include/core/SkClipOp.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkScalar.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rrect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/bubble/bubble_border_arrow_utils.h"
#include "ui/views/metadata/type_conversion.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/shadow_controller.h"

namespace views {

namespace {

// GetShadowValues and GetBorderAndShadowFlags cache their results. The shadow
// values depend on the shadow elevation, color and shadow type, so we create a
// tuple to key the cache.
using ShadowCacheKey = std::tuple<int, SkColor, BubbleBorder::Shadow>;

// The shadow type for default shadow colors.
constexpr int kDefaultShadowType = -1;

ui::Shadow::ElevationToColorsMap ShadowElevationToColorsMap(
    BubbleBorder::Shadow shadow,
    const ui::ColorProvider* color_provider) {
  ui::Shadow::ElevationToColorsMap colors_map;
  if (color_provider) {
    switch (shadow) {
      case BubbleBorder::Shadow::STANDARD_SHADOW:
        colors_map[3] = std::make_pair(
            color_provider->GetColor(
                ui::kColorShadowValueKeyShadowElevationThree),
            color_provider->GetColor(
                ui::kColorShadowValueAmbientShadowElevationThree));
        colors_map[16] = std::make_pair(
            color_provider->GetColor(
                ui::kColorShadowValueKeyShadowElevationSixteen),
            color_provider->GetColor(
                ui::kColorShadowValueAmbientShadowElevationSixteen));
        break;
#if BUILDFLAG(IS_CHROMEOS)
      case BubbleBorder::Shadow::CHROMEOS_SYSTEM_UI_SHADOW:
        colors_map =
            wm::ShadowController::GenerateShadowColorsMap(color_provider);
        break;
#endif
      default:
        NOTREACHED() << "Invalid bubble border shadow type.";
    }
  }

  const SkColor default_color =
      color_provider ? color_provider->GetColor(ui::kColorShadowBase)
                     : gfx::kPlaceholderColor;
  colors_map[kDefaultShadowType] = std::make_pair(default_color, default_color);
  return colors_map;
}

// ZEPHYRUS: the bubble outline as ONE path, with the nub blended into the top
// edge instead of stamped on as a separate triangle.
//
// Chromium draws the rounded rect, then paints an arrow over it. That leaves a
// hard corner where the arrow's base meets the edge, and no amount of rounding
// the arrow's tip hides it -- the join is the problem, not the point.
//
// The design's nub instead flows out of the edge: the outline leaves the top
// edge horizontally, flares up, rounds over at the apex, and comes back down
// the same way. Each flank is one cubic whose end tangents are BOTH horizontal,
// which is what produces the concave flare at the base and the soft point at
// the top from a single curve.
//
// Top edge only. Every popup that gets a nub hangs below a title-bar control,
// so the arrow is always on top; the other three sides keep the stock two-path
// drawing rather than have geometry here that nothing exercises.
SkPath BuildOutlineWithTopNub(const SkRRect& r_rect,
                              float nub_center_x,
                              float nub_half_width,
                              float nub_height) {
  const SkRect b = r_rect.rect();
  const float radius = r_rect.radii(SkRRect::kUpperLeft_Corner).x();

  // Keep the nub clear of the corner curves, or it grows out of the arc and
  // reads as a dent rather than a point.
  const float min_x = b.left() + radius + nub_half_width;
  const float max_x = b.right() - radius - nub_half_width;
  const float cx = std::clamp(nub_center_x, min_x, max_x);

  // How far along each flank the curve stays flat before rising. Higher values
  // widen the concave flare where the nub meets the edge.
  constexpr float kFlare = 0.45f;
  // How far the apex control points sit from the centre. Lower values sharpen
  // the point, higher ones flatten it.
  constexpr float kTip = 0.30f;

  const float top = b.top();
  const float apex = top - nub_height;

  SkPathBuilder p;
  p.moveTo(b.left() + radius, top);
  p.lineTo(cx - nub_half_width, top);
  p.cubicTo(cx - nub_half_width * kFlare, top, cx - nub_half_width * kTip, apex,
            cx, apex);
  p.cubicTo(cx + nub_half_width * kTip, apex, cx + nub_half_width * kFlare, top,
            cx + nub_half_width, top);
  p.lineTo(b.right() - radius, top);
  p.arcTo(SkRect::MakeLTRB(b.right() - 2 * radius, top, b.right(),
                           top + 2 * radius),
          -90, 90, false);
  p.lineTo(b.right(), b.bottom() - radius);
  p.arcTo(SkRect::MakeLTRB(b.right() - 2 * radius, b.bottom() - 2 * radius,
                           b.right(), b.bottom()),
          0, 90, false);
  p.lineTo(b.left() + radius, b.bottom());
  p.arcTo(SkRect::MakeLTRB(b.left(), b.bottom() - 2 * radius,
                           b.left() + 2 * radius, b.bottom()),
          90, 90, false);
  p.lineTo(b.left(), top + radius);
  p.arcTo(SkRect::MakeLTRB(b.left(), top, b.left() + 2 * radius,
                           top + 2 * radius),
          180, 90, false);
  p.close();
  return p.detach();
}

enum class BubbleArrowPart { kFill, kBorder };

SkPath GetVisibleArrowPath(BubbleBorder::Arrow arrow,
                           const gfx::Rect& bounds,
                           BubbleArrowPart part) {
  constexpr size_t kNumPoints = 4;
  gfx::RectF bounds_f(bounds);
  SkPoint points[kNumPoints];
  switch (GetBubbleArrowSide(arrow)) {
    case BubbleArrowSide::kRight:
      points[0] = {bounds_f.x(), bounds_f.y()};
      points[1] = {bounds_f.right(),
                   bounds_f.y() + BubbleBorder::kVisibleArrowRadius - 1};
      points[2] = {bounds_f.right(),
                   bounds_f.y() + BubbleBorder::kVisibleArrowRadius};
      points[3] = {bounds_f.x(), bounds_f.bottom() - 1};
      break;
    case BubbleArrowSide::kLeft:
      points[0] = {bounds_f.right(), bounds_f.bottom() - 1};
      points[1] = {bounds_f.x(),
                   bounds_f.y() + BubbleBorder::kVisibleArrowRadius};
      points[2] = {bounds_f.x(),
                   bounds_f.y() + BubbleBorder::kVisibleArrowRadius - 1};
      points[3] = {bounds_f.right(), bounds_f.y()};
      break;
    case BubbleArrowSide::kTop:
      points[0] = {bounds_f.x(), bounds_f.bottom()};
      points[1] = {bounds_f.x() + BubbleBorder::kVisibleArrowRadius - 1,
                   bounds_f.y()};
      points[2] = {bounds_f.x() + BubbleBorder::kVisibleArrowRadius,
                   bounds_f.y()};
      points[3] = {bounds_f.right() - 1, bounds_f.bottom()};
      break;
    case BubbleArrowSide::kBottom:
      points[0] = {bounds_f.right() - 1, bounds_f.y()};
      points[1] = {bounds_f.x() + BubbleBorder::kVisibleArrowRadius,
                   bounds_f.bottom()};
      points[2] = {bounds_f.x() + BubbleBorder::kVisibleArrowRadius - 1,
                   bounds_f.bottom()};
      points[3] = {bounds_f.x(), bounds_f.y()};
      break;
  }

  // ZEPHYRUS: round the tip instead of returning a bare polygon.
  //
  // points[1] and points[2] are the tip, one pixel apart, so the stock shape is
  // a spike with a flat top. The design's nub is a softened point, and at this
  // size the difference is the whole character of it: a sharp tip reads as a
  // warning marker, a rounded one as a tab.
  //
  // Done here rather than in a Zephyrus subclass because every bubble arrow in
  // the browser should match, and this is the one function that builds them.
  //
  // The rounding is applied to whichever pair of points forms the tip, so it
  // works for all four sides without knowing which one this is.
  constexpr float kTipRound = 3.f;

  const SkPoint tip{(points[1].x() + points[2].x()) / 2.f,
                    (points[1].y() + points[2].y()) / 2.f};

  // Pull back from the tip along each flank by kTipRound, clamped so a short
  // flank cannot invert the curve.
  auto pull_back = [&tip](const SkPoint& from) {
    const float dx = from.x() - tip.x();
    const float dy = from.y() - tip.y();
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= kTipRound || len == 0.f) {
      return from;
    }
    const float t = kTipRound / len;
    return SkPoint{tip.x() + dx * t, tip.y() + dy * t};
  };

  const SkPoint before = pull_back(points[0]);
  const SkPoint after = pull_back(points[3]);

  SkPathBuilder builder;
  builder.moveTo(points[0]);
  builder.lineTo(before);
  builder.quadTo(tip, after);
  builder.lineTo(points[3]);
  if (part == BubbleArrowPart::kFill) {
    builder.close();
  }
  return builder.detach();
}

const gfx::ShadowValues& GetShadowValues(
    const ui::ColorProvider* color_provider,
    const std::optional<int>& elevation,
    BubbleBorder::Shadow shadow_type) {
  // If the color provider does not exist the shadow values are being created in
  // order to calculate Insets. In that case the color plays no role so always
  // set those colors to gfx::kPlaceholderColor.

  SkColor color = color_provider
                      ? color_provider->GetColor(ui::kColorShadowBase)
                      : gfx::kPlaceholderColor;

  // The shadows are always the same for any elevation and color combination, so
  // construct them once and cache.
  static base::NoDestructor<std::map<ShadowCacheKey, gfx::ShadowValues>>
      shadow_map;
  ShadowCacheKey key(elevation.value_or(-1), color, shadow_type);

  if (shadow_map->find(key) != shadow_map->end()) {
    return shadow_map->find(key)->second;
  }

  gfx::ShadowValues shadows;
  if (elevation.has_value()) {
    DCHECK_GE(elevation.value(), 0);
    auto shadow_colors_map =
        ShadowElevationToColorsMap(shadow_type, color_provider);
    const auto iter = shadow_colors_map.find(elevation.value());
    const auto key_ambient_colors = (iter != shadow_colors_map.end())
                                        ? iter->second
                                        : shadow_colors_map[kDefaultShadowType];
    switch (shadow_type) {
      case BubbleBorder::Shadow::STANDARD_SHADOW:
        shadows = gfx::ShadowValue::MakeShadowValues(elevation.value(),
                                                     key_ambient_colors.first,
                                                     key_ambient_colors.second);
        break;
#if BUILDFLAG(IS_CHROMEOS)
      case BubbleBorder::CHROMEOS_SYSTEM_UI_SHADOW:
        if (key_ambient_colors.first == key_ambient_colors.second) {
          shadows = gfx::ShadowValue::MakeChromeOSSystemUIShadowValues(
              elevation.value(), key_ambient_colors.first);
        } else {
          shadows = gfx::ShadowValue::MakeChromeOSSystemUIShadowValues(
              elevation.value(), key_ambient_colors.first,
              key_ambient_colors.second);
        }
        break;
#endif
      default:
        NOTREACHED() << "Invalid bubble border shadow type";
    }
  } else {
    constexpr gfx::Vector2d kOffset(0, 2);
    constexpr int kSmallShadowBlur = 4;
    const SkColor small_shadow_color =
        color_provider
            ? color_provider->GetColor(ui::kColorBubbleBorderShadowSmall)
            : gfx::kPlaceholderColor;
    const SkColor large_shadow_color =
        color_provider
            ? color_provider->GetColor(ui::kColorBubbleBorderShadowLarge)
            : gfx::kPlaceholderColor;
    // gfx::ShadowValue counts blur pixels both inside and outside the shape,
    // whereas these blur values only describe the outside portion, hence they
    // must be doubled.
    shadows = gfx::ShadowValues({
        {kOffset, 2 * kSmallShadowBlur, small_shadow_color},
        {kOffset, 2 * BubbleBorder::kShadowBlur, large_shadow_color},
    });
  }

  shadow_map->insert({key, shadows});
  return shadow_map->find(key)->second;
}

bool ShouldDrawStrokeForArgs(const std::optional<bool>& draw_border_stroke,
                             const std::optional<int>& elevation,
                             BubbleBorder::Shadow shadow_type) {
  return draw_border_stroke.value_or(!elevation.has_value() &&
                                     shadow_type != BubbleBorder::NO_SHADOW);
}

const cc::PaintFlags& GetBorderAndShadowFlags(
    const ui::ColorProvider* color_provider,
    const std::optional<int>& elevation,
    BubbleBorder::Shadow shadow_type) {
  // The flags are always the same for any elevation and color combination, so
  // construct them once and cache.
  static base::NoDestructor<std::map<ShadowCacheKey, cc::PaintFlags>> flag_map;
  ShadowCacheKey key(elevation.value_or(-1),
                     color_provider->GetColor(ui::kColorShadowBase),
                     shadow_type);

  if (flag_map->find(key) != flag_map->end()) {
    return flag_map->find(key)->second;
  }

  cc::PaintFlags flags;
  flags.setColor(color_provider->GetColor(ui::kColorBubbleBorder));
  flags.setAntiAlias(true);
  flags.setLooper(gfx::CreateShadowDrawLooper(
      GetShadowValues(color_provider, elevation, shadow_type)));
  flag_map->insert({key, flags});
  return flag_map->find(key)->second;
}

template <typename T>
void DrawBorderAndShadowImpl(
    T rect,
    void (cc::PaintCanvas::*draw)(const T&, const cc::PaintFlags&),
    gfx::Canvas* canvas,
    const ui::ColorProvider* color_provider,
    bool draw_stroke = true,
    const std::optional<int>& elevation = std::nullopt,
    BubbleBorder::Shadow shadow_type = BubbleBorder::STANDARD_SHADOW) {
  if (draw_stroke) {
    // Provide a 1 px border outside the bounds.
    constexpr int kBorderStrokeThicknessPx = 1;
    const SkScalar one_pixel =
        SkFloatToScalar(kBorderStrokeThicknessPx / canvas->image_scale());
    rect.outset(one_pixel, one_pixel);
  }

  (canvas->sk_canvas()->*draw)(
      rect, GetBorderAndShadowFlags(color_provider, elevation, shadow_type));
}

// Shadow is set to NO_SHADOW if compositing is not supported. This checks for
// NO_SHADOW in cases where it would have to be explicitly set.
bool IsExplicitNoShadow(BubbleBorder::Shadow shadow) {
  if (shadow == BubbleBorder::NO_SHADOW) {
    return Widget::IsWindowCompositingSupported();
  }
  return false;
}

}  // namespace

BubbleBorder::BubbleBorder(Arrow arrow, Shadow shadow)
    : arrow_(arrow), shadow_(shadow) {
  DCHECK_LT(shadow_, SHADOW_COUNT);
  SetColor(ui::kColorDialogBackground);
}

BubbleBorder::~BubbleBorder() = default;

// static
gfx::Insets BubbleBorder::GetBorderAndShadowInsets(
    const std::optional<int>& elevation,
    const std::optional<bool>& draw_border_stroke,
    BubbleBorder::Shadow shadow_type) {
  return gfx::Insets(
             ShouldDrawStrokeForArgs(draw_border_stroke, elevation, shadow_type)
                 ? kBorderThicknessDip
                 : 0) -
         gfx::ShadowValue::GetMargin(
             GetShadowValues(nullptr, elevation, shadow_type));
}

gfx::Rect BubbleBorder::GetBounds(const gfx::Rect& anchor_rect,
                                  const gfx::Size& contents_size) const {
  const gfx::Size size(GetSizeForContentsSize(contents_size));
  // In floating mode, the bounds of the bubble border and the |anchor_rect|
  // have coinciding central points.
  if (arrow_ == FLOAT) {
    gfx::Rect rect(anchor_rect.CenterPoint(), size);
    rect.Offset(gfx::Vector2d(-size.width() / 2, -size.height() / 2));
    return rect;
  }

  // If no arrow is used, in the vertical direction, the bubble is placed below
  // the |anchor_rect| while they have coinciding horizontal centers.
  if (arrow_ == NONE) {
    gfx::Rect rect(anchor_rect.bottom_center(), size);
    rect.Offset(gfx::Vector2d(-size.width() / 2, 0));
    return rect;
  }

  // In all other cases, the used arrow determines the placement of the bubble
  // with respect to the |anchor_rect|.
  gfx::Rect contents_bounds(contents_size);
  // Always apply the border part of the inset before calculating coordinates,
  // that ensures the bubble's border is aligned with the anchor's border.
  // For the purposes of positioning, the border is rounded up to a dip, which
  // may cause misalignment in scale factors greater than 1.
  // TODO(estade): when it becomes possible to provide px bounds instead of
  // dip bounds, fix this.
  const gfx::Insets border_insets(ShouldDrawStroke() ? kBorderThicknessDip : 0);
  const gfx::Insets insets = GetInsets();
  const gfx::Insets shadow_insets = insets - border_insets;
  // TODO(dfried): Collapse border into visible arrow where applicable.
  contents_bounds.Inset(-border_insets);
  DCHECK(!avoid_shadow_overlap_ || !visible_arrow_);

  // If |avoid_shadow_overlap_| is true, the shadow part of the inset is also
  // applied now, to ensure that the shadow itself doesn't overlap the anchor.
  if (avoid_shadow_overlap_) {
    contents_bounds.Inset(-shadow_insets);
  }

  // Adjust the contents to align with the arrow. The `anchor_point` is the
  // point on `anchor_rect` to offset from; it is also used as part of the
  // visible arrow calculation if present.
  gfx::Point anchor_point =
      GetArrowAnchorPointFromAnchorRect(arrow_, anchor_rect);

  contents_bounds += GetContentBoundsOffsetToArrowAnchorPoint(
      contents_bounds, arrow_, anchor_point);

  // With NO_SHADOW, there should be further insets, but the same logic is
  // used to position the bubble origin according to |anchor_rect|.
  DCHECK(!IsExplicitNoShadow(shadow_) || insets_.has_value() ||
         shadow_insets.IsEmpty() || visible_arrow_);
  if (!avoid_shadow_overlap_) {
    contents_bounds.Inset(-shadow_insets);
  }

  // |arrow_offset_| is used to adjust bubbles that would normally be
  // partially offscreen.
  if (is_arrow_on_horizontal(arrow_)) {
    contents_bounds += gfx::Vector2d(-arrow_offset_, 0);
  } else {
    contents_bounds += gfx::Vector2d(0, -arrow_offset_);
  }

  // If no visible arrow is shown, return the content bounds.
  if (!visible_arrow_) {
    return contents_bounds;
  }

  // Finally, get the needed movement vector of |contents_bounds| to create the
  // space needed to place the visible arrow. adjustments because we don't want
  // the positioning to be altered. Offset by the size of the arrow's inset on
  // each side (only one side will be nonzero) to create space for the visible
  // arrow.
  contents_bounds +=
      GetContentsBoundsOffsetToPlaceVisibleArrow(arrow_, /*include_gap=*/true);

  // We have an anchor point which is appropriate for the arrow type, but
  // when anchoring to a small view it looks better to track from the middle
  // of the view rather than a corner. We may still adjust this point if
  // it's too close to the edge of the bubble (in this case by adjusting the
  // bubble by a few pixels rather than the anchor point).
  const gfx::Point anchor_center = anchor_rect.CenterPoint();
  const gfx::Point contents_center = contents_bounds.CenterPoint();
  if (IsVerticalArrow(arrow_)) {
    const int right_bound =
        contents_bounds.right() -
        (kVisibleArrowBuffer + kVisibleArrowRadius + shadow_insets.right());
    const int left_bound = contents_bounds.x() + kVisibleArrowBuffer +
                           kVisibleArrowRadius + shadow_insets.left();
    if (anchor_point.x() > anchor_center.x() &&
        anchor_center.x() > contents_center.x()) {
      anchor_point.set_x(anchor_center.x());
    } else if (anchor_point.x() > right_bound) {
      anchor_point.set_x(std::max(anchor_rect.x(), right_bound));
    } else if (anchor_point.x() < anchor_center.x() &&
               anchor_center.x() < contents_center.x()) {
      anchor_point.set_x(anchor_center.x());
    } else if (anchor_point.x() < left_bound) {
      anchor_point.set_x(std::min(anchor_rect.right(), left_bound));
    }
    if (anchor_point.x() < left_bound) {
      contents_bounds -= gfx::Vector2d(left_bound - anchor_point.x(), 0);
    } else if (anchor_point.x() > right_bound) {
      contents_bounds += gfx::Vector2d(anchor_point.x() - right_bound, 0);
    }
  } else {
    const int bottom_bound =
        contents_bounds.bottom() -
        (kVisibleArrowBuffer + kVisibleArrowRadius + shadow_insets.bottom());
    const int top_bound = contents_bounds.y() + kVisibleArrowBuffer +
                          kVisibleArrowRadius + shadow_insets.top();
    if (anchor_point.y() > anchor_center.y() &&
        anchor_center.y() > contents_center.y()) {
      anchor_point.set_y(anchor_center.y());
    } else if (anchor_point.y() > bottom_bound) {
      anchor_point.set_y(std::max(anchor_rect.y(), bottom_bound));
    } else if (anchor_point.y() < anchor_center.y() &&
               anchor_center.y() < contents_center.y()) {
      anchor_point.set_y(anchor_center.y());
    } else if (anchor_point.y() < top_bound) {
      anchor_point.set_y(std::min(anchor_rect.bottom(), top_bound));
    }
    if (anchor_point.y() < top_bound) {
      contents_bounds -= gfx::Vector2d(0, top_bound - anchor_point.y());
    } else if (anchor_point.y() > bottom_bound) {
      contents_bounds += gfx::Vector2d(0, anchor_point.y() - bottom_bound);
    }
  }

  CalculateVisibleArrowRect(contents_bounds, anchor_point);

  return contents_bounds;
}

// static
gfx::Vector2d BubbleBorder::GetContentsBoundsOffsetToPlaceVisibleArrow(
    BubbleBorder::Arrow arrow,
    bool include_gap) {
  DCHECK(has_arrow(arrow));

  const gfx::Insets visible_arrow_insets =
      GetVisibleArrowInsets(arrow, include_gap);
  return gfx::Vector2d(
      visible_arrow_insets.left() - visible_arrow_insets.right(),
      visible_arrow_insets.top() - visible_arrow_insets.bottom());
}

void BubbleBorder::Paint(const views::View& view, gfx::Canvas* canvas) {
  if (shadow_ == NO_SHADOW) {
    PaintNoShadow(view, canvas);
    return;
  }

  gfx::ScopedCanvas scoped(canvas);
  SkRRect r_rect = GetClientRect(view);

  // ZEPHYRUS: with a nub on the TOP edge, draw body and nub as one path so the
  // two blend instead of meeting at a corner. See BuildOutlineWithTopNub().
  //
  // The shadow comes along for free because it is drawn from the same path,
  // which the stamped-arrow version could never manage: it shadowed a rectangle
  // and then laid a flat triangle on top.
  if (const std::optional<SkPath> outline =
          GetZephyrusNubOutline(view, r_rect)) {
    canvas->sk_canvas()->clipPath(*outline, SkClipOp::kDifference,
                                  true /*doAntiAlias*/);
    canvas->sk_canvas()->drawPath(
        *outline, GetBorderAndShadowFlags(view.GetColorProvider(),
                                          md_shadow_elevation_, shadow_));
    return;
  }

  canvas->sk_canvas()->clipRRect(r_rect, SkClipOp::kDifference,
                                 true /*doAntiAlias*/);
  DrawBorderAndShadowImpl(r_rect, &cc::PaintCanvas::drawRRect, canvas,
                          view.GetColorProvider(), ShouldDrawStroke(),
                          md_shadow_elevation_, shadow_);

  if (visible_arrow_) {
    PaintVisibleArrow(view, canvas);
  }
}

// static
void BubbleBorder::DrawBorderAndShadow(
    SkRect rect,
    gfx::Canvas* canvas,
    const ui::ColorProvider* color_provider) {
  DrawBorderAndShadowImpl(rect, &cc::PaintCanvas::drawRect, canvas,
                          color_provider);
}

gfx::Insets BubbleBorder::GetInsets() const {
  // Visible arrow is not compatible with OS-drawn shadow or with manual insets.
  DCHECK((!insets_ && !IsExplicitNoShadow(shadow_)) || !visible_arrow_);
  if (insets_.has_value()) {
    return insets_.value();
  }
  gfx::Insets insets;

  switch (shadow_) {
    case STANDARD_SHADOW:
#if BUILDFLAG(IS_CHROMEOS)
    case CHROMEOS_SYSTEM_UI_SHADOW:
#endif
      insets = GetBorderAndShadowInsets(md_shadow_elevation_,
                                        draw_border_stroke_, shadow_);
      break;
    default:
      break;
  }

  // ZEPHYRUS: an explicit nub needs the same top margin the arrow would get,
  // or it is drawn outside the view and clipped away.
  if (zephyrus_nub_screen_x_.has_value()) {
    insets = gfx::Insets::TLBR(std::max(insets.top(), kVisibleArrowLength),
                               insets.left(), insets.bottom(), insets.right());
  }

  if (visible_arrow_) {
    const gfx::Insets arrow_insets = GetVisibleArrowInsets(arrow_, false);
    insets = gfx::Insets::TLBR(std::max(insets.top(), arrow_insets.top()),
                               std::max(insets.left(), arrow_insets.left()),
                               std::max(insets.bottom(), arrow_insets.bottom()),
                               std::max(insets.right(), arrow_insets.right()));
  }
  return insets;
}

gfx::Size BubbleBorder::GetMinimumSize() const {
  return GetSizeForContentsSize(gfx::Size());
}

void BubbleBorder::OnViewThemeChanged(View* view) {
  view->SchedulePaint();
}

gfx::Size BubbleBorder::GetSizeForContentsSize(
    const gfx::Size& contents_size) const {
  // Enlarge the contents size by the thickness of the border images.
  gfx::Size size(contents_size);
  const gfx::Insets insets = GetInsets();
  size.Enlarge(insets.width(), insets.height());
  return size;
}

bool BubbleBorder::AddArrowToBubbleCornerAndPointTowardsAnchor(
    const gfx::Rect& anchor_rect,
    gfx::Rect& popup_bounds,
    int popup_min_y) {
  // This function should only be called for a visible arrow.
  DCHECK(arrow_ != Arrow::NONE && arrow_ != Arrow::FLOAT);
  CHECK_GE(popup_bounds.y(), popup_min_y);

  // The total size of the arrow in its normal direction.
  const int kVisibleArrowDiamater = 2 * kVisibleArrowRadius;

  // To store the resulting x and y position of the arrow.
  int x_position, y_position;

  // The definition of an vertical arrow is inconsistent.
  if (IsVerticalArrow(arrow_)) {
    // The optimal x position depends on the |arrow_|.
    // For a center-aligned arrow, the optimal position of the arrow points
    // towards the center of the element. If the arrow is right-aligned, it
    // points towards the right edge of the element, and to the left otherwise.
    int x_optimal_position =
        (int{arrow_} & ArrowMask::CENTER)
            ? anchor_rect.CenterPoint().x() - kVisibleArrowRadius
            : ((int{arrow_} & ArrowMask::RIGHT)
                   ? anchor_rect.right() - kVisibleArrowDiamater
                   : anchor_rect.x());

    // The most left position for the arrow is the left edge of the bubble
    // plus the minimum spacing of the arrow from the edge.
    int leftmost_position_on_bubble = popup_bounds.x() + kVisibleArrowBuffer;

    // Analogous, the most right position is the right side minus the diameter
    // of the arrow and the spacing of the arrow from the edge.
    int rightmost_position_on_bubble =
        popup_bounds.right() - kVisibleArrowDiamater - kVisibleArrowBuffer;

    // If the right-most position is smaller than the left-most position, the
    // bubble's width is not sufficient to add an arrow.
    if (leftmost_position_on_bubble > rightmost_position_on_bubble) {
      // Make the arrow invisible because there is not enough space to show it.
      set_visible_arrow(false);
      return false;
    }

    // Make sure the x position is limited to the range defined by the bubble.
    x_position = std::clamp(x_optimal_position, leftmost_position_on_bubble,
                            rightmost_position_on_bubble);

    // Calculate the y position of the arrow to be either on top of below the
    // bubble.
    y_position = (int{arrow_} & ArrowMask::BOTTOM)
                     ? popup_bounds.bottom()
                     : popup_bounds.y() - kVisibleArrowLength;
  } else {
    // Adjust y position of the popup to keep the arrow pointing exactly in
    // the middle of the anchor element, still respecting
    // the |kVisibleArrowBuffer| restrictions.
    int popup_y_upper_bound = anchor_rect.CenterPoint().y() -
                              (kVisibleArrowRadius + kVisibleArrowBuffer);
    int popup_y_lower_bound = anchor_rect.CenterPoint().y() +
                              (kVisibleArrowRadius + kVisibleArrowBuffer) -
                              popup_bounds.height();

    // The popup height is not enough to accommodate the arrow.
    if (popup_y_upper_bound < popup_y_lower_bound) {
      set_visible_arrow(false);
      return false;
    }

    int popup_y_adjusted =
        std::clamp(popup_bounds.y(), popup_y_lower_bound, popup_y_upper_bound);
    popup_bounds.set_y(popup_y_adjusted);

    // For an horizontal arrow, the x position is either the left or the right
    // edge of the bubble, taking the length of the arrow into account.
    x_position = (int{arrow_} & ArrowMask::RIGHT)
                     ? popup_bounds.right()
                     : popup_bounds.x() - kVisibleArrowLength;

    // Calculate the top- and bottom-most position for the bubble.
    int topmost_position_on_bubble = popup_bounds.y() + kVisibleArrowBuffer;

    int bottommost_position_on_bubble =
        popup_bounds.bottom() - kVisibleArrowDiamater - kVisibleArrowBuffer;

    // If the top-most position is below the bottom-most position, the bubble
    // has not enough height to place an arrow.
    if (topmost_position_on_bubble > bottommost_position_on_bubble) {
      // Make the arrow invisible because there is not enough space to show it.
      set_visible_arrow(false);
      return false;
    }

    // Align the arrow with the horizontal center of the element.
    // Here, there is no differentiation between the different positions of a
    // left or right aligned arrow.
    y_position =
        std::clamp(anchor_rect.CenterPoint().y() - kVisibleArrowRadius,
                   topmost_position_on_bubble, bottommost_position_on_bubble);
  }

  visible_arrow_rect_.set_size(GetVisibleArrowSize(arrow_));
  visible_arrow_rect_.set_origin({x_position, y_position});

  // The arrow is positioned around the popup, but the popup is still in its
  // original position and the arrow may overlap the anchor element. To make
  // the whole tandem visually pointing to the anchor it must be shifted
  // in the opposite direction.
  gfx::Vector2d popup_offset =
      GetContentsBoundsOffsetToPlaceVisibleArrow(arrow_, false);
  popup_bounds.set_origin(popup_bounds.origin() + popup_offset);
  visible_arrow_rect_.set_origin(visible_arrow_rect_.origin() + popup_offset);

  // Adjust positions if the shifted popup violates the min y restrictions.
  int min_y_overlay = popup_min_y - popup_bounds.y();
  if (min_y_overlay > 0) {
    // gfx::Vector2d min_y_offset{0, min_y_overlay};
    popup_bounds.Offset(0, min_y_overlay);
    visible_arrow_rect_.Offset(0, min_y_overlay);
  }

  set_visible_arrow(true);
  return true;
}

void BubbleBorder::CalculateVisibleArrowRect(
    const gfx::Rect& contents_bounds,
    const gfx::Point& anchor_point) const {
  const gfx::Insets insets = GetInsets();

  gfx::Point new_origin;
  switch (GetBubbleArrowSide(arrow_)) {
    case BubbleArrowSide::kTop:
      new_origin =
          gfx::Point(anchor_point.x() - kVisibleArrowRadius + 1,
                     contents_bounds.y() + insets.top() - kVisibleArrowLength);
      break;

    case BubbleArrowSide::kBottom:
      new_origin = gfx::Point(anchor_point.x() - kVisibleArrowRadius + 1,
                              contents_bounds.bottom() - insets.bottom());
      break;

    case BubbleArrowSide::kRight:
      new_origin = gfx::Point(contents_bounds.right() - insets.right(),
                              anchor_point.y() - kVisibleArrowRadius + 1);
      break;

    case BubbleArrowSide::kLeft:
      new_origin =
          gfx::Point(contents_bounds.x() + insets.left() - kVisibleArrowLength,
                     anchor_point.y() - kVisibleArrowRadius + 1);
      break;
  }
  visible_arrow_rect_.set_origin(new_origin);
  visible_arrow_rect_.set_size(GetVisibleArrowSize(arrow_));
}

SkRRect BubbleBorder::GetClientRect(const View& view) const {
  gfx::RectF bounds(view.GetLocalBounds());
  bounds.Inset(gfx::InsetsF(GetInsets()));
  return SkRRect(gfx::RRectF(bounds, rounded_corners_));
}

bool BubbleBorder::ShouldDrawStroke() const {
  return ShouldDrawStrokeForArgs(draw_border_stroke_, md_shadow_elevation_,
                                 shadow_);
}

void BubbleBorder::PaintNoShadow(const View& view, gfx::Canvas* canvas) {
  gfx::ScopedCanvas scoped(canvas);
  // ZEPHYRUS: clip to the nub outline when there is one. This method clears
  // everything OUTSIDE the clip to transparent, so clipping to the plain
  // rounded rect would erase the nub that BubbleBackground just painted.
  if (const std::optional<SkPath> outline =
          GetZephyrusNubOutline(view, GetClientRect(view))) {
    canvas->sk_canvas()->clipPath(*outline, SkClipOp::kDifference,
                                  true /*doAntiAlias*/);
    canvas->sk_canvas()->drawColor(SkColors::kTransparent, SkBlendMode::kSrc);
    return;
  }
  canvas->sk_canvas()->clipRRect(GetClientRect(view), SkClipOp::kDifference,
                                 true /*doAntiAlias*/);
  canvas->sk_canvas()->drawColor(SkColors::kTransparent, SkBlendMode::kSrc);
}

void BubbleBorder::PaintVisibleArrow(const View& view, gfx::Canvas* canvas) {
  gfx::Point arrow_origin = visible_arrow_rect_.origin();
  View::ConvertPointFromScreen(&view, &arrow_origin);
  const gfx::Rect arrow_bounds(arrow_origin, visible_arrow_rect_.size());

  // Clip the canvas to a box that's big enough to hold the shadow in every
  // dimension but won't overlap the bubble itself.
  gfx::ScopedCanvas scoped(canvas);
  gfx::Rect clip_rect = arrow_bounds;
  const BubbleArrowSide side = GetBubbleArrowSide(arrow_);
  clip_rect.Inset(gfx::Insets::TLBR(side == BubbleArrowSide::kBottom ? 0 : -2,
                                    side == BubbleArrowSide::kRight ? 0 : -2,
                                    side == BubbleArrowSide::kTop ? 0 : -2,
                                    side == BubbleArrowSide::kLeft ? 0 : -2));
  canvas->ClipRect(clip_rect);

  // Unlike the flags for drawing the border, these are not cached because
  // arrows are currently rare. Should this change over time, we might want to
  // cache these flags, too.
  cc::PaintFlags flags;
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);

  if (ShouldDrawStroke()) {
    flags.setColor(view.GetColorProvider()->GetColor(ui::kColorBubbleBorder));
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.2);
    flags.setAntiAlias(true);
    flags.setLooper(gfx::CreateShadowDrawLooper(GetShadowValues(
        view.GetColorProvider(), md_shadow_elevation_, shadow_)));
    canvas->DrawPath(
        GetVisibleArrowPath(arrow_, arrow_bounds, BubbleArrowPart::kBorder),
        flags);
  }

  flags.setColor(color().ResolveToSkColor(view.GetColorProvider()));
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setStrokeWidth(1.0);
  flags.setAntiAlias(true);
  flags.setLooper(gfx::CreateShadowDrawLooper(
      GetShadowValues(view.GetColorProvider(), md_shadow_elevation_, shadow_)));
  canvas->DrawPath(
      GetVisibleArrowPath(arrow_, arrow_bounds, BubbleArrowPart::kFill), flags);
}

std::optional<SkPath> BubbleBorder::GetZephyrusNubOutline(
    const View& view,
    const SkRRect& r_rect) const {
  // An explicitly supplied anchor wins. This is the menu path: it has no arrow
  // rect because nothing ever computed one.
  if (zephyrus_nub_screen_x_.has_value()) {
    gfx::Point anchor(static_cast<int>(*zephyrus_nub_screen_x_), 0);
    View::ConvertPointFromScreen(&view, &anchor);
    return BuildOutlineWithTopNub(r_rect, static_cast<float>(anchor.x()),
                                  kVisibleArrowRadius, kVisibleArrowLength);
  }
  if (!visible_arrow_ || visible_arrow_rect_.IsEmpty() ||
      GetBubbleArrowSide(arrow_) != BubbleArrowSide::kTop) {
    return std::nullopt;
  }
  // Screen space to view space, the same conversion PaintVisibleArrow() does.
  gfx::Point arrow_origin = visible_arrow_rect_.origin();
  View::ConvertPointFromScreen(&view, &arrow_origin);
  const float center_x = arrow_origin.x() + visible_arrow_rect_.width() / 2.f;
  return BuildOutlineWithTopNub(r_rect, center_x, kVisibleArrowRadius,
                                kVisibleArrowLength);
}

void BubbleBackground::Paint(gfx::Canvas* canvas, views::View* view) const {
  // Fill the contents with a round-rect region to match the border images.
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(border_->color().ResolveToSkColor(view->GetColorProvider()));
  gfx::RectF bounds(view->GetLocalBounds());
  bounds.Inset(gfx::InsetsF(border_->GetInsets()));

  const SkRRect r_rect(gfx::RRectF(bounds, border_->rounded_corners()));

  // ZEPHYRUS: fill the nub too. Without this the nub is drawn by the border
  // alone and comes out in the shadow colour, which on a dark popup reads as a
  // white spike.
  if (const std::optional<SkPath> outline =
          border_->GetZephyrusNubOutline(*view, r_rect)) {
    canvas->sk_canvas()->drawPath(*outline, flags);
    return;
  }

  canvas->sk_canvas()->drawRRect(r_rect, flags);
}

}  // namespace views

DEFINE_ENUM_CONVERTERS(
    views::BubbleBorder::Arrow,
    {views::BubbleBorder::Arrow::TOP_LEFT, u"TOP_LEFT"},
    {views::BubbleBorder::Arrow::TOP_RIGHT, u"TOP_RIGHT"},
    {views::BubbleBorder::Arrow::BOTTOM_LEFT, u"BOTTOM_LEFT"},
    {views::BubbleBorder::Arrow::BOTTOM_RIGHT, u"BOTTOM_RIGHT"},
    {views::BubbleBorder::Arrow::LEFT_TOP, u"LEFT_TOP"},
    {views::BubbleBorder::Arrow::RIGHT_TOP, u"RIGHT_TOP"},
    {views::BubbleBorder::Arrow::LEFT_BOTTOM, u"LEFT_BOTTOM"},
    {views::BubbleBorder::Arrow::RIGHT_BOTTOM, u"RIGHT_BOTTOM"},
    {views::BubbleBorder::Arrow::TOP_CENTER, u"TOP_CENTER"},
    {views::BubbleBorder::Arrow::BOTTOM_CENTER, u"BOTTOM_CENTER"},
    {views::BubbleBorder::Arrow::LEFT_CENTER, u"LEFT_CENTER"},
    {views::BubbleBorder::Arrow::RIGHT_CENTER, u"RIGHT_CENTER"},
    {views::BubbleBorder::Arrow::NONE, u"NONE"},
    {views::BubbleBorder::Arrow::FLOAT, u"FLOAT"})

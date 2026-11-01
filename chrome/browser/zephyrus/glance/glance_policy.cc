// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/glance/glance_policy.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "base/numerics/safe_conversions.h"

namespace zephyrus::glance {

namespace {

// A real address is never this long. A page that hands the browser a
// megabyte-long href is trying something, and a card is not where to find out.
constexpr size_t kMaxUrlLength = 16 * 1024;

// How high the arc rises, at most, and as a share of the distance travelled.
constexpr int kMaxArcHeight = 20;
constexpr double kArcShare = 0.2;

// The patch the card grows from.
constexpr int kOriginWidth = 96;
constexpr int kOriginHeight = 32;

}  // namespace

Trigger TriggerFromName(std::string_view name) {
  if (name == "ctrl") {
    return Trigger::kCtrl;
  }
  if (name == "shift") {
    return Trigger::kShift;
  }
  return Trigger::kAlt;
}

std::string_view NameOf(Trigger trigger) {
  switch (trigger) {
    case Trigger::kAlt:
      return "alt";
    case Trigger::kCtrl:
      return "ctrl";
    case Trigger::kShift:
      return "shift";
  }
  return "alt";
}

bool IsGlanceableUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && url.has_host() &&
         url.possibly_invalid_spec().size() <= kMaxUrlLength;
}

bool IsGlanceClick(const Config& config, Modifiers m, Button button) {
  if (!config.enabled || button != Button::kLeft) {
    return false;
  }
  const bool trigger_held = config.trigger == Trigger::kAlt    ? m.alt
                            : config.trigger == Trigger::kCtrl ? m.ctrl
                                                               : m.shift;
  const int held =
      (m.alt ? 1 : 0) + (m.ctrl ? 1 : 0) + (m.shift ? 1 : 0) + (m.meta ? 1 : 0);
  return trigger_held && held == 1;
}

ClickGate::ClickGate() = default;
ClickGate::~ClickGate() = default;

ClickGate::Action ClickGate::OnMouse(const Config& config,
                                     MouseType type,
                                     Button button,
                                     Modifiers modifiers,
                                     const GURL& hovered_link,
                                     GURL* url) {
  if (type == MouseType::kDown) {
    if (IsGlanceClick(config, modifiers, button) &&
        IsGlanceableUrl(hovered_link)) {
      pending_ = hovered_link;
      return Action::kSwallow;
    }
    // Any other press ends the story of an earlier one.
    pending_ = GURL();
    return Action::kPass;
  }
  if (type == MouseType::kUp && button == Button::kLeft && has_pending()) {
    // The release that finishes a swallowed press is swallowed or turned into
    // the open whatever the page would make of it: the page never saw the
    // press, so it must not see the release either. Letting go of the key
    // first is a cancel, though, not an open.
    GURL link = std::move(pending_);
    pending_ = GURL();
    if (IsGlanceClick(config, modifiers, button) && url) {
      *url = std::move(link);
      return Action::kOpen;
    }
    return Action::kSwallow;
  }
  return Action::kPass;
}

void ClickGate::Reset() {
  pending_ = GURL();
}

gfx::Rect CardBounds(const gfx::Size& area) {
  if (area.IsEmpty()) {
    return gfx::Rect();
  }
  // Room for the button column on BOTH sides, so the card stays centred.
  const int room = area.width() - 2 * (kButtonColumnWidth + kButtonColumnGap);
  int width = base::ClampRound(area.width() * kCardWidthShare);
  width = std::min(width, std::max(room, kMinCardWidth));
  width = std::clamp(width, 1, area.width());
  const int height = std::max(1, area.height() - 2 * kCardVerticalMargin);
  return gfx::Rect((area.width() - width) / 2, (area.height() - height) / 2,
                   width, height);
}

gfx::Rect ButtonColumnBounds(const gfx::Rect& card, const gfx::Size& area) {
  const int outside_x = card.right() + kButtonColumnGap;
  if (outside_x + kButtonColumnWidth <= area.width()) {
    return gfx::Rect(outside_x, card.y(), kButtonColumnWidth, card.height());
  }
  // No room beside it: inside the corner, over the page.
  return gfx::Rect(card.right() - kButtonColumnWidth - kButtonColumnGap,
                   card.y() + kButtonColumnGap, kButtonColumnWidth,
                   std::max(0, card.height() - 2 * kButtonColumnGap));
}

gfx::Rect OriginRect(const gfx::Point& click, const gfx::Size& area) {
  gfx::Rect rect(click.x() - kOriginWidth / 2, click.y() - kOriginHeight / 2,
                 kOriginWidth, kOriginHeight);
  rect.AdjustToFit(gfx::Rect(area));
  return rect;
}

double Ease(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

gfx::Rect RectAt(double t, const gfx::Rect& from, const gfx::Rect& to) {
  t = std::clamp(t, 0.0, 1.0);
  if (t <= 0) {
    return from;
  }
  if (t >= 1) {
    return to;
  }
  const double from_cx = from.CenterPoint().x();
  const double from_cy = from.CenterPoint().y();
  const double to_cx = to.CenterPoint().x();
  const double to_cy = to.CenterPoint().y();
  const double distance = std::hypot(to_cx - from_cx, to_cy - from_cy);
  // Zero at both ends and highest in the middle, so the card leaves and lands
  // exactly where it should.
  const double lift =
      std::min(distance * kArcShare, static_cast<double>(kMaxArcHeight)) *
      std::sin(t * std::numbers::pi);
  const double cx = from_cx + (to_cx - from_cx) * t;
  const double cy = from_cy + (to_cy - from_cy) * t - lift;
  const double w = from.width() + (to.width() - from.width()) * t;
  const double h = from.height() + (to.height() - from.height()) * t;
  return gfx::Rect(base::ClampRound(cx - w / 2), base::ClampRound(cy - h / 2),
                   std::max(1, base::ClampRound(w)),
                   std::max(1, base::ClampRound(h)));
}

double ButtonsProgress(base::TimeDelta since_open_began) {
  return Ease((since_open_began - kButtonsDelay) / kButtonsDuration);
}

}  // namespace zephyrus::glance

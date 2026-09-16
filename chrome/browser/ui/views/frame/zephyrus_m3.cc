// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_m3.h"

#include <string>
#include <vector>

#include <algorithm>
#include <cmath>
#include <optional>

#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "ui/gfx/font.h"
#include "ui/views/background.h"
#include "ui/gfx/font_list.h"

namespace zephyrus::m3 {
namespace {

struct Style {
  int size;
  gfx::Font::Weight weight;
  gfx::Font::Weight emphasized;
};

// M3's type scale. Sizes are the spec's; weights follow its rule that title and
// label styles are medium while body, headline and display are regular.
//
// The emphasized column is M3's second set: one step up, never two. Bold on a
// 57px display style is a wall, which is why display/headline stop at medium
// rather than continuing to bold.
constexpr Style kStyles[] = {
    // display large / medium / small
    {57, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    {45, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    {36, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    // headline large / medium / small
    {32, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    {28, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    {24, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    // title large / medium / small
    {22, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    {16, gfx::Font::Weight::MEDIUM, gfx::Font::Weight::SEMIBOLD},
    {14, gfx::Font::Weight::MEDIUM, gfx::Font::Weight::SEMIBOLD},
    // body large / medium / small
    {16, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    {14, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    {12, gfx::Font::Weight::NORMAL, gfx::Font::Weight::MEDIUM},
    // label large / medium / small
    {14, gfx::Font::Weight::MEDIUM, gfx::Font::Weight::SEMIBOLD},
    {12, gfx::Font::Weight::MEDIUM, gfx::Font::Weight::SEMIBOLD},
    {11, gfx::Font::Weight::MEDIUM, gfx::Font::Weight::SEMIBOLD},
};

static_assert(std::size(kStyles) ==
                  static_cast<size_t>(Type::kLabelSmall) + 1,
              "kStyles must stay in step with Type");

}  // namespace

gfx::FontList Font(Type style, bool emphasized) {
  // Indexed through a span: raw array subscripting trips
  // -Wunsafe-buffer-usage, and the span is bounds-checked.
  const Style& s = base::span(kStyles)[static_cast<size_t>(style)];

  // INTER FIRST, then whatever the platform resolved as its UI font.
  //
  // Inter is the product's typeface: chrome/installer/zephyrus_setup/fonts
  // ships Inter-Regular and Inter-SemiBold and the installer renders its own UI
  // in them. A browser that does not match its own installer is the wrong kind
  // of inconsistent.
  //
  // This corrects a mistake. The search overlay asked for "Inter, Segoe UI" and
  // that was replaced here with a platform-font derivation on the reasoning
  // that Inter was not in the tree -- it is, and where a user has it installed
  // the overlay really was rendering in Inter. The swap was a regression
  // dressed up as a cleanup.
  //
  // The FALLBACK still matters and is why this is a list rather than a name:
  // Inter is not installed for every user (bundling it is the open Phase 2b
  // item), and it has no CJK or Devanagari coverage. Naming the platform font
  // second means those users and those scripts get the right face instead of a
  // missing-glyph box, which is the actual bug hardcoding "Segoe UI" had.
  const gfx::FontList base;
  const std::vector<std::string> families{
      "Inter", base.GetPrimaryFont().GetFontName()};
  return gfx::FontList(families, gfx::Font::NORMAL, s.size,
                       emphasized ? s.emphasized : s.weight);
}

const gfx::CubicBezier& Curve(Spring spring) {
  // Plain function-local statics, NOT base::NoDestructor: gfx::CubicBezier is
  // trivially destructible and NoDestructor static_asserts against exactly
  // that. Same trap as the published-palette slot in zephyrus_bubble_style.cc.
  //
  // M3's published spring-to-curve conversion, verbatim. The Expressive set is
  // used rather than Standard: it is the one with visible overshoot, and the
  // expressive character is the reason for adopting the system at all.
  static const gfx::CubicBezier kFastSpatial(
      0.42, 1.67, 0.21, 0.90);
  static const gfx::CubicBezier kDefaultSpatial(
      0.38, 1.21, 0.22, 1.00);
  static const gfx::CubicBezier kSlowSpatial(
      0.39, 1.29, 0.35, 0.98);
  static const gfx::CubicBezier kFastEffects(
      0.31, 0.94, 0.34, 1.00);
  static const gfx::CubicBezier kDefaultEffects(
      0.34, 0.80, 0.34, 1.00);
  static const gfx::CubicBezier kSlowEffects(
      0.34, 0.88, 0.34, 1.00);

  switch (spring) {
    case Spring::kFastSpatial:
      return kFastSpatial;
    case Spring::kDefaultSpatial:
      return kDefaultSpatial;
    case Spring::kSlowSpatial:
      return kSlowSpatial;
    case Spring::kFastEffects:
      return kFastEffects;
    case Spring::kDefaultEffects:
      return kDefaultEffects;
    case Spring::kSlowEffects:
      return kSlowEffects;
  }
  // Falling off a non-void switch is UB if a value outside the enum is ever
  // cast in. Exhaustive today, but the compiler only checks the named cases.
  NOTREACHED();
}

base::TimeDelta Duration(Spring spring) {
  switch (spring) {
    case Spring::kFastSpatial:
      return base::Milliseconds(350);
    case Spring::kDefaultSpatial:
      return base::Milliseconds(500);
    case Spring::kSlowSpatial:
      return base::Milliseconds(650);
    case Spring::kFastEffects:
      return base::Milliseconds(150);
    case Spring::kDefaultEffects:
      return base::Milliseconds(200);
    case Spring::kSlowEffects:
      return base::Milliseconds(300);
  }
  NOTREACHED();
}

gfx::Tween::Type TweenFor(Spring spring) {
  switch (spring) {
    // The spatial springs OVERSHOOT. EASE_OUT_4 is the only Tween documented as
    // leading into a bounce, so it is the closest available shape -- closest,
    // not equal: it does not actually cross 1.0, so the overshoot is lost.
    // That is the cost of staying on the compositor thread.
    case Spring::kFastSpatial:
    case Spring::kDefaultSpatial:
    case Spring::kSlowSpatial:
      return gfx::Tween::EASE_OUT_4;
    // The effects springs are smooth decelerations with no overshoot, which
    // EASE_OUT_3 matches closely.
    case Spring::kFastEffects:
    case Spring::kDefaultEffects:
    case Spring::kSlowEffects:
      return gfx::Tween::EASE_OUT_3;
  }
  NOTREACHED();
}

namespace {

// A pill: radius is half the shorter side, so there is nothing to make
// concentric. Exemption 1.
bool IsCapsule(const gfx::RoundedCornersF& r, const gfx::Size& size) {
  const float half = std::min(size.width(), size.height()) / 2.0f;
  return r.upper_left() >= half - 1.f && r.upper_right() >= half - 1.f;
}

std::optional<gfx::RoundedCornersF> RadiiOf(const views::View& v) {
  return v.GetBackground() ? v.GetBackground()->GetRoundedCornerRadii()
                           : std::nullopt;
}

// Counts so a CLEAN run is distinguishable from a run that never happened.
// Without them "no violations" and "never called" look identical in the log,
// which is the same false pass as trusting a build that did not rebuild.
int g_rounded_seen = 0;
int g_violations = 0;

void AuditInto(const views::View& v, const views::View* rounded_ancestor) {
  const std::optional<gfx::RoundedCornersF> mine = RadiiOf(v);
  if (mine) {
    ++g_rounded_seen;
  }
  if (mine && rounded_ancestor) {
    const std::optional<gfx::RoundedCornersF> theirs =
        RadiiOf(*rounded_ancestor);
    const gfx::Rect outer = rounded_ancestor->GetLocalBounds();
    const gfx::Rect inner = views::View::ConvertRectToTarget(
        v.parent(), rounded_ancestor, v.bounds());
    const int pad = std::min({inner.x() - outer.x(), inner.y() - outer.y(),
                              outer.right() - inner.right(),
                              outer.bottom() - inner.bottom()});
    if (theirs && pad >= 0 && !IsCapsule(*mine, v.size())) {
      const float want = ConcentricInner(theirs->upper_left(),
                                         static_cast<float>(pad));
      // One pixel of tolerance: the gap is measured from bounds, so uneven
      // padding and odd sizes produce off-by-ones that are not violations.
      if (std::abs(mine->upper_left() - want) > 1.f) {
        ++g_violations;
        LOG(WARNING) << "[zephyrus] Rule 2: " << v.GetClassName() << " radius "
                     << mine->upper_left() << " inside "
                     << rounded_ancestor->GetClassName() << " radius "
                     << theirs->upper_left() << " with padding " << pad
                     << " -- concentric would be " << want;
      }
    }
  }
  const views::View* next = mine ? &v : rounded_ancestor;
  for (const views::View* child : v.children()) {
    AuditInto(*child, next);
  }
}

}  // namespace

void AuditConcentricity(const views::View& root) {
  // Parsed once. This is called on every activation, and re-scanning the
  // command line each time would be a cost paid by everyone to serve a
  // debugging switch almost nobody sets.
  static const bool kEnabled =
      base::CommandLine::ForCurrentProcess()->HasSwitch(kAuditShapeSwitch);
  if (!kEnabled) {
    return;
  }
  g_rounded_seen = 0;
  g_violations = 0;
  AuditInto(root, nullptr);
  LOG(WARNING) << "[zephyrus] Rule 2 audit: walked " << g_rounded_seen
               << " rounded views, " << g_violations << " violation(s)";
}

}  // namespace zephyrus::m3

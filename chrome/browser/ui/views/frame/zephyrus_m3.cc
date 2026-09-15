// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_m3.h"

#include "base/containers/span.h"
#include "base/notreached.h"
#include "ui/gfx/font.h"
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

  // DERIVED from the platform UI font, not built from a family name.
  //
  // The call sites this replaces named "Segoe UI" directly, which is wrong on
  // two counts: it hardcodes a Windows font into cross-platform code, and it
  // bypasses whatever the platform has resolved as the UI font for the user's
  // locale -- a CJK or Devanagari UI does not want Segoe UI Latin metrics.
  // Deriving keeps the platform's choice and changes only size and weight.
  const gfx::FontList base;
  return base
      .DeriveWithSizeDelta(s.size - base.GetFontSize())
      .DeriveWithWeight(emphasized ? s.emphasized : s.weight);
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

}  // namespace zephyrus::m3

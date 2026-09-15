// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_H_

#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/color_utils.h"
#include "base/time/time.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/cubic_bezier.h"
#include "ui/views/view.h"

// Material 3, view side.
//
// PHASE 0 of the M3 overhaul (chrome/browser/zephyrus/M3_UI_OVERHAUL.md).
// The colour roles themselves live in zephyrus_color_mixer.h; this is how a
// View reaches them, plus the parts of M3 that are arithmetic rather than
// colour -- state layers, scrims, and concentric corners.
//
// Nothing in here is wired up yet. Phase 0 adds the vocabulary; Phase 0b and
// Phase 4 start speaking it.
namespace zephyrus::m3 {

// ---------------------------------------------------------------------------
// Reading a role
// ---------------------------------------------------------------------------

// The colour of `id` as `view`'s theme currently defines it.
//
// Must not be called before `view` is in a Widget -- a View has no
// ColorProvider until then, which is the same constraint that made
// PaletteFrom() a snapshot taken in OnThemeChanged(). The same rule applies
// here: read in OnThemeChanged(), not in a constructor.
inline SkColor Role(const views::View& view, ui::ColorId id) {
  const ui::ColorProvider* provider = view.GetColorProvider();
  // Magenta rather than a plausible grey: a role read too early is a bug that
  // should be impossible to miss, not one that quietly ships a wrong shade.
  return provider ? provider->GetColor(id) : SkColorSetRGB(0xFF, 0x00, 0xFF);
}

// ---------------------------------------------------------------------------
// State layers
// ---------------------------------------------------------------------------
//
// A state layer is a semi-transparent wash over a component, in the colour of
// its CONTENT -- the "on" role, not the container. Only one applies at a time.
//
// The opacities are M3's, and they are not the ones this browser has been
// using: our buttons ran 8% hover / 20% press, where the spec says 8% and 10%.
// The 20% press is why our controls feel heavier than the rest of the UI.
inline constexpr SkAlpha kHover = 0x14;     // 8%
inline constexpr SkAlpha kFocus = 0x1A;     // 10%
inline constexpr SkAlpha kPressed = 0x1A;   // 10%
inline constexpr SkAlpha kDragged = 0x29;   // 16%
inline constexpr SkAlpha kDisabled = 0x61;  // 38%

// M3 scrims: the scrim role at 32%.
inline constexpr SkAlpha kScrim = 0x52;

// The state layer itself, as a colour to paint OVER a container.
//
// `content` is the component's own foreground -- the icon or label colour. Use
// this when the layer is drawn as its own pass.
inline SkColor StateLayer(SkColor content, SkAlpha opacity) {
  return SkColorSetA(content, opacity);
}

// The container WITH its state layer already composited in.
//
// Use this when the component paints a single flat fill and cannot afford a
// second pass. The result is opaque, so it is also what to use on a surface
// that must not show what is behind it.
inline SkColor WithStateLayer(SkColor container,
                              SkColor content,
                              SkAlpha opacity) {
  return color_utils::AlphaBlend(content, container, opacity);
}

// ---------------------------------------------------------------------------
// Type scale
// ---------------------------------------------------------------------------
//
// M3's five roles at three sizes each. Fifteen styles, and a product uses a
// handful of them -- the point is that it uses the SAME handful everywhere.
//
// This replaces nothing, because there was nothing: font sizes were typed in at
// each call site as strings ("Segoe UI Semibold, 15px", "Segoe UI, 11px") or as
// a delta off whatever the inherited font happened to be. One of those strings
// named Inter, which is not in the tree, so it had been silently falling back
// to Segoe UI.
//
// Sizes are M3's. Weights are M3's: title/label are medium, body/headline are
// regular. `emphasized` is M3's second set -- one step heavier, for selected
// items, primary actions and headlines that need to carry.
//
// NOTE the sizes are specified against Roboto's metrics. Until Roboto is
// bundled these render in the platform UI font, so line heights and tracking
// are approximate even though the sizes are exact. That is the known gap in
// Phase 2, not an oversight here.
enum class Type {
  kDisplayLarge,   // 57
  kDisplayMedium,  // 45
  kDisplaySmall,   // 36
  kHeadlineLarge,  // 32
  kHeadlineMedium, // 28
  kHeadlineSmall,  // 24  -- dialog headlines
  kTitleLarge,     // 22
  kTitleMedium,    // 16
  kTitleSmall,     // 14
  kBodyLarge,      // 16
  kBodyMedium,     // 14  -- dialog supporting text
  kBodySmall,      // 12
  kLabelLarge,     // 14  -- button labels
  kLabelMedium,    // 12
  kLabelSmall,     // 11  -- uppercase section headings
};

// The font for `style`, derived from the platform UI font.
gfx::FontList Font(Type style, bool emphasized = false);

// ---------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------
//
// M3's motion system is spring physics. For platforms without springs the spec
// publishes exact cubic-bezier equivalents, and these are those -- not an
// approximation someone eyeballed.
//
// gfx::Tween CANNOT express them: every M3 spatial curve overshoots (its third
// control point sits above 1.0) and no Tween::Type does. So these are solved
// with gfx::CubicBezier and applied to a LINEAR animation's value. The
// overshoot is the point -- it is what makes motion read as Material rather
// than as a generic ease-out.
//
// SPATIAL moves and resizes things. EFFECTS changes colour and opacity. Using
// a spatial curve on a fade makes it look like it bounced; using an effects
// curve on a move makes it look stiff.
enum class Spring {
  kFastSpatial,       // 350ms
  kDefaultSpatial,    // 500ms
  kSlowSpatial,       // 650ms
  kFastEffects,       // 150ms
  kDefaultEffects,    // 200ms
  kSlowEffects,       // 300ms
};

// The curve for `spring`. Solve() takes a linear 0..1 and returns the eased
// value; pair it with gfx::Tween::LINEAR so the curve is applied exactly once.
const gfx::CubicBezier& Curve(Spring spring);

// The spec's duration for `spring`.
base::TimeDelta Duration(Spring spring);

// ---------------------------------------------------------------------------
// Rule 2 — corner concentricity
// ---------------------------------------------------------------------------
//
// When a rounded shape sits inside another rounded shape, their corners share
// a centre. The inner radius is DERIVED, never chosen:
//
//     inner = outer - the padding between them
//
// M3 states the same formula as "optical roundness" but treats it as guidance.
// Here it is a rule. See section 0 of the overhaul document for the two
// exemptions -- capsules, whose radius is a function of height and so has
// nothing to make concentric, and the interior seams of a connected control,
// which take their corner from the component spec instead.
//
// Where a component spec names a corner, that number wins. This governs
// everywhere the spec is silent, which is every nesting relationship in the
// browser -- and that is precisely where we have got it wrong before.
inline constexpr float ConcentricInner(float outer, float padding) {
  // Clamped at zero: once the padding exceeds the outer radius the curves have
  // already flattened, and a negative radius is not a shape.
  return (outer - padding) > 0.f ? (outer - padding) : 0.f;
}

inline constexpr int ConcentricInner(int outer, int padding) {
  return (outer - padding) > 0 ? (outer - padding) : 0;
}

}  // namespace zephyrus::m3

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_M3_H_

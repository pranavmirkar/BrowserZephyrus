// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BUBBLE_STYLE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BUBBLE_STYLE_H_

#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/controls/label.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/canvas.h"

namespace views {
class BubbleDialogDelegate;
class View;
}

#include "chrome/browser/ui/zephyrus_version.h"

namespace zephyrus {

// zephyrus::kVersion is defined in zephyrus_version.h (included above), kept in
// chrome/browser/ui so the settings WebUI can reach it too.

// The radius every Zephyrus card surface uses — bubbles, dialogs, rows,
// buttons, and the omnibox results card.
// SHAPE IS BINARY. Measured on the reference: 144 elements at full pill against
// 16 at 8px and 14 at 6px -- there is essentially no mid-range rounding, which
// is the range most interfaces live in.
//
//   Pill  -> you can press it (buttons, chips, toggles, the omnibox field)
//   Card  -> it holds other things (panels, popups, cards, split panes)
//   Nothing in between. A 12px or 16px radius is always one of these two,
//   chosen wrong.
inline constexpr int kRadiusCard = 8;
// Kept as the old name so the many existing call sites stay put; it was 10,
// which is exactly the mid-range value the rule above exists to remove.
inline constexpr int kCornerRadius = kRadiusCard;

// A THIRD radius, for surfaces that FLOAT over the window: context menus, the
// omnibox results, and the popups hanging off the title bar.
//
// This is not the mid-range value the rule above rejects. The distinction is
// real: a card is embedded in the layout and shares the window's edges, while a
// popup is a separate pane cast on top of it and reads as its own object. 8px
// on something floating looks like a clipped rectangle; 28px says "this is a
// sheet, not part of the page".
//
// Embedded cards stay at kRadiusCard. Do not merge the two.
inline constexpr int kRadiusPopup = 24;

// The nub: the small point on the top edge of an anchored popup, aimed at the
// control that opened it.
//
// ONLY on popups with a real anchor (the shield, downloads, extensions). A
// context menu opens at the cursor and the omnibox results span the whole
// field, so a nub there would point at nothing and is deliberately absent.
inline constexpr int kNubWidth = 20;
inline constexpr int kNubHeight = 10;

// ---------------------------------------------------------------------------
// The Zephyrus palette.
//
// This header is the SOURCE for every colour in the browser. Nothing else
// defines one -- two independent copies of a palette is exactly how this
// browser ended up with two empty states.
//
// The language is Nothing OS: a greyscale ramp and ONE accent. Values are
// measured from Nothing's own stylesheets rather than eyeballed.
//
// TWO THEMES, which is new. Zephyrus previously had a single permanent colour
// and no light/dark switching at all. That does not survive this language: a
// near-black ground is half of it. Following the OS setting also finally makes
// our chrome agree with the native surfaces (context menus, WebUI, dialogs)
// that were always following it regardless -- a long-standing mismatch that the
// single-theme rule could never fix.

struct Palette {
  SkColor ground;      // Base. The window's own colour.
  SkColor surface;     // A raised panel or card sitting on the ground.
  SkColor rule;        // 1px hairlines. Separation is a line, not a shadow.
  SkColor ink;         // Primary text and solid fills.
  SkColor muted;       // Secondary text.
  SkColor faint;       // Disabled text, and the quietest borders.
  SkColor accent;      // The ONLY accent. See the warning below.
  SkColor accent_ink;  // Text/glyphs sitting ON the accent.
};

// Light. #FFFFFF ground with pure-black text, but #040404 for large dark
// FILLS -- a softening that stops big dark areas reading as holes punched in
// the page. Both values are real and used for different jobs.
inline constexpr Palette kLightPalette = {
    .ground = SkColorSetRGB(0xFF, 0xFF, 0xFF),
    .surface = SkColorSetRGB(0xF4, 0xF4, 0xF4),
    .rule = SkColorSetRGB(0xE2, 0xE2, 0xE2),
    .ink = SkColorSetRGB(0x04, 0x04, 0x04),
    .muted = SkColorSetRGB(0x59, 0x5A, 0x5A),
    .faint = SkColorSetRGB(0xB1, 0xB3, 0xB3),
    .accent = SkColorSetRGB(0xC6, 0x10, 0x2E),
    .accent_ink = SkColorSetRGB(0xFF, 0xFF, 0xFF),
};

// Dark. The accent is NOT the same value: #C6102E on a near-black ground falls
// to roughly 3:1 and reads muddy, so it is lifted to #FF3B52. This is a
// deliberate deviation from the source palette, recorded here rather than
// introduced silently.
inline constexpr Palette kDarkPalette = {
    .ground = SkColorSetRGB(0x04, 0x04, 0x04),
    .surface = SkColorSetRGB(0x12, 0x12, 0x12),
    .rule = SkColorSetRGB(0x26, 0x26, 0x26),
    .ink = SkColorSetRGB(0xFF, 0xFF, 0xFF),
    .muted = SkColorSetRGB(0xB1, 0xB3, 0xB3),
    .faint = SkColorSetRGB(0x59, 0x5A, 0x5A),
    .accent = SkColorSetRGB(0xFF, 0x3B, 0x52),
    .accent_ink = SkColorSetRGB(0x04, 0x04, 0x04),
};

// Which palette is live, resolved from the OS dark-mode setting.
//
// Defined in the .cc so this header does not drag native_theme.h into every
// translation unit that includes browser_view.h. Never call it during static
// initialisation -- NativeTheme is not ready that early.
const Palette& Current();

// Private Workspace: a GREY palette of its own.
//
// It used to be the inverse of whatever theme was active -- light chrome inside
// a dark browser, and vice versa. That was a defensible way to say "this window
// is different" without spending a second accent, and it looked wrong in
// practice: an inverted title bar reads as a rendering fault or a different
// application rather than a mode of this one, and it fights whatever the page
// is doing underneath.
//
// Grey says the same thing more quietly. It is clearly neither the light nor
// the dark theme, so the window is unmistakably in another mode, but it is
// still recognisably this browser. The ink stays white and the accent keeps the
// lifted red, so contrast behaves the way it does on the dark theme.
inline constexpr Palette kPrivatePalette = {
    .ground = SkColorSetRGB(0x2E, 0x2E, 0x30),
    .surface = SkColorSetRGB(0x3A, 0x3A, 0x3D),
    .rule = SkColorSetRGB(0x4C, 0x4C, 0x50),
    .ink = SkColorSetRGB(0xFF, 0xFF, 0xFF),
    .muted = SkColorSetRGB(0xB1, 0xB3, 0xB3),
    .faint = SkColorSetRGB(0x7A, 0x7A, 0x7E),
    .accent = SkColorSetRGB(0xFF, 0x3B, 0x52),
    .accent_ink = SkColorSetRGB(0x04, 0x04, 0x04),
};

// The palette for one window.
const Palette& PaletteFor(bool is_private);

inline SkColor Ground() { return Current().ground; }
inline SkColor Surface() { return Current().surface; }
inline SkColor Rule() { return Current().rule; }
inline SkColor Ink() { return Current().ink; }
inline SkColor Muted() { return Current().muted; }
inline SkColor Faint() { return Current().faint; }
inline SkColor AccentInk() { return Current().accent_ink; }

// The accent.
//
// ONE accent, and it means "now": recording, live blocking, destructive
// confirmation, an error. It has no decorative use. Its entire value is how
// rarely it appears, so every new call site spends some of that value --
// treat adding one as a decision, not a styling choice.
//
// It is NOT free to use on security surfaces: Chromium's SSL interstitials are
// already full-screen red because red means danger. Using the same red for
// ordinary chrome makes those warnings less distinguishable, which is a safety
// problem rather than an aesthetic one.
inline SkColor Accent() { return Current().accent; }

// Ink appropriate for `base`.
//
// Use this instead of color_utils::GetColorWithMaxContrast(base). Surfaces
// painted over PAGE content still fall back to max contrast, because there the
// page really does decide -- those are the chameleon surfaces and they are
// supposed to follow whatever is behind them.
inline SkColor InkFor(SkColor base) {
  const Palette& light = kLightPalette;
  const Palette& dark = kDarkPalette;
  if (base == light.ground || base == light.surface) {
    return light.ink;
  }
  if (base == dark.ground || base == dark.surface) {
    return dark.ink;
  }
  // The private greys resolve to white by max contrast anyway, but say so
  // explicitly: relying on the fallback means a later tweak to those greys
  // could silently flip the ink to black.
  if (base == kPrivatePalette.ground || base == kPrivatePalette.surface) {
    return kPrivatePalette.ink;
  }
  return color_utils::GetColorWithMaxContrast(base);
}

// Raises a surface off `base` by one step.
//
// Under Nothing there is very little elevation to express: separation comes
// from a hairline and a flat surface step, not from a shadow or a wash. So this
// stays deliberately shallow, and exists mainly so hover and press remain
// distinguishable from idle.
//
// Direction depends on the ground. A dark ground raises toward white; a light
// ground has nowhere lighter to go, so it steps very slightly toward the ink
// and lets the hairline carry the elevation instead.
inline SkColor Raise(SkColor base, SkAlpha a) {
  if (color_utils::IsDark(base)) {
    return color_utils::AlphaBlend(SK_ColorWHITE, base, a);
  }
  return color_utils::AlphaBlend(SK_ColorBLACK, base,
                                 static_cast<SkAlpha>(a / 4));
}

// Full-pill radius. Large enough that Skia clamps it to half the height.
inline constexpr int kPillRadius = 9999;

// Hairline weight. Separation under this language is a 1px rule and a flat
// surface step -- never a shadow. There is almost no elevation to express.
inline constexpr float kHairline = 1.f;

// Zephyrus bubble chrome, in the two phases the Views API forces on us.
//
// Applying this is NOT optional styling: the frame's default 1px border stroke
// comes from a themed color that renders as a hard outline around the card,
// and separation is meant to come from the drop shadow instead.
//
// Phase 1 — before the widget exists. `set_corner_radius()` only writes into
// the delegate's params, which the frame reads once at creation, so calling it
// afterwards silently does nothing.
void ConfigureBubble(views::BubbleDialogDelegate* bubble);

// Adds the NUB: the small point on a popup's edge, aimed at the control that
// opened it. Call AFTER the widget exists, next to ApplyBubbleFrame().
//
// Only for popups hanging off a title-bar control (the shield, downloads,
// extensions). Deliberately NOT in ApplyBubbleFrame(), which every Zephyrus
// bubble calls: a nub is a claim about what opened the popup, and on something
// like the profile dialog it would point at whatever happened to be underneath.
//
// The nub follows the bubble's arrow position rather than sitting at centre, so
// a popup anchored TOP_RIGHT points at its icon on the right. Chromium draws it
// at kVisibleArrowRadius (9) half-width by kVisibleArrowLength (8) tall, near
// enough to the design's 20x10 to use as-is.
void ApplyAnchoredNub(views::BubbleDialogDelegate* bubble);

// Phase 2 — after the widget exists, because the frame view is created with
// it. For a BubbleDialogDelegateView subclass call this from
// OnWidgetInitialized(); for a plain delegate, right after CreateBubble*().
void ApplyBubbleFrame(views::BubbleDialogDelegate* bubble);

// Call at the top of any "show this bubble" path and bail out when it returns
// true: the click being handled is the one that just dismissed the bubble, so
// re-opening would make the trigger impossible to toggle shut. Arming is
// automatic — ApplyBubbleFrame() above records the close. Each recorded close
// suppresses at most one re-open.
bool ConsumeReopenSuppression(const views::View* anchor);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BUBBLE_STYLE_H_

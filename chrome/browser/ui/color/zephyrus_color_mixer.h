// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_
#define CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_

#include <string>

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"

// The Material 3 colour scheme, resolved against Chromium's theme.
//
// PHASE 0 of the M3 overhaul (chrome/browser/zephyrus/M3_UI_OVERHAUL.md).
// Zephyrus has retired its own design language; this is the 26-role M3 scheme
// that replaces the 8-role Nothing OS palette.
//
// Everything here is a REFERENCE, not a value. Each role is defined in terms of
// Chromium's ref tone ramps, which are themselves generated from a seed colour
// -- Pomegranate #C6102E by default, or whatever the user picked in Customize
// Chrome. So the scheme follows the theme by construction, and there is exactly
// one place colour is decided.
//
// WHY REF TONES AND NOT kColorSys*: Chromium's sys tokens are M3-derived but
// not M3-shaped. There is no kColorSysSurfaceContainerLow/High/Highest -- what
// exists is kColorSysSurface1..5, which are M2-style alpha overlays, plus a
// separate Base/BaseContainer pair. Mapping M3's five container levels onto
// those would be a guess. The ref ramps, by contrast, carry the exact tone
// stops M3 specifies for surface containers (4, 6, 12, 17, 22, 92, 94, 96, 98
// are in the neutral ramp precisely because M3 needs them), so the roles below
// are the spec's own table rather than an approximation of it.
//
// Ids live here rather than in chrome_color_id.h so this costs zero upstream
// lines: they start at kChromeColorsEnd, which is what that marker is for.
enum ZephyrusColorIds : ui::ColorId {
  // -- Primary: the brand. Most prominent actions and active states. --------
  kColorZephyrusPrimary = kChromeColorsEnd,
  kColorZephyrusOnPrimary,
  kColorZephyrusPrimaryContainer,
  kColorZephyrusOnPrimaryContainer,

  // -- Secondary: less prominent components. Filter chips, selected nav. ----
  kColorZephyrusSecondary,
  kColorZephyrusOnSecondary,
  kColorZephyrusSecondaryContainer,
  kColorZephyrusOnSecondaryContainer,

  // -- Tertiary: contrasting accents that balance primary and secondary. ----
  kColorZephyrusTertiary,
  kColorZephyrusOnTertiary,
  kColorZephyrusTertiaryContainer,
  kColorZephyrusOnTertiaryContainer,

  // -- Error. STATIC by M3: does not follow dynamic colour, only light/dark.
  kColorZephyrusError,
  kColorZephyrusOnError,
  kColorZephyrusErrorContainer,
  kColorZephyrusOnErrorContainer,

  // -- Surface, and the five container levels. ------------------------------
  //
  // These replace the hairline as the browser's separation mechanism in Phase
  // 0b. Nesting two regions means stepping one level, not drawing a line.
  kColorZephyrusSurface,
  kColorZephyrusSurfaceContainerLowest,
  kColorZephyrusSurfaceContainerLow,
  kColorZephyrusSurfaceContainer,
  kColorZephyrusSurfaceContainerHigh,
  kColorZephyrusSurfaceContainerHighest,
  kColorZephyrusOnSurface,
  kColorZephyrusOnSurfaceVariant,

  // -- Outline. Boundaries that must be seen, and decorative ones. ----------
  //
  // kColorZephyrusOutlineVariant is where the old hairline goes: M3 uses it for
  // dividers inside lists and menus, and nowhere else. It is NOT the general
  // separation mechanism any more -- see the surface containers above.
  kColorZephyrusOutline,
  kColorZephyrusOutlineVariant,

  // -- Inverse. For surfaces that deliberately contrast with the rest. ------
  kColorZephyrusInverseSurface,
  kColorZephyrusInverseOnSurface,
  kColorZephyrusInversePrimary,

  // -- Scrim and shadow. ----------------------------------------------------
  //
  // M3 draws scrims at 32% of this colour. The alpha is applied at the call
  // site, not baked in here, because the same role is used at other opacities.
  kColorZephyrusScrim,
  kColorZephyrusShadow,

  // -- LEGACY: the retired 8-role Nothing OS palette. -----------------------
  //
  // TEMPORARY SHIM. These keep the ~60 existing Ground()/Ink()/Accent() call
  // sites compiling while the surfaces above them are converted one file at a
  // time in Phase 4. Their mappings are UNCHANGED from before the overhaul, on
  // purpose: Phase 0 must not alter a single pixel.
  //
  // Do not add a new use of these. They are deleted with the last converted
  // surface.
  kColorZephyrusLegacyGround,
  kColorZephyrusLegacySurface,
  kColorZephyrusLegacyRule,
  kColorZephyrusLegacyInk,
  kColorZephyrusLegacyMuted,
  kColorZephyrusLegacyFaint,
  kColorZephyrusLegacyAccent,
  kColorZephyrusLegacyAccentInk,

  kZephyrusColorsEnd,
};

// Defines the ids above from Chromium's own tokens.
//
// Runs LAST, so the tokens it reads are the finished ones -- after the native
// mixer and after any custom theme. Running last is also what lets it override
// Chromium's own tokens, which it now does for a small and growing set (see the
// end of the .cc): going fully M3 means one scheme, so a Chromium surface left
// on its old token is a second scheme sitting next to ours.
//
// This is NOT the old push-mixer, which was deleted for pushing a HARDCODED
// palette over the theme and making Customize Chrome inert. Every role here is
// derived from the ref ramps Chromium itself uses, so a themed colour still
// reaches every surface; only the tone assignment changes.
void AddZephyrusColorMixer(ui::ColorProvider* provider,
                           const ui::ColorProviderKey& key);

// The CSS custom property for an M3 role, e.g. "--color-zephyrus-surface" for
// kColorZephyrusSurface. chrome://theme/colors.css?sets=zephyrus emits every
// role under these names from the SAME ColorProvider the native UI reads, so a
// WebUI page styled with them cannot drift from the browser around it.
//
// Covers kColorZephyrusPrimary up to (not including) the legacy shim roles,
// which are deliberately not exposed: nothing new may be built on them.
std::string ZephyrusColorIdCssName(ui::ColorId id);

#endif  // CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_

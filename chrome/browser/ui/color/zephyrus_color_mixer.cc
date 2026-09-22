// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/zephyrus_color_mixer.h"

#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"
#include "ui/color/color_transform.h"

std::string ZephyrusColorIdCssName(ui::ColorId id) {
  switch (id) {
    case kColorZephyrusPrimary: return "--color-zephyrus-primary";
    case kColorZephyrusOnPrimary: return "--color-zephyrus-on-primary";
    case kColorZephyrusPrimaryContainer:
      return "--color-zephyrus-primary-container";
    case kColorZephyrusOnPrimaryContainer:
      return "--color-zephyrus-on-primary-container";
    case kColorZephyrusSecondary: return "--color-zephyrus-secondary";
    case kColorZephyrusOnSecondary: return "--color-zephyrus-on-secondary";
    case kColorZephyrusSecondaryContainer:
      return "--color-zephyrus-secondary-container";
    case kColorZephyrusOnSecondaryContainer:
      return "--color-zephyrus-on-secondary-container";
    case kColorZephyrusTertiary: return "--color-zephyrus-tertiary";
    case kColorZephyrusOnTertiary: return "--color-zephyrus-on-tertiary";
    case kColorZephyrusTertiaryContainer:
      return "--color-zephyrus-tertiary-container";
    case kColorZephyrusOnTertiaryContainer:
      return "--color-zephyrus-on-tertiary-container";
    case kColorZephyrusError: return "--color-zephyrus-error";
    case kColorZephyrusOnError: return "--color-zephyrus-on-error";
    case kColorZephyrusErrorContainer:
      return "--color-zephyrus-error-container";
    case kColorZephyrusOnErrorContainer:
      return "--color-zephyrus-on-error-container";
    case kColorZephyrusSurface: return "--color-zephyrus-surface";
    case kColorZephyrusSurfaceContainerLowest:
      return "--color-zephyrus-surface-container-lowest";
    case kColorZephyrusSurfaceContainerLow:
      return "--color-zephyrus-surface-container-low";
    case kColorZephyrusSurfaceContainer:
      return "--color-zephyrus-surface-container";
    case kColorZephyrusSurfaceContainerHigh:
      return "--color-zephyrus-surface-container-high";
    case kColorZephyrusSurfaceContainerHighest:
      return "--color-zephyrus-surface-container-highest";
    case kColorZephyrusOnSurface: return "--color-zephyrus-on-surface";
    case kColorZephyrusOnSurfaceVariant:
      return "--color-zephyrus-on-surface-variant";
    case kColorZephyrusOutline: return "--color-zephyrus-outline";
    case kColorZephyrusOutlineVariant:
      return "--color-zephyrus-outline-variant";
    case kColorZephyrusInverseSurface:
      return "--color-zephyrus-inverse-surface";
    case kColorZephyrusInverseOnSurface:
      return "--color-zephyrus-inverse-on-surface";
    case kColorZephyrusInversePrimary:
      return "--color-zephyrus-inverse-primary";
    case kColorZephyrusScrim: return "--color-zephyrus-scrim";
    case kColorZephyrusShadow: return "--color-zephyrus-shadow";
    default:
      // Legacy shim roles and anything outside the M3 scheme: unnamed, so a
      // caller that iterates past the range gets nothing it could style with.
      return std::string();
  }
}

void AddZephyrusColorMixer(ui::ColorProvider* provider,
                           const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();
  const bool dark = key.color_mode == ui::ColorProviderKey::ColorMode::kDark;

  // Every role below is M3's own light/dark tone pair, read off the spec's
  // table. The ramps they reference are generated from the seed colour, so
  // this whole block re-tints coherently when the theme changes and there is
  // nothing here to keep in sync by hand.
  //
  // The pairing is normative: each container role is guaranteed >= 3:1 against
  // its "on" role. Do not mix roles across pairs -- that is exactly how
  // contrast breaks when a user raises contrast or flips theme.

  // -- Primary ---------------------------------------------------------------
  mixer[kColorZephyrusPrimary] = {dark ? ui::kColorRefPrimary80
                                       : ui::kColorRefPrimary40};
  mixer[kColorZephyrusOnPrimary] = {dark ? ui::kColorRefPrimary20
                                         : ui::kColorRefPrimary100};
  mixer[kColorZephyrusPrimaryContainer] = {dark ? ui::kColorRefPrimary30
                                                : ui::kColorRefPrimary90};
  mixer[kColorZephyrusOnPrimaryContainer] = {dark ? ui::kColorRefPrimary90
                                                  : ui::kColorRefPrimary10};

  // -- Secondary -------------------------------------------------------------
  //
  // A REAL tonal palette now. It used to be the brand hue at 12% chroma -- an
  // almost-grey, because the retired language allowed exactly one accent. That
  // constraint is gone, so secondary is whatever the generator derives, and it
  // can carry selected states and chips the way M3 intends.
  mixer[kColorZephyrusSecondary] = {dark ? ui::kColorRefSecondary80
                                         : ui::kColorRefSecondary40};
  mixer[kColorZephyrusOnSecondary] = {dark ? ui::kColorRefSecondary20
                                           : ui::kColorRefSecondary100};
  mixer[kColorZephyrusSecondaryContainer] = {dark ? ui::kColorRefSecondary30
                                                  : ui::kColorRefSecondary90};
  mixer[kColorZephyrusOnSecondaryContainer] = {dark ? ui::kColorRefSecondary90
                                                    : ui::kColorRefSecondary10};

  // A disabled title-bar glyph sits ON the group's container, not on the bar,
  // so it dims against onSecondaryContainer -- M3's 38% for disabled content.
  // Chromium's own disabled toolbar icon colour is derived from its toolbar
  // palette and reads wrong on a seeded container.
  mixer[kColorToolbarButtonIconDisabled] =
      ui::SetAlpha(kColorZephyrusOnSecondaryContainer, 0x61);

  // -- Tertiary --------------------------------------------------------------
  mixer[kColorZephyrusTertiary] = {dark ? ui::kColorRefTertiary80
                                        : ui::kColorRefTertiary40};
  mixer[kColorZephyrusOnTertiary] = {dark ? ui::kColorRefTertiary20
                                          : ui::kColorRefTertiary100};
  mixer[kColorZephyrusTertiaryContainer] = {dark ? ui::kColorRefTertiary30
                                                 : ui::kColorRefTertiary90};
  mixer[kColorZephyrusOnTertiaryContainer] = {dark ? ui::kColorRefTertiary90
                                                   : ui::kColorRefTertiary10};

  // -- Error -----------------------------------------------------------------
  //
  // OPEN ISSUE, live as of Phase 0b: our brand IS a red, so `error` and
  // `primary` now sit close together. M3 keeps error static precisely so it
  // stays recognisable, but that argument is weaker when the browser's own
  // accent is also red.
  //
  // Not worth pre-solving -- it needs to be seen. The first surface that shows
  // both is the test: a form error inside a primary-tinted dialog, or the
  // delete-workspace dialog, whose destructive button is currently pinned to
  // the palette accent rather than to `error` and should move here once we
  // know the two are distinguishable.
  mixer[kColorZephyrusError] = {dark ? ui::kColorRefError80
                                     : ui::kColorRefError40};
  mixer[kColorZephyrusOnError] = {dark ? ui::kColorRefError20
                                       : ui::kColorRefError100};
  mixer[kColorZephyrusErrorContainer] = {dark ? ui::kColorRefError30
                                              : ui::kColorRefError90};
  mixer[kColorZephyrusOnErrorContainer] = {dark ? ui::kColorRefError90
                                                : ui::kColorRefError10};

  // -- Surface and its five container levels ---------------------------------
  //
  // These tone stops are the reason this mixer reads ref ramps instead of sys
  // tokens: 4, 6, 12, 17, 22, 92, 94, 96 and 98 exist in the neutral ramp
  // specifically to serve M3's surface containers, and no sys token exposes
  // them.
  //
  // The levels are an ORDER, not a palette to pick from. Nested regions step
  // one level at a time; skipping levels throws away the hierarchy they encode.
  mixer[kColorZephyrusSurface] = {dark ? ui::kColorRefNeutral6
                                       : ui::kColorRefNeutral98};
  mixer[kColorZephyrusSurfaceContainerLowest] = {dark ? ui::kColorRefNeutral4
                                                      : ui::kColorRefNeutral100};
  mixer[kColorZephyrusSurfaceContainerLow] = {dark ? ui::kColorRefNeutral10
                                                   : ui::kColorRefNeutral96};
  mixer[kColorZephyrusSurfaceContainer] = {dark ? ui::kColorRefNeutral12
                                                : ui::kColorRefNeutral94};
  mixer[kColorZephyrusSurfaceContainerHigh] = {dark ? ui::kColorRefNeutral17
                                                    : ui::kColorRefNeutral92};
  mixer[kColorZephyrusSurfaceContainerHighest] = {dark ? ui::kColorRefNeutral22
                                                       : ui::kColorRefNeutral90};
  mixer[kColorZephyrusOnSurface] = {dark ? ui::kColorRefNeutral90
                                         : ui::kColorRefNeutral10};
  mixer[kColorZephyrusOnSurfaceVariant] = {dark ? ui::kColorRefNeutralVariant80
                                                : ui::kColorRefNeutralVariant30};

  // -- Outline ---------------------------------------------------------------
  mixer[kColorZephyrusOutline] = {dark ? ui::kColorRefNeutralVariant60
                                       : ui::kColorRefNeutralVariant50};
  mixer[kColorZephyrusOutlineVariant] = {dark ? ui::kColorRefNeutralVariant30
                                              : ui::kColorRefNeutralVariant80};

  // -- Inverse ---------------------------------------------------------------
  mixer[kColorZephyrusInverseSurface] = {dark ? ui::kColorRefNeutral90
                                              : ui::kColorRefNeutral20};
  mixer[kColorZephyrusInverseOnSurface] = {dark ? ui::kColorRefNeutral20
                                                : ui::kColorRefNeutral95};
  mixer[kColorZephyrusInversePrimary] = {dark ? ui::kColorRefPrimary40
                                              : ui::kColorRefPrimary80};

  // -- Scrim and shadow ------------------------------------------------------
  //
  // Both are pure black in both themes by spec. The opacity belongs to the
  // caller: M3 scrims are 32%, but the same role is used at other strengths.
  mixer[kColorZephyrusScrim] = {ui::kColorRefNeutral0};
  mixer[kColorZephyrusShadow] = {ui::kColorRefNeutral0};

  // -- LEGACY SHIM, NOW POINTING AT M3 ---------------------------------------
  //
  // PHASE 0b. In Phase 0 these held their pre-overhaul mappings so nothing
  // moved. They now resolve to M3 roles, which is what puts the entire browser
  // on the new scheme in one edit -- every Ground()/Ink()/Accent() call site
  // follows without being touched.
  //
  // THE TONAL STEP TURNS ON HERE, and it is worth knowing what it replaced:
  //
  //   Light, before:  Ground = neutral100, Surface = neutral100.
  //                   IDENTICAL. There was no tonal separation in light mode
  //                   at all -- the hairline was carrying all of it, which is
  //                   exactly what the retired language specified.
  //   Light, after:   Ground = neutral98, Surface = neutral94. A real step,
  //                   container darker than surface, which is M3's direction
  //                   for light themes.
  //
  //   Dark, before:   Ground = neutral25, Surface = neutral10 -- the ground was
  //                   LIGHTER than the panel raised off it. Backwards.
  //   Dark, after:    Ground = neutral6, Surface = neutral12. The panel is
  //                   lighter than the window, as a raised thing should be.
  //
  // The hairlines themselves do not disappear in this phase. `Rule` still
  // resolves, so every existing 1px divider still paints -- it just takes M3's
  // colour for the job. Removing them where a tone step now does the work is
  // per-surface editing and belongs to Phase 4.
  mixer[kColorZephyrusLegacyGround] = {kColorZephyrusSurface};
  mixer[kColorZephyrusLegacySurface] = {kColorZephyrusSurfaceContainer};

  // Narrowed to M3's actual use for it: dividers inside lists and menus. It is
  // no longer the browser's separation mechanism -- the surface containers
  // above are.
  mixer[kColorZephyrusLegacyRule] = {kColorZephyrusOutlineVariant};

  mixer[kColorZephyrusLegacyInk] = {kColorZephyrusOnSurface};
  mixer[kColorZephyrusLegacyMuted] = {kColorZephyrusOnSurfaceVariant};

  // Disabled is on-surface at 38% under M3, not a token of its own. Expressing
  // it as the real thing rather than borrowing kColorSysStateDisabled keeps it
  // correct when the scheme changes underneath.
  mixer[kColorZephyrusLegacyFaint] =
      ui::SetAlpha({kColorZephyrusOnSurface}, 0x61);

  mixer[kColorZephyrusLegacyAccent] = {kColorZephyrusPrimary};
  mixer[kColorZephyrusLegacyAccentInk] = {kColorZephyrusOnPrimary};

  // -- CHROMIUM'S SURFACES, ONTO M3 ROLES ------------------------------------
  //
  // This is the first override this mixer has ever added, and it reverses the
  // note this file used to carry ("nothing upstream reads these ids, so this
  // pass cannot change how Chromium looks"). That was right while Zephyrus had
  // its own language and Chromium's surfaces were Chromium's. It is wrong now:
  // going fully M3 means one scheme, and a token left behind is a second one.
  //
  // It is NOT a return of the old push-mixer that was deleted for making
  // Chromium's theming inert. That pushed a HARDCODED palette over the theme.
  // These roles are derived from the same ref ramps Chromium itself uses, so a
  // Customize Chrome colour still reaches every surface -- this only changes
  // which TONE of the themed ramp the toolbar lands on.
  //
  // WHY IT IS NEEDED: Phase 0b moved Zephyrus's roles to M3 and left these
  // where they were, so adjacent surfaces ended up on two different systems.
  // The sidebar (zephyrus::Surface -> surface-container, tone 94) sat against a
  // toolbar on kColorSysBase (tone 100) and the mismatch was plainly visible.
  //
  // M3 puts BOTH a toolbar and a navigation rail on surface-container, so they
  // are now the same tone by construction rather than by coincidence.
  //
  // This also preserves what the old mapping in material_chrome_color_mixer.cc
  // was protecting: that comment warns against pointing the toolbar at the same
  // token as the page card, which flattens the window. Still true, and still
  // satisfied -- the page content is `surface` (98/6) and the chrome is
  // `surface-container` (94/12), so the boundary survives at every seed colour.
  //
  // Must live HERE rather than in material_chrome_color_mixer.cc: a mixer can
  // only reference colours defined by mixers added BEFORE it, and this one runs
  // last. Defining the toolbar from ref tones over there would fork the role
  // into two sources of truth.
  mixer[kColorToolbar] = {kColorZephyrusSurfaceContainer};

  // THE FRAME PLANE, onto the same role.
  //
  // kColorFrameActive resolves to kColorSysHeader upstream -- its own token
  // family, a different tone from anything above. It paints more than the title
  // bar: BrowserView::GetZephyrusThemeColor() returns it, and that fills the
  // CONTENTS CONTAINER, which is the margin running down the left and right of
  // the page card and along the bottom. So once the toolbar and sidebar moved
  // to surface-container, that margin was the last strip still on the old
  // token, and it showed as a hairline of the wrong colour framing the card.
  //
  // The two-plane rule this file's neighbours are built around still holds, and
  // is worth restating because it is easy to read this change as breaking it:
  // the planes are CHROME and PAGE, not frame and toolbar. Chrome (frame,
  // toolbar, sidebar, panels) is surface-container; the page card and the
  // surfaces that belong to it stay on surface. 94 against 98 in light, 12
  // against 6 in dark. The flattening of 2026-09-10 came from collapsing the
  // page card into the chrome, which this does not do.
  //
  // Inactive takes the SAME value rather than a dimmed one. It was already
  // inconsistent: the frame dimmed on blur but the toolbar has no inactive
  // variant, so unfocusing a window re-opened the seam this change closes.
  // Focus is signalled by the OS shadow and the caption buttons instead.
  mixer[ui::kColorFrameActive] = {kColorZephyrusSurfaceContainer};
  mixer[ui::kColorFrameInactive] = {kColorZephyrusSurfaceContainer};
}

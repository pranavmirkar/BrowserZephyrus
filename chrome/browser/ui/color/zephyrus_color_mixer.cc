// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/zephyrus_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"

#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_recipe.h"

void AddZephyrusSysColorMixer(ui::ColorProvider* provider,
                              const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();
  const zephyrus::Palette& p = zephyrus::Current();

  // ------------------------------------------------------------------------
  // Material "sys" tokens -- this is what retunes the WebUI PAGES.
  //
  // chrome://settings, history, downloads, bookmarks, extensions and the
  // password manager are HTML, not Views. They do not read the ids above; they
  // read --color-sys-* CSS variables served by chrome://theme/colors.css, which
  // is generated from these tokens. Setting them here recolours every one of
  // those pages at once, with no CSS edited and nothing to re-apply on rebase.
  //
  // That is the whole reason to do it here rather than in each page's
  // stylesheet: the pages are upstream files, and there are hundreds of them.
  mixer[ui::kColorSysBase] = {p.ground};
  mixer[ui::kColorSysBaseContainer] = {p.surface};
  mixer[ui::kColorSysBaseContainerElevated] = {p.surface};
  mixer[ui::kColorSysSurface] = {p.surface};
  mixer[ui::kColorSysSurfaceVariant] = {p.surface};
  mixer[ui::kColorSysSurfaceSection] = {p.ground};
  mixer[ui::kColorSysHeader] = {p.ground};
  mixer[ui::kColorSysHeaderContainer] = {p.surface};

  mixer[ui::kColorSysOnSurface] = {p.ink};
  mixer[ui::kColorSysOnSurfaceSubtle] = {p.muted};
  mixer[ui::kColorSysOnHeaderPrimary] = {p.ink};

  // Inverse pair. These must stay a genuine pair -- a toast or tooltip built
  // from them draws its text with OnSurface, so setting one without the other
  // reproduces the invisible-text bug on a different surface.
  mixer[ui::kColorSysInverseSurface] = {p.ink};
  mixer[ui::kColorSysInverseOnSurface] = {p.ground};

  mixer[ui::kColorSysDivider] = {p.rule};
  mixer[ui::kColorSysNeutralOutline] = {p.rule};
  mixer[ui::kColorSysTonalOutline] = {p.rule};

  // Primary is the accent, and OnPrimary is what sits on top of it. Left
  // unpaired, every filled button in Settings would print ink-on-red.
  mixer[ui::kColorSysPrimary] = {p.accent};
  mixer[ui::kColorSysOnPrimary] = {p.accent_ink};
  mixer[ui::kColorSysStateFocusRing] = {p.accent};

  // Hover and pressed are value steps, not tints, so they work on either theme.
  mixer[ui::kColorSysStateHover] = {SkColorSetA(p.ink, 0x14)};
  mixer[ui::kColorSysStatePressed] = {SkColorSetA(p.ink, 0x24)};

  // Error keeps a red of its own. It is NOT the brand accent: a form validation
  // message and a brand button must not be the same colour, or neither reads as
  // meaning anything.
  mixer[ui::kColorSysError] = {SkColorSetRGB(0xD1, 0x3A, 0x3A)};

}

void AddZephyrusColorMixer(ui::ColorProvider* provider,
                           const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();

  const zephyrus::Palette& p = zephyrus::Current();

  // Surfaces. Menus and bubbles are the SURFACE step, not the ground: they sit
  // over the window, so they have to separate from it, and under this language
  // that separation is a flat step plus a hairline rather than a shadow.
  mixer[ui::kColorPrimaryBackground] = {p.ground};
  mixer[ui::kColorPrimaryForeground] = {p.ink};
  mixer[ui::kColorMenuBackground] = {p.surface};
  mixer[ui::kColorBubbleBackground] = {p.surface};
  mixer[ui::kColorDialogBackground] = {p.surface};
  mixer[ui::kColorTooltipBackground] = {p.surface};

  // Ink.
  mixer[ui::kColorMenuItemForeground] = {p.ink};
  mixer[ui::kColorDialogForeground] = {p.ink};
  mixer[ui::kColorTooltipForeground] = {p.ink};

  // Selection INVERTS, exactly as the sidebar's active tab does. With one
  // accent there is no colour to spend on "this row", so value carries it --
  // and a highlighted menu row is the single most common selected thing in the
  // browser, so it is worth being unambiguous.
  //
  // EVERY inverted surface has to flip its ink too. Setting only the background
  // leaves the label at the normal foreground, which is now the same colour as
  // the fill behind it -- the row highlights and the text vanishes. That is not
  // hypothetical: it shipped in the sidebar first, was fixed there with
  // active_fg, and then reappeared here because the mixer was written without
  // carrying the lesson across. Any *BackgroundSelected assignment below needs
  // a matching *ForegroundSelected.
  mixer[ui::kColorMenuItemBackgroundSelected] = {p.ink};
  mixer[ui::kColorMenuItemForegroundSelected] = {p.ground};

  // Dropdowns (autofill, the omnibox popup's cousins) invert on the same rule.
  mixer[ui::kColorDropdownBackground] = {p.surface};
  mixer[ui::kColorDropdownForeground] = {p.ink};
  mixer[ui::kColorDropdownBackgroundSelected] = {p.ink};
  mixer[ui::kColorDropdownForegroundSelected] = {p.ground};

  // Tables and trees (task manager, history, certificate views) do the same,
  // and carry a focused/unfocused pair each.
  mixer[ui::kColorTableBackground] = {p.surface};
  mixer[ui::kColorTableForeground] = {p.ink};
  mixer[ui::kColorTableBackgroundSelectedFocused] = {p.ink};
  mixer[ui::kColorTableForegroundSelectedFocused] = {p.ground};
  mixer[ui::kColorTableBackgroundSelectedUnfocused] = {p.ink};
  mixer[ui::kColorTableForegroundSelectedUnfocused] = {p.ground};
  mixer[ui::kColorTreeBackground] = {p.surface};
  mixer[ui::kColorTreeNodeForeground] = {p.ink};
  mixer[ui::kColorTreeNodeBackgroundSelectedFocused] = {p.ink};
  mixer[ui::kColorTreeNodeForegroundSelectedFocused] = {p.ground};
  mixer[ui::kColorTreeNodeBackgroundSelectedUnfocused] = {p.ink};
  mixer[ui::kColorTreeNodeForegroundSelectedUnfocused] = {p.ground};

  // Rules, not shadows.
    mixer[ui::kColorMenuSeparator] = {p.rule};
  mixer[ui::kColorSeparator] = {p.rule};
  mixer[ui::kColorMenuBorder] = {p.rule};

  // Text fields follow the ground so an input reads as a hole in the surface
  // rather than another raised card.
  mixer[ui::kColorTextfieldBackground] = {p.ground};
  mixer[ui::kColorTextfieldForeground] = {p.ink};

  // ------------------------------------------------------------------------
  // Chrome ids that DERIVE from the accent, overridden directly.
  //
  // These are declared as `{ui::kColorSysPrimary}` in material_chrome_color_mixer
  // and pinned again here so they cannot be re-derived.
  //
  // HISTORICAL NOTE, because the original comment here recorded a wrong
  // diagnosis: this list was added while chasing a blue "By date" tab in
  // chrome://history, on the theory that mixer-chain ordering was not carrying
  // the accent through. That was NOT the cause. The tab is a WebUI component
  // whose CSS falls back to a hardcoded var(--google-blue-300) when its colour
  // token is undefined, so no ColorProvider value could ever have reached it;
  // the real fix was retuning the blue ramp in cr_shared_vars.css.
  //
  // The pins are kept because they are correct and cheap for the Views-side
  // consumers, not because they fixed that bug. Do not cite them as evidence
  // about how the mixer chain resolves.
  mixer[kColorTabNavItemSelected] = {p.accent};
  mixer[kColorNavMenuItemSelected] = {p.accent};
  mixer[kColorTabSearchSelected] = {p.accent};
  mixer[kColorDownloadManagerProgress] = {p.accent};
  mixer[kColorDownloadBubblePrimaryIcon] = {p.accent};
  mixer[kColorExtensionManagerHighlightText] = {p.accent};
  mixer[kColorPageInfoPermissionUsedIcon] = {p.accent};
  mixer[kColorBookmarkManagerItemOutline] = {p.accent};
  mixer[kColorCastDialogHelpIcon] = {p.accent};
  mixer[kColorSliderActive] = {p.accent};
  mixer[kColorSliderActiveText] = {p.accent_ink};
  mixer[kColorFeaturePromoBubbleBackground] = {p.accent};
  mixer[kColorFeaturePromoBubbleForeground] = {p.accent_ink};

  // The focus ring is the one place the accent appears constantly, and that is
  // deliberate: keyboard focus is a live "you are here" state, which is what
  // the single red is reserved for. It is also the accent's most
  // accessibility-critical use, so it stays the accent rather than the ink.
  mixer[ui::kColorFocusableBorderFocused] = {p.accent};
}

// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/material_tab_strip_color_mixer.h"

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/color/chrome_color_provider_utils.h"
#include "ui/color/color_id.h"
#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"

namespace {

// Zephyrus (from Helium): an inactive tab is invisible until hovered, and the
// hover is a partial draw of what the tab will look like once it is active --
// not a separate grey. 45% is Helium's value.
constexpr SkAlpha kTabInactiveHoverAlpha = 0.45 * 255;

}  // namespace

void AddMaterialTabStripColorMixer(ui::ColorProvider* provider,
                                   const ui::ColorProviderKey& key) {
  if (!ShouldApplyChromeMaterialOverrides(key)) {
    return;
  }

  const bool dark_mode =
      key.color_mode == ui::ColorProviderKey::ColorMode::kDark;

  // TODO(crbug.com/40883407): Validate final mappings for ChromeRefresh23
  // color.
  ui::ColorMixer& mixer = provider->AddMixer();
  mixer[kColorDetachedTabBackgroundActiveFrameActive] = {
      dark_mode ? ui::kColorSysSurfaceVariant : ui::kColorSysBase};
  // Zephyrus (from Helium): the active tab IS the omnibox pill.
  //
  // Tying them to one token means the two largest shapes in the chrome move
  // together under any theme, instead of drifting apart as kColorSysBase and
  // kColorSysOmniboxContainer take a seed colour differently.
  mixer[kColorTabBackgroundActiveFrameActive] = {kColorLocationBarBackground};
  mixer[kColorTabBackgroundActiveFrameInactive] = {kColorLocationBarBackground};

  // Inactive tabs draw NOTHING -- the frame shows through. Under a themed
  // palette a per-tab background is a second competing plane behind the strip;
  // absence reads cleaner and costs nothing to theme.
  mixer[kColorTabBackgroundInactiveFrameActive] =
      ui::SetAlpha(kColorTabBackgroundActiveFrameActive, SK_AlphaTRANSPARENT);
  mixer[kColorTabBackgroundInactiveFrameInactive] =
      ui::SetAlpha(kColorTabBackgroundActiveFrameInactive, SK_AlphaTRANSPARENT);
  mixer[kColorTabBackgroundInactiveHoverFrameActive] = {ui::SetAlpha(
      kColorTabBackgroundActiveFrameActive, kTabInactiveHoverAlpha)};
  mixer[kColorTabStripComboButtonSeparator] = {ui::kColorSysDivider};
  mixer[kColorTabStripControlButtonInkDrop] = {ui::kColorSysStateHeaderHover};
  mixer[kColorTabStripControlButtonInkDropRipple] = {
      ui::kColorSysStateRippleNeutralOnSubtle};

  // TODO(tbergquist): Use kColorSysStateHeaderHoverInactive, once it exists.
  mixer[kColorTabBackgroundInactiveHoverFrameInactive] = {ui::SetAlpha(
      kColorTabBackgroundActiveFrameInactive, kTabInactiveHoverAlpha)};

  // A selected tab is an active tab. The upstream recipes blended a selection
  // state over the INACTIVE background, which is now transparent -- so they
  // would have resolved against nothing.
  mixer[kColorTabBackgroundSelectedFrameActive] = {kColorLocationBarBackground};
  mixer[kColorTabBackgroundSelectedFrameInactive] = {
      kColorLocationBarBackground};
  mixer[kColorTabBackgroundSelectedHoverFrameActive] = {
      kColorLocationBarBackground};
  mixer[kColorTabBackgroundSelectedHoverFrameInactive] = {
      kColorLocationBarBackground};
#if !BUILDFLAG(IS_ANDROID)
  mixer[kColorTabDiscardRingFrameActive] = {ui::kColorSysStateInactiveRing};
  mixer[kColorTabDiscardRingFrameInactive] = {kColorTabDiscardRingFrameActive};
#endif
  mixer[kColorTabForegroundActiveFrameActive] = {ui::kColorSysOnSurface};
  mixer[kColorTabForegroundActiveFrameInactive] = {
      kColorTabForegroundActiveFrameActive};
  mixer[kColorTabForegroundInactiveFrameActive] =
      ui::BlendForMinContrast({ui::kColorSysOnSurfaceSecondary},
                              {kColorTabBackgroundInactiveFrameActive});
  mixer[kColorTabForegroundInactiveFrameInactive] =
      ui::BlendForMinContrast({kColorTabForegroundInactiveFrameActive},
                              {kColorTabBackgroundInactiveFrameInactive});

  // TabDivider colors.
  mixer[kColorTabDividerFrameActive] = {ui::kColorSysOnHeaderDivider};
  mixer[kColorTabDividerFrameInactive] = {ui::kColorSysOnHeaderDividerInactive};

  // Tabstrip Control Button colors.
  mixer[kColorNewTabButtonCRForegroundFrameActive] = {
      ui::kColorSysOnSurfaceSubtle};
  mixer[kColorNewTabButtonCRForegroundFrameInactive] = {
      ui::kColorSysOnSurfaceSubtle};
  mixer[kColorNewTabButtonCRBackgroundFrameActive] = {
      ui::kColorSysHeaderContainer};
  mixer[kColorNewTabButtonCRBackgroundFrameInactive] = {
      ui::kColorSysHeaderContainerInactive};

  mixer[kColorTabSearchButtonCRForegroundFrameActive] = {
      ui::kColorSysOnSurfacePrimary};
  mixer[kColorTabSearchButtonCRForegroundFrameInactive] = {
      ui::kColorSysOnSurfacePrimaryInactive};
}

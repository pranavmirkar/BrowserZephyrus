// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/color/zephyrus_color_mixer.h"

#include "ui/color/color_mixer.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_recipe.h"

void AddZephyrusColorMixer(ui::ColorProvider* provider,
                           const ui::ColorProviderKey& key) {
  ui::ColorMixer& mixer = provider->AddMixer();

  // Each Zephyrus name is one Chromium token. These are references, not
  // values: the recipe resolves against the chain below this mixer, so
  // whatever the theme, the native mixer and any custom colour finally settled
  // on is what a Zephyrus view gets.
  //
  // The mapping is the exact inverse of what the old push-mixer wrote, so the
  // default look is unchanged on a default theme and the whole difference is
  // that a NON-default theme now arrives.

  // Ground is the window's own colour; surface is a panel raised off it.
  mixer[kColorZephyrusGround] = {ui::kColorSysBase};
  mixer[kColorZephyrusSurface] = {ui::kColorSysSurface};

  // Separation is a line. Divider is the token Chromium uses for exactly that,
  // and it already carries the theme's idea of how strong a hairline should be
  // against its own background.
  mixer[kColorZephyrusRule] = {ui::kColorSysDivider};

  // Ink and its quieter steps. OnSurface is paired with Surface upstream, so
  // taking both from that pair is what keeps text legible on a theme nobody
  // here has seen -- rather than pairing a themed background with our old
  // hardcoded black.
  mixer[kColorZephyrusInk] = {ui::kColorSysOnSurface};
  mixer[kColorZephyrusMuted] = {ui::kColorSysOnSurfaceSubtle};
  mixer[kColorZephyrusFaint] = {ui::kColorSysStateDisabled};

  // The accent, and what is legible ON it.
  //
  // Kept as a PAIR for the reason the old mixer learned the hard way: setting a
  // fill without its matching foreground prints ink-on-accent and the label
  // disappears. Primary/OnPrimary is that pair upstream, and it stays a pair
  // through a theme change, which our hardcoded accent_ink could not.
  mixer[kColorZephyrusAccent] = {ui::kColorSysPrimary};
  mixer[kColorZephyrusAccentInk] = {ui::kColorSysOnPrimary};
}

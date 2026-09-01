// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_
#define CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_

#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"

// Pushes the Zephyrus palette into Chromium's own colour system.
//
// WHY THIS EXISTS
// ---------------
// Zephyrus-owned views read zephyrus::Ground()/Ink()/etc. directly. Everything
// upstream -- context menus, the app menu, tooltips, the find bar, permission
// and download bubbles, autofill popups, dialogs -- does not: those read
// ui::ColorProvider. Converting them one file at a time would mean patching
// dozens of upstream files and re-patching every one of them on each rebase.
//
// Doing it here retunes them all at once and touches a single upstream file
// (chrome_color_mixers.cc), which is the difference between a rebase that
// conflicts in one place and a rebase that conflicts everywhere.
//
// ORDER MATTERS, and it is why this is two functions.
//
// Chromium's mixers form a chain, and a recipe like
// `mixer[kColorTabNavItemSelected] = {ui::kColorSysPrimary}` resolves that
// reference against the chain below the mixer that declared it. Setting the
// Material sys tokens FIRST, before Chrome's own mixers run, means all ~61
// downstream derivations compute from our palette rather than Chromium's.
//
// (An earlier version of this comment claimed the split was what fixed the blue
// tab in chrome://history. It was not -- that was a hardcoded CSS fallback in
// cr_shared_vars.css. The ordering here is still the right structure, but it
// was not the cure for that bug.)
void AddZephyrusSysColorMixer(ui::ColorProvider* provider,
                              const ui::ColorProviderKey& key);

// ...and the direct Views overrides (menus, dialogs, tooltips, selection) go
// LAST, after the native mixer and any custom theme, which would otherwise
// overwrite them.
void AddZephyrusColorMixer(ui::ColorProvider* provider,
                           const ui::ColorProviderKey& key);

#endif  // CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_

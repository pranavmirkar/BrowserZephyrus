// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_
#define CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_

#include "chrome/browser/ui/color/chrome_color_id.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"

// The Zephyrus palette, as names that RESOLVE AGAINST CHROMIUM'S THEME.
//
// THIS USED TO POINT THE OTHER WAY, and the reversal is the whole change.
// The old mixer took a hardcoded monochrome palette and pushed it onto
// Chromium's Material tokens, so every upstream surface -- context menus, the
// app menu, tooltips, dialogs, autofill, tables, trees, and every WebUI page
// that reads --color-sys-* -- came out in our two colours no matter what the
// browser theme said. It worked, and it made Chromium's own theming inert:
// picking a colour in Customize Chrome changed nothing, because our pass ran
// last and overwrote it.
//
// Now the palette is a VIEW ONTO the theme rather than a replacement for it.
// The ids below are defined in terms of Chromium's sys tokens, so a colour
// chosen on the New Tab Page reaches Zephyrus's own sidebar, popups and search
// overlay by the same route it reaches everything else.
//
// The vocabulary survives the change deliberately. Zephyrus views ask for
// "ground" and "ink" and "rule", not for kColorSysBase and kColorSysOnSurface,
// because those names carry the design language's rules -- separation is a
// hairline, not a shadow; there is one accent and it means "now". Keeping the
// names keeps one place to change if the mapping is ever wrong.
//
// Ids live here rather than in chrome_color_id.h so this costs zero upstream
// lines: they start at kChromeColorsEnd, which is what that marker is for.
enum ZephyrusColorIds : ui::ColorId {
  kColorZephyrusGround = kChromeColorsEnd,
  kColorZephyrusSurface,
  kColorZephyrusRule,
  kColorZephyrusInk,
  kColorZephyrusMuted,
  kColorZephyrusFaint,
  kColorZephyrusAccent,
  kColorZephyrusAccentInk,
  kZephyrusColorsEnd,
};

// Defines the ids above from Chromium's own tokens.
//
// Runs LAST, so the tokens it reads are the finished ones -- after the native
// mixer and after any custom theme. It adds no override of its own: nothing
// upstream reads these ids, so this pass cannot change how Chromium looks.
// That is the point. Chromium's surfaces are Chromium's again.
void AddZephyrusColorMixer(ui::ColorProvider* provider,
                           const ui::ColorProviderKey& key);

#endif  // CHROME_BROWSER_UI_COLOR_ZEPHYRUS_COLOR_MIXER_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_ZEPHYRUS_SETUP_MASCOT_H_
#define CHROME_INSTALLER_ZEPHYRUS_SETUP_MASCOT_H_

#include <stdint.h>

#include <vector>

namespace zephyrus_setup {

// The Zephyrus mascot, drawn natively.
//
// WHY THIS IS NOT A WEBVIEW
// -------------------------
// The mascot was authored as animated SVG/CSS. Rendering it faithfully would
// normally mean hosting WebView2 -- but an installer is the one program that
// has to run on a machine you know nothing about, and "install failed: runtime
// missing" is the worst possible first impression a browser can make.
//
// It turns out not to cost anything: the entire mascot is 167 axis-aligned
// <rect> elements. No paths, no curves, no bitmaps. So the whole character is a
// table of rectangles and a handful of interpolators -- no SVG engine, no
// dependency, and pixel-crisp at any DPI, which matters more for pixel art than
// for anything else.
//
// Coordinates below are in the source artwork's 160x150 viewBox and are scaled
// at draw time.

// Palette, taken from the mascot artwork rather than re-picked, so the
// installer and the in-browser companion cannot drift apart.
inline constexpr uint32_t kBlue = 0x4A80C4;
inline constexpr uint32_t kBlueLight = 0xB5D4F4;
inline constexpr uint32_t kNavy = 0x1B2A4A;
inline constexpr uint32_t kRed = 0xE0554F;
inline constexpr uint32_t kWhite = 0xFFFFFF;
// Zephyrus window background. The product's single fixed colour.
inline constexpr uint32_t kBackground = 0x0E1123;

struct Rect {
  float x;
  float y;
  float w;
  float h;
  uint32_t color;
};

// What the mascot is doing. Each maps to an install phase; the failure state is
// deliberately part of the set rather than an afterthought, because a failed
// install is exactly when a mascot has to say something useful.
enum class State {
  kIdle,      // waiting to be told to start -- breathing, blinking
  kWorking,   // extracting  -- hands hammer, sparks
  kCarrying,  // installing  -- walks with the payload overhead
  kThinking,  // finishing   -- rocks, thought dots pulse
  kHappy,     // done        -- hops, sparkles, eyes closed
  kAlert,     // failed      -- furrowed brow, shake, red badge
};

// Rectangles to draw for `state` at animation time `t` (seconds since the state
// began). Returned in painter's order.
std::vector<Rect> BuildMascot(State state, double t);

}  // namespace zephyrus_setup

#endif  // CHROME_INSTALLER_ZEPHYRUS_SETUP_MASCOT_H_

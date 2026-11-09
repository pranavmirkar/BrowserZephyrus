// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AURORA_ZEPHYRUS_AURORA_PALETTE_H_
#define CHROME_BROWSER_ZEPHYRUS_AURORA_ZEPHYRUS_AURORA_PALETTE_H_

#include <array>
#include <cstddef>
#include <vector>

#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"

namespace zephyrus::aurora {

// How many curtains the aurora draws, and so how many colours it needs.
inline constexpr size_t kSlots = 3;

// The most vivid, mutually distinct colours in `bitmap`, strongest first, at
// most `max_colors`. `bitmap` is a small picture of the page (a few dozen pixels
// across is plenty). Empty when the picture is essentially grey, white or black:
// a plain page has no colour to lend, and inventing one would be a lie.
std::vector<SkColor> ExtractVividColors(const SkBitmap& bitmap,
                                        size_t max_colors = kSlots);

// Pulls `color` into the range an aurora can use: saturated enough to read as a
// colour and neither so dark that it vanishes nor so light that it washes out.
// The result is opaque; the aurora sets its own opacity.
SkColor ToAuroraTone(SkColor color);

// One colour per curtain. `vivid` is what ExtractVividColors found; `fallback`
// stands in when it found nothing. One colour is spread into near neighbours on
// the colour wheel, which is what a real aurora does, so a page with a single
// brand colour still gets a ribbon with some depth. Every slot is toned.
std::array<SkColor, kSlots> FillSlots(const std::vector<SkColor>& vivid,
                                      SkColor fallback);

}  // namespace zephyrus::aurora

#endif  // CHROME_BROWSER_ZEPHYRUS_AURORA_ZEPHYRUS_AURORA_PALETTE_H_

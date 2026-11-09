// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/aurora/zephyrus_aurora_palette.h"

#include <algorithm>

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/color_utils.h"

namespace zephyrus::aurora {

namespace {

SkBitmap Solid(int w, int h, SkColor c) {
  SkBitmap b;
  b.allocN32Pixels(w, h);
  b.eraseColor(c);
  return b;
}

void Fill(SkBitmap& bitmap, int x0, int x1, SkColor c) {
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = x0; x < x1; ++x) {
      *bitmap.getAddr32(x, y) = c;
    }
  }
}

double HueOf(SkColor c) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(c, &hsl);
  return hsl.h;
}

// Distance of a hue from red, either way round the wheel.
double FromRed(SkColor c) {
  const double h = HueOf(c);
  return std::min(h, 1.0 - h);
}

}  // namespace

TEST(ZephyrusAuroraPaletteTest, PlainPagesHaveNoColour) {
  EXPECT_TRUE(ExtractVividColors(Solid(32, 20, SK_ColorWHITE)).empty());
  EXPECT_TRUE(ExtractVividColors(Solid(32, 20, SK_ColorBLACK)).empty());
  EXPECT_TRUE(
      ExtractVividColors(Solid(32, 20, SkColorSetRGB(0x1e, 0x1e, 0x1e)))
          .empty());
  // Off-white and beige are not colours either.
  EXPECT_TRUE(
      ExtractVividColors(Solid(32, 20, SkColorSetRGB(0xf5, 0xf0, 0xe6)))
          .empty());
}

TEST(ZephyrusAuroraPaletteTest, NullAndEmptyBitmapsAreSafe) {
  EXPECT_TRUE(ExtractVividColors(SkBitmap()).empty());
  EXPECT_TRUE(ExtractVividColors(Solid(32, 20, SK_ColorRED), 0).empty());
}

TEST(ZephyrusAuroraPaletteTest, OneColourPageGivesThatColour) {
  const auto found =
      ExtractVividColors(Solid(32, 20, SkColorSetRGB(0x1a, 0xa8, 0x4a)));
  ASSERT_EQ(1u, found.size());
  // The colour that went in, give or take the bucket averaging.
  EXPECT_NEAR(HueOf(SkColorSetRGB(0x1a, 0xa8, 0x4a)), HueOf(found[0]), 0.02);
}

TEST(ZephyrusAuroraPaletteTest, StrongestColourComesFirst) {
  // 70% blue, 30% red.
  SkBitmap b = Solid(40, 20, SkColorSetRGB(0xe0, 0x20, 0x20));
  Fill(b, 0, 28, SkColorSetRGB(0x20, 0x40, 0xe0));
  const auto found = ExtractVividColors(b);
  ASSERT_EQ(2u, found.size());
  EXPECT_NEAR(0.65, HueOf(found[0]), 0.06);  // blue
  EXPECT_NEAR(0.0, FromRed(found[1]), 0.06);
}

TEST(ZephyrusAuroraPaletteTest, ASmallAccentDoesNotColourAWhitePage) {
  // A 1% red button on white.
  SkBitmap b = Solid(100, 100, SK_ColorWHITE);
  for (int y = 0; y < 10; ++y) {
    for (int x = 0; x < 10; ++x) {
      *b.getAddr32(x, y) = SK_ColorRED;
    }
  }
  EXPECT_TRUE(ExtractVividColors(b).empty());
}

TEST(ZephyrusAuroraPaletteTest, NeverMoreThanAskedFor) {
  SkBitmap b = Solid(60, 10, SK_ColorRED);
  Fill(b, 20, 40, SK_ColorGREEN);
  Fill(b, 40, 60, SK_ColorBLUE);
  EXPECT_EQ(3u, ExtractVividColors(b, 3).size());
  EXPECT_EQ(2u, ExtractVividColors(b, 2).size());
  EXPECT_EQ(1u, ExtractVividColors(b, 1).size());
}

TEST(ZephyrusAuroraPaletteTest, ToneKeepsAColourInRange) {
  for (SkColor c :
       {SK_ColorWHITE, SK_ColorBLACK, SkColorSetRGB(0x10, 0x10, 0x30),
        SkColorSetRGB(0xff, 0xe0, 0xe0), SK_ColorRED}) {
    const SkColor t = ToAuroraTone(c);
    EXPECT_EQ(255u, SkColorGetA(t));
    color_utils::HSL hsl;
    color_utils::SkColorToHSL(t, &hsl);
    EXPECT_GE(hsl.s, 0.54);
    EXPECT_LE(hsl.s, 0.91);
    EXPECT_GE(hsl.l, 0.49);
    EXPECT_LE(hsl.l, 0.65);
  }
}

TEST(ZephyrusAuroraPaletteTest, SlotsAreAlwaysFilledAndToned) {
  const SkColor fallback = SkColorSetRGB(0xc0, 0x30, 0x50);
  for (size_t n = 0; n <= 3; ++n) {
    std::vector<SkColor> vivid;
    for (size_t i = 0; i < n; ++i) {
      vivid.push_back(SkColorSetRGB(0x30 * (i + 1), 0x90, 0xf0 - 0x30 * i));
    }
    for (SkColor slot : FillSlots(vivid, fallback)) {
      EXPECT_EQ(255u, SkColorGetA(slot)) << n;
      color_utils::HSL hsl;
      color_utils::SkColorToHSL(slot, &hsl);
      EXPECT_GE(hsl.s, 0.54) << n;
      EXPECT_GE(hsl.l, 0.49) << n;
      EXPECT_LE(hsl.l, 0.65) << n;
    }
  }
}

TEST(ZephyrusAuroraPaletteTest, OneColourBecomesThreeNeighbours) {
  const auto slots = FillSlots({SkColorSetRGB(0x20, 0xb0, 0x40)}, SK_ColorRED);
  EXPECT_NEAR(HueOf(slots[0]) + 1.0 / 12.0, HueOf(slots[1]), 0.03);
  EXPECT_NEAR(HueOf(slots[0]) - 1.0 / 12.0, HueOf(slots[2]), 0.03);
}

TEST(ZephyrusAuroraPaletteTest, NoColourUsesTheFallbackFamily) {
  const SkColor fallback = SkColorSetRGB(0x30, 0x60, 0xe0);
  const auto slots = FillSlots({}, fallback);
  EXPECT_NEAR(HueOf(ToAuroraTone(fallback)), HueOf(slots[0]), 0.02);
}

}  // namespace zephyrus::aurora

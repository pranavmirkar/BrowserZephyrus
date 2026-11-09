// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/aurora/zephyrus_aurora_palette.h"

#include <algorithm>
#include <cmath>

#include "ui/gfx/color_utils.h"

namespace zephyrus::aurora {

namespace {

constexpr int kHueBuckets = 24;

// A pixel needs this much chroma to count at all. Below it the pixel is grey to
// the eye however the channels happen to differ, and counting it would let a
// page full of off-white text-on-white vote for "beige".
constexpr double kMinChroma = 0.14;

// A bucket must hold at least this fraction of the picture, in fully-vivid
// pixels, to be reported. 2% keeps a single red button from colouring a page
// that is otherwise white, and still lets a banner across the top count.
constexpr double kMinShare = 0.02;

// Colours closer than this many buckets (of 24) on the wheel are one colour.
constexpr int kSuppressRadius = 3;

int WrapBucket(int b) {
  return ((b % kHueBuckets) + kHueBuckets) % kHueBuckets;
}

double Clamp(double v, double lo, double hi) {
  return std::min(hi, std::max(lo, v));
}

SkColor RotateHue(SkColor color, double turns) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  hsl.h = hsl.h + turns;
  hsl.h -= std::floor(hsl.h);
  return color_utils::HSLToSkColor(hsl, 255);
}

SkColor Mix(SkColor a, SkColor b) {
  return SkColorSetRGB((SkColorGetR(a) + SkColorGetR(b)) / 2,
                       (SkColorGetG(a) + SkColorGetG(b)) / 2,
                       (SkColorGetB(a) + SkColorGetB(b)) / 2);
}

}  // namespace

std::vector<SkColor> ExtractVividColors(const SkBitmap& bitmap,
                                        size_t max_colors) {
  std::vector<SkColor> result;
  const int width = bitmap.width();
  const int height = bitmap.height();
  if (bitmap.isNull() || width <= 0 || height <= 0 || max_colors == 0) {
    return result;
  }

  std::array<double, kHueBuckets> weight{};
  std::array<double, kHueBuckets> sum_r{};
  std::array<double, kHueBuckets> sum_g{};
  std::array<double, kHueBuckets> sum_b{};
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const SkColor c = bitmap.getColor(x, y);
      if (SkColorGetA(c) < 128) {
        continue;
      }
      color_utils::HSL hsl;
      color_utils::SkColorToHSL(c, &hsl);
      // Chroma: saturation, discounted toward black and white where saturation
      // stops meaning anything.
      const double chroma = hsl.s * (1.0 - std::abs(2.0 * hsl.l - 1.0));
      if (chroma < kMinChroma) {
        continue;
      }
      const int bucket = WrapBucket(static_cast<int>(hsl.h * kHueBuckets));
      weight[bucket] += chroma;
      sum_r[bucket] += chroma * SkColorGetR(c);
      sum_g[bucket] += chroma * SkColorGetG(c);
      sum_b[bucket] += chroma * SkColorGetB(c);
    }
  }

  // A bucket borrows half of each neighbour, so one hue that straddles a bucket
  // edge is still one peak.
  std::array<double, kHueBuckets> smooth{};
  for (int b = 0; b < kHueBuckets; ++b) {
    smooth[b] = weight[b] + 0.5 * (weight[WrapBucket(b - 1)] +
                                   weight[WrapBucket(b + 1)]);
  }

  const double threshold = kMinShare * width * height;
  while (result.size() < max_colors) {
    int best = 0;
    for (int b = 1; b < kHueBuckets; ++b) {
      if (smooth[b] > smooth[best]) {
        best = b;
      }
    }
    if (smooth[best] < threshold) {
      break;
    }
    double w = 0, r = 0, g = 0, bl = 0;
    for (int d = -1; d <= 1; ++d) {
      const int b = WrapBucket(best + d);
      w += weight[b];
      r += sum_r[b];
      g += sum_g[b];
      bl += sum_b[b];
    }
    if (w <= 0) {
      break;
    }
    result.push_back(SkColorSetRGB(static_cast<uint8_t>(std::lround(r / w)),
                                   static_cast<uint8_t>(std::lround(g / w)),
                                   static_cast<uint8_t>(std::lround(bl / w))));
    for (int d = -kSuppressRadius; d <= kSuppressRadius; ++d) {
      smooth[WrapBucket(best + d)] = 0;
    }
  }
  return result;
}

SkColor ToAuroraTone(SkColor color) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(color, &hsl);
  hsl.s = Clamp(hsl.s, 0.55, 0.90);
  hsl.l = Clamp(hsl.l, 0.50, 0.64);
  return color_utils::HSLToSkColor(hsl, 255);
}

std::array<SkColor, kSlots> FillSlots(const std::vector<SkColor>& vivid,
                                      SkColor fallback) {
  // Two neighbours a twelfth of the wheel either side (30 degrees): close enough
  // to read as one family, far enough to be seen as separate ribbons.
  constexpr double kNeighbour = 1.0 / 12.0;
  std::array<SkColor, kSlots> slots;
  switch (vivid.size()) {
    case 0:
      slots = {fallback, RotateHue(fallback, kNeighbour),
               RotateHue(fallback, -kNeighbour)};
      break;
    case 1:
      slots = {vivid[0], RotateHue(vivid[0], kNeighbour),
               RotateHue(vivid[0], -kNeighbour)};
      break;
    case 2:
      slots = {vivid[0], Mix(vivid[0], vivid[1]), vivid[1]};
      break;
    default:
      slots = {vivid[0], vivid[1], vivid[2]};
      break;
  }
  for (SkColor& slot : slots) {
    slot = ToAuroraTone(slot);
  }
  return slots;
}

}  // namespace zephyrus::aurora

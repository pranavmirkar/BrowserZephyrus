// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_onboarding_sky.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkBlurTypes.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMaskFilter.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/effects/SkGradient.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/scoped_canvas.h"

namespace zephyrus {

namespace {

using agent::SkyTime;

constexpr int kVariants = 3;
// Clouds per layer, far to near.
constexpr std::array<int, OnboardingSky::kLayers> kCount = {6, 5, 3};
// Width of a cloud as a fraction of the window's width.
constexpr std::array<double, OnboardingSky::kLayers> kWidth = {0.16, 0.26, 0.40};
// Drift, in window widths per second: a near cloud crosses in about a minute.
constexpr std::array<double, OnboardingSky::kLayers> kSpeed = {0.0045, 0.009,
                                                               0.017};
// How opaque each layer's cloud bodies are: far ones are hazier.
constexpr std::array<float, OnboardingSky::kLayers> kBody = {0.55f, 0.82f, 0.96f};
// Where each layer floats, top and bottom of its band, as fractions of height.
constexpr std::array<double, OnboardingSky::kLayers> kBandTop = {0.05, 0.22, 0.55};
constexpr std::array<double, OnboardingSky::kLayers> kBandBottom = {0.32, 0.55,
                                                                    0.82};
// How far each layer slides with the pointer, as a fraction of the window's
// width at full deflection. Near layers move more: that is the depth.
constexpr std::array<double, OnboardingSky::kLayers> kParallax = {0.006, 0.014,
                                                                  0.028};

// One time of day: the sky's colours top to bottom, a glow (the sun or the
// moon), and how the clouds are lit.
struct Palette {
  std::array<SkColor, 5> sky;
  std::array<float, 5> stops;
  float glow_x, glow_y, glow_radius;  // fractions of the window
  SkColor4f glow;
  SkColor cloud;      // the lit body
  SkColor underside;  // the shade beneath
  float cloud_alpha;  // multiplies each layer's body opacity
  bool stars;
  bool moon;
};

const Palette& PaletteFor(SkyTime time) {
  static const Palette kDawn = {
      {SkColorSetRGB(0x2F, 0x3E, 0x7A), SkColorSetRGB(0x6D, 0x6F, 0xB0),
       SkColorSetRGB(0xE3, 0x9C, 0xAE), SkColorSetRGB(0xFF, 0xC9, 0x9E),
       SkColorSetRGB(0xFF, 0xEE, 0xD6)},
      {0.0f, 0.3f, 0.62f, 0.85f, 1.0f},
      0.2f, 0.92f, 0.6f, {1.0f, 0.86f, 0.7f, 0.55f},
      SkColorSetRGB(0xFF, 0xF3, 0xEC), SkColorSetRGB(0xD9, 0xA7, 0xBA), 0.95f,
      false, false};
  static const Palette kDay = {
      {SkColorSetRGB(0x1F, 0x5F, 0xD6), SkColorSetRGB(0x3C, 0x86, 0xEA),
       SkColorSetRGB(0x7D, 0xB8, 0xF7), SkColorSetRGB(0xC9, 0xE6, 0xFF),
       SkColorSetRGB(0xEE, 0xF7, 0xFF)},
      {0.0f, 0.32f, 0.62f, 0.86f, 1.0f},
      0.84f, 0.06f, 0.55f, {1.0f, 0.98f, 0.92f, 0.55f},
      SK_ColorWHITE, SkColorSetRGB(0xB9, 0xCF, 0xE8), 1.0f, false, false};
  static const Palette kSunset = {
      {SkColorSetRGB(0x26, 0x24, 0x63), SkColorSetRGB(0x5E, 0x3A, 0x8A),
       SkColorSetRGB(0xC9, 0x5B, 0x86), SkColorSetRGB(0xFF, 0x95, 0x5E),
       SkColorSetRGB(0xFF, 0xD3, 0x8A)},
      {0.0f, 0.3f, 0.58f, 0.82f, 1.0f},
      0.74f, 0.9f, 0.62f, {1.0f, 0.72f, 0.38f, 0.7f},
      SkColorSetRGB(0xFF, 0xE0, 0xCF), SkColorSetRGB(0xA8, 0x62, 0x8C), 0.92f,
      false, false};
  static const Palette kNight = {
      {SkColorSetRGB(0x04, 0x09, 0x1C), SkColorSetRGB(0x0A, 0x16, 0x34),
       SkColorSetRGB(0x12, 0x27, 0x50), SkColorSetRGB(0x1C, 0x39, 0x6A),
       SkColorSetRGB(0x26, 0x47, 0x7B)},
      {0.0f, 0.3f, 0.6f, 0.85f, 1.0f},
      0.8f, 0.16f, 0.32f, {0.78f, 0.86f, 1.0f, 0.35f},
      SkColorSetRGB(0x51, 0x65, 0x96), SkColorSetRGB(0x1A, 0x25, 0x46), 0.62f,
      true, true};
  switch (time) {
    case SkyTime::kDawn:
      return kDawn;
    case SkyTime::kDay:
      return kDay;
    case SkyTime::kSunset:
      return kSunset;
    case SkyTime::kNight:
      return kNight;
  }
  return kDay;
}

// mulberry32: small, fast, and the same everywhere, so a cloud layout is a seed.
class Random {
 public:
  explicit Random(uint32_t seed) : state_(seed) {}
  double Next() {
    uint32_t t = (state_ += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
  double In(double low, double high) { return low + (high - low) * Next(); }

 private:
  uint32_t state_;
};

sk_sp<SkShader> VerticalGradient(float height, const Palette& palette) {
  std::vector<SkColor4f> colors;
  for (SkColor c : palette.sky) {
    colors.push_back(SkColor4f::FromColor(c));
  }
  std::vector<float> stops(palette.stops.begin(), palette.stops.end());
  const SkPoint pts[2] = {{0, 0}, {0, height}};
  const SkGradient grad(
      SkGradient::Colors(SkSpan<const SkColor4f>(colors.data(), colors.size()),
                         SkSpan<const float>(stops.data(), stops.size()),
                         SkTileMode::kClamp),
      SkGradient::Interpolation{});
  return SkShaders::LinearGradient(pts, grad);
}

sk_sp<SkShader> RadialGlow(SkPoint centre, float radius, SkColor4f inner) {
  std::vector<SkColor4f> colors = {inner, {inner.fR, inner.fG, inner.fB, 0}};
  std::vector<float> stops = {0, 1};
  const SkGradient grad(
      SkGradient::Colors(SkSpan<const SkColor4f>(colors.data(), colors.size()),
                         SkSpan<const float>(stops.data(), stops.size()),
                         SkTileMode::kClamp),
      SkGradient::Interpolation{});
  return SkShaders::RadialGradient(centre, radius, grad);
}

gfx::ImageSkia RenderSky(const gfx::Size& px, const Palette& palette,
                         uint32_t seed) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(px.width(), px.height());
  SkCanvas canvas(bitmap);
  const float w = static_cast<float>(px.width());
  const float h = static_cast<float>(px.height());
  SkPaint paint;
  paint.setShader(VerticalGradient(h, palette));
  canvas.drawRect(SkRect::MakeWH(w, h), paint);

  if (palette.stars) {
    // A scatter of stars, thicker and brighter higher up.
    Random rng(seed ^ 0x51A7u);
    const int count = static_cast<int>(w * h / 9000.0f);
    SkPaint star;
    star.setAntiAlias(true);
    for (int i = 0; i < count; ++i) {
      const float y = static_cast<float>(std::pow(rng.Next(), 1.6)) * h * 0.8f;
      const float x = static_cast<float>(rng.Next()) * w;
      const float r = static_cast<float>(rng.In(0.4, 1.4)) * std::max(1.0f, w / 1600.0f);
      star.setColor(SK_ColorWHITE);
      star.setAlphaf(static_cast<float>(rng.In(0.25, 0.95)) * (1.0f - y / h));
      canvas.drawCircle(x, y, r, star);
    }
  }

  // The sun's or the moon's glow.
  SkPaint glow;
  glow.setShader(RadialGlow({w * palette.glow_x, h * palette.glow_y},
                            std::max(w, h) * palette.glow_radius, palette.glow));
  canvas.drawRect(SkRect::MakeWH(w, h), glow);

  if (palette.moon) {
    // A small full moon, softly edged.
    SkPaint moon;
    moon.setAntiAlias(true);
    moon.setColor(SkColorSetRGB(0xF4, 0xF6, 0xFF));
    moon.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, w * 0.0015f));
    canvas.drawCircle(w * palette.glow_x, h * palette.glow_y, w * 0.022f, moon);
  }
  bitmap.setImmutable();
  return gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
}

// One cloud: a flat base with rounded puffs on top, a shaded underside, and a
// brighter crown where the light catches it. All soft-edged by blurring.
gfx::ImageSkia RenderCloud(int width, int layer, const Palette& palette,
                           Random& rng) {
  const int height = static_cast<int>(width * 0.46);
  SkBitmap bitmap;
  bitmap.allocN32Pixels(width, height);
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  SkCanvas canvas(bitmap);
  const float w = static_cast<float>(width);
  const float h = static_cast<float>(height);

  struct Puff {
    float x, y, r;
  };
  std::vector<Puff> puffs;
  const int n = 6 + static_cast<int>(rng.Next() * 4);
  for (int i = 0; i < n; ++i) {
    const float t = (i + 0.5f) / n;
    // Taller in the middle, lower at the ends.
    const float hump = 1.0f - std::abs(t - 0.5f) * 1.6f;
    const float r = h * static_cast<float>(rng.In(0.16, 0.24) + 0.14 * hump);
    const float x =
        w * (0.16f + 0.68f * t) + static_cast<float>(rng.In(-0.03, 0.03)) * w;
    const float y = h * 0.66f - r * static_cast<float>(rng.In(0.25, 0.75)) * hump;
    puffs.push_back({x, y, r});
  }

  const float soft = w * 0.022f;
  auto draw_body = [&](SkColor color, float alpha, float dy, float sigma) {
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(color);
    p.setAlphaf(alpha);
    p.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
    canvas.drawOval(
        SkRect::MakeLTRB(w * 0.1f, h * 0.5f + dy, w * 0.9f, h * 0.84f + dy), p);
    for (const Puff& puff : puffs) {
      canvas.drawCircle(puff.x, puff.y + dy, puff.r, p);
    }
  };
  const float body = std::min(1.0f, kBody[layer] * palette.cloud_alpha);
  draw_body(palette.underside, body * 0.75f, h * 0.05f, soft * 1.4f);
  draw_body(palette.cloud, body, 0, soft);
  SkPaint crown;
  crown.setAntiAlias(true);
  crown.setColor(palette.cloud);
  crown.setAlphaf(std::min(1.0f, body + 0.1f));
  crown.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, soft * 0.8f));
  for (const Puff& puff : puffs) {
    canvas.drawCircle(puff.x - puff.r * 0.18f, puff.y - puff.r * 0.2f,
                      puff.r * 0.62f, crown);
  }
  bitmap.setImmutable();
  return gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
}

}  // namespace

OnboardingSky::OnboardingSky(agent::SkyTime time, uint32_t seed)
    : time_(time), seed_(seed) {
  Random rng(seed);
  for (int layer = 0; layer < kLayers; ++layer) {
    for (int i = 0; i < kCount[layer]; ++i) {
      Cloud cloud;
      cloud.layer = layer;
      cloud.sprite = i % kVariants;
      // Spread across the drift width so the sky starts full, not empty.
      cloud.x = (i + rng.In(0.0, 0.8)) / kCount[layer] * (1.0 + kWidth[layer]) -
                kWidth[layer];
      cloud.y = rng.In(kBandTop[layer], kBandBottom[layer]);
      clouds_.push_back(cloud);
    }
  }
}

OnboardingSky::~OnboardingSky() = default;

void OnboardingSky::SetPointer(double nx, double ny) {
  target_x_ = std::clamp(nx, -1.0, 1.0);
  target_y_ = std::clamp(ny, -1.0, 1.0);
}

void OnboardingSky::Advance(double seconds) {
  seconds = std::clamp(seconds, 0.0, 0.25);
  for (Cloud& cloud : clouds_) {
    cloud.x -= kSpeed[cloud.layer] * seconds;
    // Off the left edge: come back in from the right.
    if (cloud.x < -kWidth[cloud.layer]) {
      cloud.x += 1.0 + kWidth[cloud.layer];
    }
  }
  // Ease toward the pointer: a slow follow reads as depth, a fast one as lag.
  const double k = 1.0 - std::exp(-seconds * 2.5);
  shift_x_ += (target_x_ - shift_x_) * k;
  shift_y_ += (target_y_ - shift_y_) * k;
}

std::vector<double> OnboardingSky::CloudPositionsForTesting() const {
  std::vector<double> out;
  for (const Cloud& cloud : clouds_) {
    out.push_back(cloud.x);
  }
  return out;
}

void OnboardingSky::EnsureImages(const gfx::Size& pixels) {
  if (pixels == pixels_ && !sky_.isNull()) {
    return;
  }
  pixels_ = pixels;
  const Palette& palette = PaletteFor(time_);
  sky_ = RenderSky(pixels, palette, seed_);
  sprites_.assign(kLayers, {});
  Random rng(seed_ ^ 0x9E3779B9u);
  for (int layer = 0; layer < kLayers; ++layer) {
    const int width =
        std::max(8, static_cast<int>(pixels.width() * kWidth[layer]));
    for (int v = 0; v < kVariants; ++v) {
      sprites_[layer].push_back(RenderCloud(width, layer, palette, rng));
    }
  }
}

void OnboardingSky::Paint(
    gfx::Canvas* canvas,
    const gfx::Size& dip_size,
    base::FunctionRef<void(int layer, float device_scale)> after_layer) {
  if (dip_size.IsEmpty()) {
    return;
  }
  gfx::ScopedCanvas scoped(canvas);
  const float dsf = canvas->UndoDeviceScaleFactor();
  const gfx::Size px = gfx::ScaleToCeiledSize(dip_size, dsf);
  EnsureImages(px);
  canvas->DrawImageInt(sky_, 0, 0);
  // Far layers first, so near clouds pass in front of them. Each layer slides
  // AGAINST the pointer by its depth.
  for (int layer = 0; layer < kLayers; ++layer) {
    const int dx = static_cast<int>(
        std::lround(-shift_x_ * kParallax[layer] * px.width()));
    const int dy = static_cast<int>(
        std::lround(-shift_y_ * kParallax[layer] * 0.6 * px.height()));
    for (const Cloud& cloud : clouds_) {
      if (cloud.layer != layer) {
        continue;
      }
      const gfx::ImageSkia& sprite =
          sprites_[layer][static_cast<size_t>(cloud.sprite % kVariants)];
      canvas->DrawImageInt(
          sprite, static_cast<int>(std::lround(cloud.x * px.width())) + dx,
          static_cast<int>(std::lround(cloud.y * px.height())) + dy);
    }
    after_layer(layer, dsf);
  }
}

}  // namespace zephyrus

// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/lens_painter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "base/no_destructor.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkColorFilter.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/effects/SkColorMatrix.h"
#include "third_party/skia/include/effects/SkGradient.h"
#include "third_party/skia/include/effects/SkImageFilters.h"

namespace zephyrus::agent {

namespace {

// ---- helpers -----------------------------------------------------------------

SkColor4f Color(double r, double g, double b, double a = 1.0) {
  return {static_cast<float>(r / 255.0), static_cast<float>(g / 255.0),
          static_cast<float>(b / 255.0), static_cast<float>(a)};
}

SkColor4f Hex(unsigned v, double a = 1.0) {
  return Color((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, a);
}

struct Stop {
  float at;
  SkColor4f color;
};

sk_sp<SkShader> Linear(SkPoint a, SkPoint b, const std::vector<Stop>& stops) {
  std::vector<SkColor4f> colors;
  std::vector<float> pos;
  for (const Stop& s : stops) {
    colors.push_back(s.color);
    pos.push_back(s.at);
  }
  const SkPoint pts[2] = {a, b};
  const SkGradient grad(
      SkGradient::Colors(SkSpan<const SkColor4f>(colors.data(), colors.size()),
                         SkSpan<const float>(pos.data(), pos.size()),
                         SkTileMode::kClamp),
      SkGradient::Interpolation{});
  return SkShaders::LinearGradient(pts, grad);
}

// An SVG objectBoundingBox radial gradient (centre and radius 50% of the box):
// an ellipse inscribed in `box`.
sk_sp<SkShader> RadialInBox(const SkRect& box, const std::vector<Stop>& stops) {
  std::vector<SkColor4f> colors;
  std::vector<float> pos;
  for (const Stop& s : stops) {
    colors.push_back(s.color);
    pos.push_back(s.at);
  }
  const SkGradient grad(
      SkGradient::Colors(SkSpan<const SkColor4f>(colors.data(), colors.size()),
                         SkSpan<const float>(pos.data(), pos.size()),
                         SkTileMode::kClamp),
      SkGradient::Interpolation{});
  SkMatrix m;
  m.setScale(box.width() / 2, box.height() / 2);
  m.postTranslate(box.centerX(), box.centerY());
  return SkShaders::RadialGradient({0, 0}, 1.0f, grad, &m);
}

SkPaint Fill(sk_sp<SkShader> shader, double alpha = 1.0) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setShader(std::move(shader));
  p.setAlphaf(static_cast<float>(alpha));
  return p;
}

SkPaint Solid(SkColor4f c, double alpha = 1.0) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setColor4f(c);
  p.setAlphaf(c.fA * static_cast<float>(alpha));
  return p;
}

// ---- the artwork's paths -------------------------------------------------------

SkPath BodyPath(double u, double k) {
  const LensBodyControls c = ComputeBodyControls(u, k);
  SkPathBuilder b;
  b.moveTo(0, 134.408f);
  b.cubicTo(static_cast<float>(c.a[1].x), static_cast<float>(c.a[1].y),
            static_cast<float>(c.a[2].x), static_cast<float>(c.a[2].y), 251,
            134.408f);
  b.lineTo(251, 385.605f);
  b.cubicTo(static_cast<float>(c.b[1].x), static_cast<float>(c.b[1].y),
            static_cast<float>(c.b[2].x), static_cast<float>(c.b[2].y), 0,
            385.605f);
  b.close();
  return b.detach();
}

// The eye's dome, in the artwork's absolute coordinates (left eye; the flat
// base is at y = 260.007, centred on x = 95.5).
const SkPath& DomePath() {
  static const base::NoDestructor<SkPath> path([] {
    SkPathBuilder b;
    b.moveTo(103.5f, 260.007f);
    b.cubicTo(105.709f, 260.007f, 107.562f, 258.168f, 106.839f, 256.08f);
    b.cubicTo(106.251f, 254.38f, 105.282f, 252.818f, 103.985f, 251.521f);
    b.cubicTo(101.735f, 249.271f, 98.6826f, 248.007f, 95.5f, 248.007f);
    b.cubicTo(92.3174f, 248.007f, 89.2652f, 249.271f, 87.0147f, 251.521f);
    b.cubicTo(85.7184f, 252.818f, 84.7493f, 254.38f, 84.1605f, 256.08f);
    b.cubicTo(83.4377f, 258.168f, 85.2909f, 260.007f, 87.5f, 260.007f);
    b.lineTo(95.5f, 260.007f);
    b.lineTo(103.5f, 260.007f);
    b.close();
    return b.detach();
  }());
  return *path;
}

// The default highlight, in the artwork's absolute coordinates.
const SkPath& BowlPath() {
  static const base::NoDestructor<SkPath> path([] {
    SkPathBuilder b;
    b.moveTo(99.5f, 252.007f);
    b.cubicTo(100.605f, 252.007f, 101.531f, 252.926f, 101.17f, 253.97f);
    b.cubicTo(100.875f, 254.82f, 100.391f, 255.601f, 99.7426f, 256.249f);
    b.cubicTo(98.6174f, 257.374f, 97.0913f, 258.007f, 95.5f, 258.007f);
    b.cubicTo(93.9087f, 258.007f, 92.3826f, 257.374f, 91.2574f, 256.249f);
    b.cubicTo(90.6092f, 255.601f, 90.1246f, 254.82f, 89.8303f, 253.97f);
    b.cubicTo(89.4688f, 252.926f, 90.3954f, 252.007f, 91.5f, 252.007f);
    b.lineTo(95.5f, 252.007f);
    b.lineTo(99.5f, 252.007f);
    b.close();
    return b.detach();
  }());
  return *path;
}

const SkPath& HeartPath() {
  static const base::NoDestructor<SkPath> path([] {
    SkPathBuilder b;
    b.moveTo(0, 5.2f);
    b.cubicTo(-1.2f, 4.2f, -6.4f, 1.2f, -6.4f, -2.4f);
    b.cubicTo(-6.4f, -4.6f, -4.8f, -5.8f, -3.2f, -5.8f);
    b.cubicTo(-1.8f, -5.8f, -0.6f, -5.0f, 0, -3.8f);
    b.cubicTo(0.6f, -5.0f, 1.8f, -5.8f, 3.2f, -5.8f);
    b.cubicTo(4.8f, -5.8f, 6.4f, -4.6f, 6.4f, -2.4f);
    b.cubicTo(6.4f, 1.2f, 1.2f, 4.2f, 0, 5.2f);
    b.close();
    return b.detach();
  }());
  return *path;
}

const SkPath& StarPath() {
  static const base::NoDestructor<SkPath> path([] {
    SkPathBuilder b;
    b.moveTo(0, -6.5f);
    b.cubicTo(0.7f, -2, 2, -0.7f, 6.5f, 0);
    b.cubicTo(2, 0.7f, 0.7f, 2, 0, 6.5f);
    b.cubicTo(-0.7f, 2, -2, 0.7f, -6.5f, 0);
    b.cubicTo(-2, -0.7f, -0.7f, -2, 0, -6.5f);
    b.close();
    return b.detach();
  }());
  return *path;
}

// 2.7 turns of an Archimedean spiral, as the prototype builds it (points
// rounded to two decimals, joined by straight segments).
const SkPath& SpiralPath() {
  static const base::NoDestructor<SkPath> path([] {
    SkPathBuilder b;
    bool first = true;
    for (double a = 0; a <= 2.7 * kLensTau; a += 0.22) {
      const double r = 0.3 + a * 0.33;
      const float x = static_cast<float>(std::round(r * std::cos(a) * 100) / 100);
      const float y = static_cast<float>(std::round(r * std::sin(a) * 100) / 100);
      if (first) {
        b.moveTo(x, y);
        first = false;
      } else {
        b.lineTo(x, y);
      }
    }
    return b.detach();
  }());
  return *path;
}

// A tear: a point at the top, a round bottom (an SVG arc of radius 3.4).
const SkPath& TearPath() {
  static const base::NoDestructor<SkPath> path([] {
    SkPathBuilder b;
    b.moveTo(0, -5);
    b.cubicTo(2.6f, -1, 3.4f, 1.2f, 3.4f, 2.4f);
    b.arcTo({3.4f, 3.4f}, 0, SkPathBuilder::kSmall_ArcSize, SkPathDirection::kCW,
            {-3.4f, 2.4f});
    b.cubicTo(-3.4f, 1.2f, -2.6f, -1, 0, -5);
    b.close();
    return b.detach();
  }());
  return *path;
}

// The lid: the part of the eye that shows, in eye-local coordinates.
SkPath LidPath(const LensLid& lid) {
  SkPathBuilder b;
  b.moveTo(-16, static_cast<float>(lid.a));
  b.lineTo(16, static_cast<float>(lid.b));
  b.lineTo(16, static_cast<float>(lid.yb));
  b.quadTo(0, static_cast<float>(lid.yc), -16, static_cast<float>(lid.yb));
  b.close();
  return b.detach();
}

// ---- layers -----------------------------------------------------------------------

// CSS saturate() then brightness(), as one colour matrix. They apply to a
// whole fill layer after it is composited, so the caller draws the layer into
// an offscreen layer with this filter.
sk_sp<SkColorFilter> SaturateBrightness(double sat, double bri) {
  const float s = static_cast<float>(sat);
  const float b = static_cast<float>(bri);
  const float m[20] = {
      b * (0.213f + 0.787f * s), b * (0.715f - 0.715f * s), b * (0.072f - 0.072f * s), 0, 0,
      b * (0.213f - 0.213f * s), b * (0.715f + 0.285f * s), b * (0.072f - 0.072f * s), 0, 0,
      b * (0.213f - 0.213f * s), b * (0.715f - 0.715f * s), b * (0.072f + 0.928f * s), 0, 0,
      0, 0, 0, 1, 0};
  SkColorMatrix matrix;
  matrix.setRowMajor(m);
  return SkColorFilters::Matrix(matrix);
}

// The supplied two-layer gradient, unchanged: a horizontal multi-stop palette
// and a vertical dark shading over it.
void PaintStaticFill(SkCanvas* canvas, const SkPath& body) {
  SkPaint base = Fill(Linear({0, 260.007f}, {251, 260.007f},
                             {{0.00f, Hex(0x082A53)},
                              {0.14f, Hex(0x0EBFF4)},
                              {0.32f, Hex(0xFF903C)},
                              {0.46f, Hex(0xFFF1B5)},
                              {0.57f, Hex(0xF3FFFD)},
                              {0.73f, Hex(0x9E78FF)},
                              {0.87f, Hex(0x5D89F0)},
                              {1.00f, Hex(0x141A41)}}));
  canvas->drawPath(body, base);
  SkPaint shade = Fill(Linear({125.5f, 0}, {125.5f, 520.013f},
                              {{0.00f, Hex(0x01050C, 0.97)},
                               {0.28f, Hex(0x01050C, 0.75)},
                               {0.42f, Hex(0xEB4530, 0.12)},
                               {0.51f, Hex(0xFFFFFF, 0.26)},
                               {0.61f, Hex(0x01050C, 0.15)},
                               {0.80f, Hex(0x01050C, 0.81)},
                               {1.00f, Hex(0x01050C, 0.98)}}));
  canvas->drawPath(body, shade);
}

// Tint, beam, flash and click ripples: effects shared by every fill, clipped to
// the body.
void PaintEffects(SkCanvas* canvas, const LensFrame& f) {
  const SkRect wide = SkRect::MakeXYWH(-10, 100, 271, 300);
  if (f.tint_opacity > 0.003) {
    const LensRgb& c = f.tint_color;
    // The prototype only refreshes the colour while it is visible; the value
    // here is the current easing target, which is what it would show.
    canvas->drawRect(
        wide, Fill(RadialInBox(wide, {{0, Color(c.r, c.g, c.b, 0.95)},
                                      {1, Color(c.r, c.g, c.b, 0.6)}}),
                   f.tint_opacity));
  }
  if (f.beam_opacity > 0.001) {
    SkRect beam = SkRect::MakeXYWH(-38, 100, 76, 300);
    beam.offset(static_cast<float>(f.beam_x), 0);
    canvas->drawRect(
        beam, Fill(Linear({beam.left(), 0}, {beam.right(), 0},
                          {{0, Color(255, 255, 255, 0)},
                           {0.5f, Color(255, 255, 255, 0.9)},
                           {1, Color(255, 255, 255, 0)}}),
                   f.beam_opacity));
  }
  if (f.flash_opacity > 0.001) {
    canvas->drawRect(
        wide, Fill(RadialInBox(wide, {{0, Color(255, 255, 255, 1)},
                                      {1, Color(255, 255, 255, 0)}}),
                   f.flash_opacity));
  }
  for (const LensRippleFrame& r : f.ripples) {
    if (!r.active || r.opacity <= 0) {
      continue;
    }
    SkPaint p = Solid(Color(255, 255, 255, 1), r.opacity);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(static_cast<float>(r.stroke));
    canvas->drawCircle(static_cast<float>(r.cx), static_cast<float>(r.cy),
                       static_cast<float>(r.r), p);
  }
}

// ---- the Fluid fill ------------------------------------------------------------------
//
// Dark glass with colour glowing inside: seven soft colour fields orbiting the
// middle, three waveform ribbons that pinch to nothing at the edges, a rotating
// gradient rim and a gloss sheen. Drawn into its own layer and composited at the
// fill's crossfade opacity, in the prototype's order. The two blurs (ribbons and
// rim) are what make it cost anything; `lite` leaves them out.

sk_sp<SkImageFilter> BlurFilter(float sigma) {
  return SkImageFilters::Blur(sigma, sigma, nullptr);
}

SkPaint ScreenPaint() {
  SkPaint p;
  p.setAntiAlias(true);
  p.setBlendMode(SkBlendMode::kScreen);
  return p;
}

SkPath PolylinePath(const std::array<LensPoint, 31>& points) {
  SkPathBuilder b;
  for (size_t i = 0; i < points.size(); ++i) {
    const SkPoint p = {static_cast<float>(points[i].x), static_cast<float>(points[i].y)};
    if (i == 0) {
      b.moveTo(p);
    } else {
      b.lineTo(p);
    }
  }
  return b.detach();
}

SkPaint Stroke(sk_sp<SkShader> shader, float width, double alpha = 1.0) {
  SkPaint p = Fill(std::move(shader), alpha);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(width);
  p.setStrokeCap(SkPaint::kRound_Cap);
  p.setStrokeJoin(SkPaint::kRound_Join);
  return p;
}

void PaintFluidFill(SkCanvas* canvas,
                    const LensFrame& f,
                    const SkPath& body,
                    const LensPaintOptions& options) {
  const LensFluidFrame& fl = f.fluid;
  const auto& pal = fl.palette;
  const auto color = [&](int i, double a = 1.0) {
    return Color(pal[i].r, pal[i].g, pal[i].b, a);
  };

  // 1. The glass the colour glows in.
  canvas->drawRect(SkRect::MakeXYWH(-10, 100, 271, 300),
                   Fill(Linear({0, 134}, {0, 386},
                               {{0.0f, Hex(0x06070E)},
                                {0.5f, Hex(0x0C0D1F)},
                                {1.0f, Hex(0x05060D)}})));

  // 2. The colour fields, breathing together.
  canvas->save();
  canvas->translate(125.5f, 262);
  canvas->scale(static_cast<float>(fl.blobs_scale), static_cast<float>(fl.blobs_scale));
  canvas->translate(-125.5f, -262);
  const std::array<LensBlobSpec, 7>& specs = LensBlobSpecs();
  for (int i = 0; i < 7; ++i) {
    const LensBlobSpec& b = specs[i];
    const LensBlobFrame& bf = fl.blobs[i];
    canvas->save();
    canvas->translate(static_cast<float>(bf.tx), static_cast<float>(bf.ty));
    canvas->rotate(static_cast<float>(bf.rot_deg));
    canvas->scale(static_cast<float>(bf.sx), static_cast<float>(bf.sy));
    // A soft radial field: full colour in the middle fading to nothing.
    const SkRect box = SkRect::MakeXYWH(-static_cast<float>(b.r), -static_cast<float>(b.r),
                                        2 * static_cast<float>(b.r), 2 * static_cast<float>(b.r));
    SkPaint p = Fill(RadialInBox(box, {{0.00f, color(i, 1.0)},
                                       {0.35f, color(i, 0.66)},
                                       {0.70f, color(i, 0.16)},
                                       {1.00f, color(i, 0.0)}}));
    if (b.screen) {
      p.setBlendMode(SkBlendMode::kScreen);
    }
    canvas->drawCircle(0, 0, static_cast<float>(b.r), p);
    canvas->restore();
  }
  canvas->restore();

  // 3. Waveform ribbons: a blurred glow (omitted in lite mode, where it would
  // look harsh without its blur) and a thin bright core.
  const auto ribbon_gradient = [&](int a, int b, int c) {
    return Linear({-10, 0}, {261, 0}, {{0.0f, color(a)}, {0.5f, color(b)}, {1.0f, color(c)}});
  };
  if (!options.lite) {
    SkPaint layer = ScreenPaint();
    layer.setImageFilter(BlurFilter(4.5f));
    canvas->saveLayer(nullptr, &layer);
    canvas->drawPath(PolylinePath(fl.ribbons[0]), Stroke(ribbon_gradient(2, 1, 3), 20));
    canvas->drawPath(PolylinePath(fl.ribbons[1]), Stroke(ribbon_gradient(4, 2, 1), 26));
    canvas->drawPath(PolylinePath(fl.ribbons[2]), Stroke(ribbon_gradient(2, 1, 3), 12, 0.85));
    canvas->restore();
  }
  {
    const LensRgb core = LensMix(pal[6], {255, 255, 255}, 0.5);
    const SkColor4f c = Color(core.r, core.g, core.b);
    const std::array<float, 3> widths = {2.2f, 2.8f, 1.7f};
    const std::array<double, 3> alphas = {0.75, 0.65, 0.7};
    SkPaint layer = ScreenPaint();
    canvas->saveLayer(nullptr, &layer);
    for (int i = 0; i < 3; ++i) {
      SkPaint p = Stroke(nullptr, widths[i], alphas[i]);
      p.setShader(nullptr);
      p.setColor4f(c);
      p.setAlphaf(static_cast<float>(alphas[i]));
      canvas->drawPath(PolylinePath(fl.ribbons[i]), p);
    }
    canvas->restore();
  }

  // 4. Shade the top and bottom into darkness, as the Static fill does.
  canvas->drawRect(SkRect::MakeXYWH(-10, 100, 271, 300),
                   Fill(Linear({125.5f, 0}, {125.5f, 520.013f},
                               {{0.00f, Hex(0x02030A, 0.90)},
                                {0.27f, Hex(0x02030A, 0.60)},
                                {0.42f, Hex(0x02030A, 0.03)},
                                {0.58f, Hex(0x02030A, 0.03)},
                                {0.78f, Hex(0x02030A, 0.62)},
                                {1.00f, Hex(0x02030A, 0.92)}})));

  // 5. The rim: a glowing gradient that turns slowly along the outline, and a
  // thin specular line.
  {
    const SkMatrix turn = SkMatrix::RotateDeg(static_cast<float>(fl.rim_rotation_deg), {125.5f, 262});
    std::vector<Stop> rim = {{0.00f, color(2)}, {0.25f, color(1)}, {0.50f, color(3)},
                             {0.75f, color(4)}, {1.00f, color(2)}};
    std::vector<SkColor4f> colors;
    std::vector<float> pos;
    for (const Stop& s : rim) {
      colors.push_back(s.color);
      pos.push_back(s.at);
    }
    const SkPoint pts[2] = {{0, 262}, {251, 262}};
    const SkGradient grad(
        SkGradient::Colors(SkSpan<const SkColor4f>(colors.data(), colors.size()),
                           SkSpan<const float>(pos.data(), pos.size()), SkTileMode::kClamp),
        SkGradient::Interpolation{});
    SkPaint glow = Stroke(SkShaders::LinearGradient(pts, grad, &turn),
                          options.lite ? 1.8f : 11.0f, fl.rim_glow_opacity);
    glow.setBlendMode(SkBlendMode::kScreen);
    if (!options.lite) {
      glow.setImageFilter(BlurFilter(5.0f));
    }
    canvas->drawPath(body, glow);
  }
  canvas->drawPath(body, Stroke(Linear({0, 134}, {251, 386},
                                       {{0.00f, Color(255, 255, 255, 0.90)},
                                        {0.32f, Color(255, 255, 255, 0.05)},
                                        {0.68f, Color(255, 255, 255, 0.05)},
                                        {1.00f, Color(255, 255, 255, 0.50)}}),
                                1.6f));

  // 6. The sheen along the top of the oval.
  canvas->save();
  canvas->translate(static_cast<float>(fl.gloss_tx), static_cast<float>(fl.gloss_ty));
  canvas->rotate(static_cast<float>(fl.gloss_rot_deg));
  canvas->drawOval(SkRect::MakeXYWH(-64, -15, 128, 30),
                   Fill(Linear({0, -15}, {0, 15},
                               {{0, Color(255, 255, 255, 0.30)}, {1, Color(255, 255, 255, 0)}})));
  canvas->restore();
}

// ---- the face ------------------------------------------------------------------------

void PaintHighlights(SkCanvas* canvas, const LensEyeFrame& eye) {
  // In the dome's frame the highlight group sits at (95.5, 255.007) plus the
  // parallax offset; every shape is centred on it. After the caller's
  // translate(-95.5, -260.007) that is an offset of (hl_tx, hl_ty - 5) from the
  // eye origin.
  canvas->translate(static_cast<float>(eye.hl_tx) + 95.5f,
                    static_cast<float>(eye.hl_ty) + 255.007f);
  for (int h = 0; h < kLensHighlightCount; ++h) {
    const LensEyeFrame::Shape& shape = eye.shapes[h];
    if (!shape.show) {
      continue;
    }
    canvas->save();
    switch (static_cast<LensHighlight>(h)) {
      case LensHighlight::kBowl:
        canvas->scale(static_cast<float>(shape.sx), static_cast<float>(shape.sy));
        canvas->translate(-95.5f, -255.007f);
        canvas->drawPath(BowlPath(), Solid(Color(255, 255, 255)));
        break;
      case LensHighlight::kHeart:
        canvas->scale(static_cast<float>(shape.sx), static_cast<float>(shape.sy));
        canvas->drawPath(HeartPath(), Solid(Hex(0xFF5F8D)));
        break;
      case LensHighlight::kStar:
        canvas->rotate(static_cast<float>(shape.rot_deg));
        canvas->scale(static_cast<float>(shape.sx), static_cast<float>(shape.sy));
        canvas->drawPath(StarPath(), Solid(Color(255, 255, 255)));
        break;
      case LensHighlight::kDot:
        canvas->scale(static_cast<float>(shape.sx), static_cast<float>(shape.sy));
        canvas->drawCircle(0, 0, 3.4f, Solid(Color(255, 255, 255)));
        break;
      case LensHighlight::kSpiral: {
        canvas->rotate(static_cast<float>(shape.rot_deg));
        canvas->scale(static_cast<float>(shape.sx), static_cast<float>(shape.sy));
        SkPaint p = Solid(Color(255, 255, 255));
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(1.25f);
        p.setStrokeCap(SkPaint::kRound_Cap);
        p.setStrokeJoin(SkPaint::kRound_Join);
        canvas->drawPath(SpiralPath(), p);
        break;
      }
      default:
        break;
    }
    canvas->restore();
  }
}

void PaintEye(SkCanvas* canvas, const LensFrame& f, const LensEyeFrame& eye) {
  canvas->save();
  // translate(tx ty) rotate(rot 0 -5) scale(sx sy)
  canvas->translate(static_cast<float>(eye.tx), static_cast<float>(eye.ty));
  canvas->translate(0, -5);
  canvas->rotate(static_cast<float>(eye.rot_deg));
  canvas->translate(0, 5);
  canvas->scale(static_cast<float>(eye.sx), static_cast<float>(eye.sy));
  // The lid clips the eye; the lid path is in this (eye-local) frame.
  canvas->clipPath(LidPath(eye.lid), /*doAntiAlias=*/true);
  canvas->translate(-95.5f, -260.007f);
  SkPaint dome = Fill(Linear({0, 248.007f}, {0, 260.007f},
                             {{0.00f, Hex(0x010308)},
                              {0.45f, Color(f.eye_middle.r, f.eye_middle.g,
                                            f.eye_middle.b)},
                              {1.00f, Color(f.eye_bottom.r, f.eye_bottom.g,
                                            f.eye_bottom.b)}}));
  canvas->drawPath(DomePath(), dome);
  PaintHighlights(canvas, eye);
  canvas->restore();
}

void PaintTears(SkCanvas* canvas, const LensFrame& f) {
  for (const LensTearFrame& t : f.tears) {
    if (t.opacity <= 0) {
      continue;
    }
    canvas->save();
    canvas->translate(static_cast<float>(t.x), static_cast<float>(t.y));
    canvas->scale(static_cast<float>(t.sx), static_cast<float>(t.sy));
    SkPaint p = Fill(Linear({0, -5}, {0, 5.8f},
                            {{0, Hex(0xD8F0FF)}, {1, Hex(0x6CB8FF)}}),
                     t.opacity);
    canvas->drawPath(TearPath(), p);
    canvas->restore();
  }
}

}  // namespace

void PaintLens(SkCanvas* canvas,
               const LensFrame& f,
               const SkRect& dest,
               const LensPaintOptions& options) {
  const int saved = canvas->save();
  // The viewBox -> dest mapping.
  canvas->translate(dest.left(), dest.top());
  canvas->scale(static_cast<float>(dest.width() / kLensViewWidth),
                static_cast<float>(dest.height() / kLensViewHeight));
  canvas->translate(static_cast<float>(-kLensViewX),
                    static_cast<float>(-kLensViewY));

  // bodyG: translate(tx ty) rotate(deg) scale(sx sy) translate(-125.5 -330)
  canvas->translate(static_cast<float>(f.body_tx), static_cast<float>(f.body_ty));
  canvas->rotate(static_cast<float>(f.body_rot_deg));
  canvas->scale(static_cast<float>(f.body_sx), static_cast<float>(f.body_sy));
  canvas->translate(-125.5f, -330);

  const SkPath body = BodyPath(f.u, f.k);

  // The fill layers, bottom to top: Static (opaque, hidden once the others have
  // taken over), then Fluid at its crossfade opacity. Each is drawn into a layer
  // when the flat moods' saturate/brightness filter applies, because the filter
  // acts on the composited layer, not on each shape in it.
  const bool filtered = std::abs(f.sat - 1) > 0.005 || std::abs(f.bri - 1) > 0.005;
  if (f.layers.show_static) {
    if (filtered) {
      SkPaint layer;
      layer.setColorFilter(SaturateBrightness(f.sat, f.bri));
      canvas->saveLayer(nullptr, &layer);
      PaintStaticFill(canvas, body);
      canvas->restore();
    } else {
      PaintStaticFill(canvas, body);
    }
  }
  if (f.layers.show_fluid && f.fluid.computed) {
    canvas->save();
    canvas->clipPath(body, /*doAntiAlias=*/true);
    SkPaint layer;
    layer.setAlphaf(static_cast<float>(f.layers.fluid));
    if (filtered) {
      layer.setColorFilter(SaturateBrightness(f.sat, f.bri));
    }
    canvas->saveLayer(nullptr, &layer);
    PaintFluidFill(canvas, f, body, options);
    canvas->restore();
    canvas->restore();
  }

  // Effects, clipped to the body.
  canvas->save();
  canvas->clipPath(body, /*doAntiAlias=*/true);
  PaintEffects(canvas, f);
  canvas->restore();

  // The face turns with the slant.
  canvas->save();
  canvas->translate(125.5f, 260);
  canvas->rotate(static_cast<float>(f.face_rot_deg));
  canvas->translate(-125.5f, -260);
  PaintEye(canvas, f, f.eyes[0]);
  PaintEye(canvas, f, f.eyes[1]);
  PaintTears(canvas, f);
  canvas->restore();

  canvas->restoreToCount(saved);
}

SkBitmap RenderLensToBitmap(const LensFrame& frame,
                            int width,
                            int height,
                            const LensPaintOptions& options) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(width, height);
  bitmap.eraseColor(SK_ColorTRANSPARENT);
  SkCanvas canvas(bitmap);
  PaintLens(&canvas, frame,
            SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)),
            options);
  return bitmap;
}

}  // namespace zephyrus::agent

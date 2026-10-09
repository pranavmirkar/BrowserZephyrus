// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Milestone 3: the Skia painter.
//
// Two jobs. Always: every state paints something sensible and paints it the same
// way twice. On request: when LENS_DUMP_DIR is set, write a PNG per state so the
// native drawing can be compared with the prototype's (tools in
// docs/mascot-lens-plan.md, "Parity method"). The dump uses the same seed, frame
// count and size as the reference capture page, so the same frame is drawn on
// both sides.

#include <cstdlib>
#include <string>
#include <vector>

#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "chrome/browser/zephyrus/agent/lens_model.h"
#include "chrome/browser/zephyrus/agent/lens_painter.h"
#include "chrome/browser/zephyrus/agent/lens_random.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "ui/gfx/codec/png_codec.h"

namespace zephyrus::agent {
namespace {

// What the reference capture does for every state: a fresh seeded model, 108
// frames (1.8 s) at 60 fps. Size is the viewBox at 2x.
constexpr uint32_t kCaptureSeed = 4242;
constexpr int kCaptureFrames = 108;
constexpr int kWidth = 622;
constexpr int kHeight = 632;

LensFrame CaptureFrame(LensState state,
                       LensFill fill,
                       double intensity = 1.0,
                       int frames = kCaptureFrames) {
  SeededLensRandom throwaway(0);
  LensModel model(&throwaway);
  SeededLensRandom rng(kCaptureSeed);
  model.SetRandom(&rng);
  model.Reset();
  model.SetFill(fill);
  model.SetIntensity(intensity);
  model.SetState(state, /*force=*/true);
  for (int i = 0; i < frames; ++i) {
    model.Update(1.0 / 60);
  }
  return model.ComputeFrame();
}

struct Stats {
  int opaque = 0;      // pixels with alpha > 0
  int bright = 0;      // fairly light pixels (the lit body)
  int dark_blue = 0;   // eye-ish pixels
  int min_x = 1 << 30, max_x = -1, min_y = 1 << 30, max_y = -1;
};

// Whether two bitmaps hold exactly the same pixels.
bool SamePixels(const SkBitmap& a, const SkBitmap& b) {
  if (a.width() != b.width() || a.height() != b.height()) {
    return false;
  }
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      if (a.getColor(x, y) != b.getColor(x, y)) {
        return false;
      }
    }
  }
  return true;
}

Stats Measure(const SkBitmap& bitmap) {
  Stats s;
  for (int y = 0; y < bitmap.height(); ++y) {
    for (int x = 0; x < bitmap.width(); ++x) {
      const SkColor c = bitmap.getColor(x, y);
      if (SkColorGetA(c) == 0) {
        continue;
      }
      ++s.opaque;
      s.min_x = std::min(s.min_x, x);
      s.max_x = std::max(s.max_x, x);
      s.min_y = std::min(s.min_y, y);
      s.max_y = std::max(s.max_y, y);
      if (SkColorGetR(c) + SkColorGetG(c) + SkColorGetB(c) > 450) {
        ++s.bright;
      }
      if (SkColorGetB(c) > 100 && SkColorGetR(c) < 40 && SkColorGetG(c) < 80) {
        ++s.dark_blue;
      }
    }
  }
  return s;
}

TEST(LensPainterTest, EveryStatePaintsABodyInsideItsWindowAndIsDeterministic) {
  for (int i = 0; i < kLensStateCount; ++i) {
    const LensState s = static_cast<LensState>(i);
    const LensFrame f = CaptureFrame(s, LensFill::kStatic);
    const SkBitmap a = RenderLensToBitmap(f, kWidth, kHeight);
    const SkBitmap b = RenderLensToBitmap(f, kWidth, kHeight);
    const Stats st = Measure(a);
    // The body is a big lens: a good fraction of the window, never all of it.
    EXPECT_GT(st.opaque, kWidth * kHeight / 8) << LensStateKey(s);
    EXPECT_LT(st.opaque, kWidth * kHeight * 3 / 4) << LensStateKey(s);
    EXPECT_GT(st.bright, 2000) << LensStateKey(s) << ": a lit body";
    // Inside the window with a margin (the viewBox leaves room for squash,
    // tilt and tears).
    EXPECT_GT(st.min_x, 0) << LensStateKey(s);
    EXPECT_LT(st.max_x, kWidth - 1) << LensStateKey(s);
    EXPECT_GT(st.min_y, 0) << LensStateKey(s);
    // Same frame, same pixels.
    EXPECT_TRUE(SamePixels(a, b)) << LensStateKey(s);
  }
}

TEST(LensPainterTest, TheEyesAreDrawnAndAClosedEyeShowsLess) {
  const SkBitmap awake = RenderLensToBitmap(
      CaptureFrame(LensState::kSurprise, LensFill::kStatic), kWidth, kHeight);
  const SkBitmap asleep = RenderLensToBitmap(
      CaptureFrame(LensState::kSleeping, LensFill::kStatic), kWidth, kHeight);
  // Count near-black pixels (the eye dome) in the eye band for each.
  const auto dark = [](const SkBitmap& bmp) {
    int n = 0;
    for (int y = 250; y < 420; ++y) {
      for (int x = 150; x < 480; ++x) {
        const SkColor c = bmp.getColor(x, y);
        if (SkColorGetA(c) > 200 && SkColorGetR(c) + SkColorGetG(c) + SkColorGetB(c) < 90) {
          ++n;
        }
      }
    }
    return n;
  };
  EXPECT_GT(dark(awake), 3 * dark(asleep) + 50)
      << "a surprised eye is much bigger than a sleeping one";
}

TEST(LensPainterTest, TearsEffectsAndTheFilterChangeThePicture) {
  // A crying mascot with tears falling versus the same pose with none.
  SeededLensRandom rng(1);
  LensModel crying(&rng);
  crying.SetState(LensState::kCrying, true);
  for (int i = 0; i < 90; ++i) {
    crying.Update(1.0 / 60);
  }
  LensFrame with_tears = crying.ComputeFrame();
  ASSERT_FALSE(with_tears.tears.empty());
  LensFrame without = with_tears;
  without.tears.clear();
  const SkBitmap a = RenderLensToBitmap(with_tears, kWidth, kHeight);
  const SkBitmap b = RenderLensToBitmap(without, kWidth, kHeight);
  EXPECT_FALSE(SamePixels(a, b));

  // A click ripple adds pixels over the body.
  LensFrame rippled = without;
  rippled.ripples[0] = {true, 125, 262, 60, 3, 0.8};
  EXPECT_FALSE(SamePixels(RenderLensToBitmap(rippled, kWidth, kHeight), b));

  // Desaturated and dimmed (the flat moods) differs from full colour.
  LensFrame flat = without;
  flat.sat = 0.5;
  flat.bri = 0.8;
  EXPECT_FALSE(SamePixels(RenderLensToBitmap(flat, kWidth, kHeight), b));
}

TEST(LensPainterTest, EveryStatePaintsAFluidBodyThatLooksNothingLikeTheStaticOne) {
  for (int i = 0; i < kLensStateCount; ++i) {
    const LensState s = static_cast<LensState>(i);
    const LensFrame fluid = CaptureFrame(s, LensFill::kFluid);
    ASSERT_TRUE(fluid.layers.show_fluid) << LensStateKey(s);
    ASSERT_TRUE(fluid.fluid.computed) << LensStateKey(s);
    const SkBitmap a = RenderLensToBitmap(fluid, kWidth, kHeight);
    const Stats st = Measure(a);
    EXPECT_GT(st.opaque, kWidth * kHeight / 8) << LensStateKey(s);
    EXPECT_LT(st.opaque, kWidth * kHeight * 3 / 4) << LensStateKey(s);
    // Same pose, other fill: the picture must actually differ.
    const SkBitmap b = RenderLensToBitmap(CaptureFrame(s, LensFill::kStatic), kWidth, kHeight);
    EXPECT_FALSE(SamePixels(a, b)) << LensStateKey(s);
    // And it is repeatable.
    EXPECT_TRUE(SamePixels(a, RenderLensToBitmap(fluid, kWidth, kHeight))) << LensStateKey(s);
  }
}

TEST(LensPainterTest, LiteIsDifferentButKeepsTheSameShapeAndReverts) {
  const LensFrame f = CaptureFrame(LensState::kWorking, LensFill::kFluid);
  LensPaintOptions lite;
  lite.lite = true;
  const SkBitmap full = RenderLensToBitmap(f, kWidth, kHeight);
  const SkBitmap thin = RenderLensToBitmap(f, kWidth, kHeight, lite);
  EXPECT_FALSE(SamePixels(full, thin)) << "lite drops the blurs";
  // The silhouette (the body outline) is the same either way.
  const Stats a = Measure(full);
  const Stats b = Measure(thin);
  EXPECT_NEAR(a.opaque, b.opaque, a.opaque / 10);
  // Reversible: going back to full quality gives back the exact full picture.
  EXPECT_TRUE(SamePixels(full, RenderLensToBitmap(f, kWidth, kHeight)));
  // Lite does nothing to a Static body, which has no blurs to drop.
  const LensFrame st = CaptureFrame(LensState::kWorking, LensFill::kStatic);
  EXPECT_TRUE(SamePixels(RenderLensToBitmap(st, kWidth, kHeight),
                         RenderLensToBitmap(st, kWidth, kHeight, lite)));
}

TEST(LensPainterTest, AFluidFrameNeverComputedPaintsNoFluid) {
  // Hidden layers do zero work: with the layer off the frame carries no fluid
  // geometry and the picture is the Static one.
  const LensFrame f = CaptureFrame(LensState::kIdle, LensFill::kStatic);
  EXPECT_FALSE(f.fluid.computed);
  EXPECT_FALSE(f.layers.show_fluid);
}

// ---- what it costs ----------------------------------------------------------------

// Not a pass/fail test: with LENS_BENCH=1 it prints how long one frame takes,
// split into the model (Update + ComputeFrame) and painting. This is SOFTWARE
// raster on whatever machine runs it, so it is a ceiling: the browser composites
// on the GPU where it can. Run with the machine otherwise idle.
TEST(LensPainterTest, CostPerFrame) {
  const std::optional<std::string> on = base::Environment::Create()->GetVar("LENS_BENCH");
  if (!on || on->empty()) {
    GTEST_SKIP() << "set LENS_BENCH=1 to measure";
  }
  struct Row {
    const char* name;
    LensFill fill;
    bool lite;
  };
  const Row rows[] = {{"static", LensFill::kStatic, false},
                      {"fluid", LensFill::kFluid, false},
                      {"fluid-lite", LensFill::kFluid, true}};
  constexpr int kFrames = 300;
  for (const Row& row : rows) {
    for (int size : {96, 160, 311, 622}) {
      SeededLensRandom rng(1);
      LensModel model(&rng);
      model.SetFill(row.fill);
      model.SetState(LensState::kWorking, true);
      for (int i = 0; i < 180; ++i) {  // let the fill crossfade finish
        model.Update(1.0 / 60);
      }
      SkBitmap bitmap;
      bitmap.allocN32Pixels(size, size);
      SkCanvas canvas(bitmap);
      LensPaintOptions options;
      options.lite = row.lite;
      base::TimeDelta model_time, paint_time;
      for (int i = 0; i < kFrames; ++i) {
        base::ElapsedTimer m;
        model.Update(1.0 / 60);
        const LensFrame frame = model.ComputeFrame();
        model_time += m.Elapsed();
        base::ElapsedTimer p;
        bitmap.eraseColor(SK_ColorTRANSPARENT);
        PaintLens(&canvas, frame, SkRect::MakeWH(size, size), options);
        paint_time += p.Elapsed();
      }
      GTEST_LOG_(INFO) << row.name << " @" << size << "px: model "
                       << model_time.InMicroseconds() / kFrames << " us, paint "
                       << paint_time.InMicroseconds() / kFrames << " us per frame";
    }
  }
}

// ---- the parity dump --------------------------------------------------------------

TEST(LensPainterTest, DumpEveryStateWhenAskedFor) {
  const std::optional<std::string> dir =
      base::Environment::Create()->GetVar("LENS_DUMP_DIR");
  if (!dir || dir->empty()) {
    GTEST_SKIP() << "set LENS_DUMP_DIR to write one PNG per state";
  }
  const base::FilePath out = base::FilePath::FromUTF8Unsafe(*dir);
  ASSERT_TRUE(base::CreateDirectory(out));
  // LENS_DUMP_FRAMES picks the moment (default: the reference capture's).
  int frames = kCaptureFrames;
  if (const std::optional<std::string> f =
          base::Environment::Create()->GetVar("LENS_DUMP_FRAMES")) {
    base::StringToInt(*f, &frames);
  }
  // LENS_DUMP_FILL=fluid writes the Fluid fill instead of the Static one.
  LensFill fill = LensFill::kStatic;
  if (const std::optional<std::string> f =
          base::Environment::Create()->GetVar("LENS_DUMP_FILL")) {
    fill = *f == "fluid" ? LensFill::kFluid : LensFill::kStatic;
  }
  for (int i = 0; i < kLensStateCount; ++i) {
    const LensState s = static_cast<LensState>(i);
    LensPaintOptions options;
    options.lite = base::Environment::Create()->HasVar("LENS_DUMP_LITE");
    const SkBitmap bitmap = RenderLensToBitmap(
        CaptureFrame(s, fill, 1.0, frames), kWidth, kHeight, options);
    const std::optional<std::vector<uint8_t>> png =
        gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, /*discard_transparency=*/false);
    ASSERT_TRUE(png.has_value()) << LensStateKey(s);
    const std::string name = std::string(LensStateKey(s)) + ".png";
    ASSERT_TRUE(base::WriteFile(out.AppendASCII(name), *png)) << name;
  }
}

}  // namespace
}  // namespace zephyrus::agent

// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ONBOARDING_SKY_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ONBOARDING_SKY_H_

#include <cstdint>
#include <vector>

#include "base/functional/function_ref.h"
#include "chrome/browser/zephyrus/agent/onboarding_flow.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"

namespace gfx {
class Canvas;
}

namespace zephyrus {

// The onboarding's backdrop: a sky for the time of day (dawn, day, sunset or
// night) with soft clouds drifting across it in three layers, the far ones
// small, pale and slow, the near ones big and quicker. The layers also slide a
// little against each other as the pointer moves, so the sky has depth.
//
// Built for being repainted 30 times a second over the whole window. Nothing is
// blurred or gradient-filled per frame: the sky and every cloud are rendered
// ONCE into images (redone only when the window's pixel size changes), and a
// frame is one sky image plus a dozen cloud images at new positions. The images
// keep their ids, so the GPU keeps their textures.
class OnboardingSky {
 public:
  static constexpr int kLayers = 3;

  explicit OnboardingSky(agent::SkyTime time, uint32_t seed = 7);
  OnboardingSky(const OnboardingSky&) = delete;
  OnboardingSky& operator=(const OnboardingSky&) = delete;
  ~OnboardingSky();

  agent::SkyTime time() const { return time_; }

  // Moves the clouds on by `seconds` of wind, and eases the parallax toward
  // where the pointer is.
  void Advance(double seconds);

  // Where the pointer is, from -1 (left/top) to 1 (right/bottom) of the window.
  void SetPointer(double nx, double ny);

  // Paints the whole backdrop into `canvas`, which covers `dip_size`. Draws in
  // physical pixels so the clouds are never resampled. `after_layer(layer)`
  // runs after each cloud layer, still in physical pixels, so something can fly
  // BETWEEN the layers: in front of the far clouds, behind the near ones.
  void Paint(gfx::Canvas* canvas,
             const gfx::Size& dip_size,
             base::FunctionRef<void(int layer, float device_scale)> after_layer);

  // For tests: where each cloud is, as a fraction of the width it drifts over.
  std::vector<double> CloudPositionsForTesting() const;

 private:
  struct Cloud {
    int layer = 0;      // 0 far, 1 middle, 2 near
    int sprite = 0;     // which picture of that layer
    double x = 0;       // left edge, as a fraction of the drift width
    double y = 0;       // top, as a fraction of the window height
  };

  void EnsureImages(const gfx::Size& pixels);

  agent::SkyTime time_;
  std::vector<Cloud> clouds_;
  gfx::Size pixels_;
  gfx::ImageSkia sky_;
  // [layer][variant]
  std::vector<std::vector<gfx::ImageSkia>> sprites_;
  uint32_t seed_;
  // Parallax: where the pointer is, and where the layers have eased to.
  double target_x_ = 0, target_y_ = 0;
  double shift_x_ = 0, shift_y_ = 0;
};

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ONBOARDING_SKY_H_

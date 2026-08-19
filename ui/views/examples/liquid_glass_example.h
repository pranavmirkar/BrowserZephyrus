// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_EXAMPLES_LIQUID_GLASS_EXAMPLE_H_
#define UI_VIEWS_EXAMPLES_LIQUID_GLASS_EXAMPLE_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/controls/slider.h"
#include "ui/views/examples/example_base.h"
#include "ui/views/examples/views_examples_export.h"
#include "ui/views/liquid_glass.h"

namespace views {
class Label;

namespace examples {

// Demonstrates the reusable Liquid Glass material (views::ApplyLiquidGlass) by
// applying it to several panels floating over a colorful, high-frequency
// backdrop, with live sliders to tune blur, refraction, corner radius and
// tint on one of the panels.
class VIEWS_EXAMPLES_EXPORT LiquidGlassExample : public ExampleBase,
                                                 public SliderListener {
 public:
  LiquidGlassExample();

  LiquidGlassExample(const LiquidGlassExample&) = delete;
  LiquidGlassExample& operator=(const LiquidGlassExample&) = delete;

  ~LiquidGlassExample() override;

  // ExampleBase:
  void CreateExampleView(View* container) override;

  // SliderListener:
  void SliderValueChanged(Slider* sender,
                          float value,
                          float old_value,
                          SliderChangeReason reason) override;

 private:
  // Re-reads the sliders into `params_`, re-applies glass to the tunable panel
  // and refreshes the readout label.
  void ApplyFromSliders();

  LiquidGlassParams params_;

  raw_ptr<View> tunable_panel_ = nullptr;
  raw_ptr<Label> readout_label_ = nullptr;
  raw_ptr<Slider> blur_slider_ = nullptr;
  raw_ptr<Slider> zoom_slider_ = nullptr;
  raw_ptr<Slider> radius_slider_ = nullptr;
  raw_ptr<Slider> tint_slider_ = nullptr;
};

}  // namespace examples
}  // namespace views

#endif  // UI_VIEWS_EXAMPLES_LIQUID_GLASS_EXAMPLE_H_

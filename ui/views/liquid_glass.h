// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_LIQUID_GLASS_H_
#define UI_VIEWS_LIQUID_GLASS_H_

#include "base/functional/callback.h"
#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"
#include "ui/views/views_export.h"

namespace ui {
class MouseEvent;
}  // namespace ui

namespace views {

class View;

// Parameters describing a "Liquid Glass" material (translucent, refractive,
// with a specular sheen and bright rim). The defaults approximate a light
// environment; tune per surface or start from the Light()/Dark() presets.
struct VIEWS_EXPORT LiquidGlassParams {
  // Corner radius of the glass, in DIP. Applied to all four corners.
  float corner_radius = 12.f;

  // Frosted blur applied to whatever is *behind* the glass. The compositor
  // blurs by roughly 3x this standard deviation (in DIP). Larger = frostier.
  float blur_sigma = 18.f;

  // Lens refraction. Magnifies the backdrop by |refraction_zoom|, blended
  // across |refraction_inset| px at the edge, which bends light near the rim
  // -- the native analogue of Apple's displacement-map refraction. Set zoom to
  // 1.0 to disable.
  //
  // NOTE: background zoom has no effect under software compositing
  // (crbug.com/1451898). On GPU-composited platforms (the norm on Windows) it
  // works; the blur/tint/highlight below still render regardless.
  float refraction_zoom = 1.15f;
  int refraction_inset = 12;

  // Tint painted over the blurred backdrop. The alpha channel controls how
  // milky the glass is; the RGB gives it a color cast.
  SkColor tint_color = SkColorSetARGB(0x24, 0xFF, 0xFF, 0xFF);

  // Strength (0-1) of the bright specular sheen along the top edge.
  float highlight_opacity = 0.55f;

  // Strength (0-1) of the thin bright rim stroke around the glass.
  float rim_opacity = 0.35f;

  // Convenience presets.
  static LiquidGlassParams Light();
  static LiquidGlassParams Dark();
};

// Applies the Liquid Glass material to |view|. This is the only call you need:
// it promotes the view to a texture layer, enables backdrop blur + edge
// refraction with rounded corners, and installs a Background that paints the
// tint, specular highlight and rim.
//
// Works on ANY views::View. The view's own contents (labels, images, child
// views) paint on top of the glass, so foreground content stays crisp. Safe to
// call again with different params to re-tune; call RemoveLiquidGlass() to
// undo the backdrop effects.
VIEWS_EXPORT void ApplyLiquidGlass(View* view,
                                   const LiquidGlassParams& params = {});

// Removes the glass background and backdrop effects previously applied by
// ApplyLiquidGlass(). Leaves the view's layer in place (other code may depend
// on it) but clears the blur, zoom and background.
VIEWS_EXPORT void RemoveLiquidGlass(View* view);

// Animates `view` into place: fades opacity 0->1 and scales 0.96->1.0 about its
// center with an enter-the-scene easing. The view must already own a layer
// (e.g. after ApplyLiquidGlass). `delay` lets callers stagger several panels.
// Movement is dropped (fade only) when the OS asks for reduced motion. Call
// once the view has non-empty bounds so the scale pivots on the true center.
VIEWS_EXPORT void AnimateLiquidGlassIn(View* view,
                                       base::TimeDelta delay = base::TimeDelta());

// A drop-in glass container that manages its own material and interaction
// motion: a subtle lift on hover, a scale-down on press, and a liquid settle on
// release -- the Apple-style "this surface is alive" feel. Put child views
// inside it as usual; call SetPressedCallback() to make it behave like a
// pressable glass surface. All motion respects the OS reduced-motion setting.
class VIEWS_EXPORT LiquidGlassView : public View {
  METADATA_HEADER(LiquidGlassView, View)

 public:
  explicit LiquidGlassView(LiquidGlassParams params = {});
  LiquidGlassView(const LiquidGlassView&) = delete;
  LiquidGlassView& operator=(const LiquidGlassView&) = delete;
  ~LiquidGlassView() override;

  // Re-applies the material with new parameters.
  void SetGlassParams(const LiquidGlassParams& params);

  // When set, the view acts like a button: it consumes the press and runs
  // `callback` on release inside its bounds.
  void SetPressedCallback(base::RepeatingClosure callback);

  // View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;

 private:
  // Resting scale given current hover/press state.
  float TargetScale() const;

  LiquidGlassParams params_;
  base::RepeatingClosure pressed_callback_;
  bool hovered_ = false;
  bool pressed_ = false;
};

}  // namespace views

#endif  // UI_VIEWS_LIQUID_GLASS_H_

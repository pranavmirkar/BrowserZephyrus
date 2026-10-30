// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_PAINTER_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_PAINTER_H_

#include "chrome/browser/zephyrus/agent/lens_model.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkRect.h"

// Draws the lens mascot from a LensFrame with plain Skia: no views, no
// compositor, nothing but a canvas. That is what lets milestone 3 render every
// state to a PNG in a unit test and compare it with the prototype, and what lets
// the overlay hand the painter a gfx::Canvas's SkCanvas without the painter
// knowing about views at all.
//
// It is a port of the prototype's SVG (agent-mascot.html), element for element:
// the same gradients, the same clip paths, the same group transforms, in the
// same order. Static and Fluid fills are drawn; Opal and Space are not in
// version 1.

namespace zephyrus::agent {

// The artwork's visible window, in its own coordinates (the SVG viewBox).
inline constexpr double kLensViewX = -30;
inline constexpr double kLensViewY = 98;
inline constexpr double kLensViewWidth = 311;
inline constexpr double kLensViewHeight = 316;

// How to paint, per call, so the caller can change its mind frame to frame.
struct LensPaintOptions {
  // The prototype's slow-machine fallback ("lite"), made reversible: the Fluid
  // fill is painted without its two blur filters, its glow-only layers are left
  // out and the rim is thinned. The body, eyes and effects are unchanged. The
  // prototype switched this on for the life of the page; here the caller (see
  // LensQualityGovernor) can switch it back off when the machine recovers.
  bool lite = false;
};

// Paints `frame` so that the artwork's viewBox fills `dest`. Draws only the
// mascot (transparent elsewhere); the caller supplies any background. The
// canvas state is restored on return.
void PaintLens(SkCanvas* canvas,
               const LensFrame& frame,
               const SkRect& dest,
               const LensPaintOptions& options = {});

// Convenience for tests and tools: a transparent BGRA bitmap of the given size
// with the mascot painted into it.
SkBitmap RenderLensToBitmap(const LensFrame& frame,
                            int width,
                            int height,
                            const LensPaintOptions& options = {});

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_LENS_PAINTER_H_

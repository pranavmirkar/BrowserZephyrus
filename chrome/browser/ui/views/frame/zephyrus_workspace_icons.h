// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_ICONS_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_ICONS_H_

#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/time/time.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect_f.h"

namespace gfx {
class Canvas;
}

namespace zephyrus {

// The silhouette a workspace's container morphs INTO when it is current.
//
// Shape is versatile, not semantic -- these carry no fixed meaning and are
// assigned for variety, which is Material's own rule for them. What matters is
// only that two adjacent workspaces do not land on the same one.
enum class Shape {
  kCircle,
  kSquircle,
  kCookie,
  kClover,
  kBurst,
  kGem,
  kPentagon,
  kTriangle,
  kHexagon,
};

// What the glyph does while its cell is hovered.
//
// Chosen for what the workspace MEANS, which is SF Symbols' discipline rather
// than Material's: bounce to acknowledge, breathe or pulse for something
// ongoing, orbit for work in progress. Unlike Shape, these are semantic.
enum class Effect {
  kBounce,   // Overshoot and settle. One-shot.
  kWiggle,   // Rotational shake. One-shot.
  kJitter,   // Squash and stretch, axes counter-phased. One-shot.
  kStagger,  // Each subpath springs in behind the last. One-shot.
  kDraw,     // The stroke retraces itself. One-shot.
  kBreathe,  // Scale and alpha swell. Continuous.
  kPulse,    // Alpha only. Continuous.
  kOrbit,    // Every subpath but the first turns. Continuous.
  kShimmer,  // A bright band travels across the strokes. One-shot.
};

// Whether the effect loops for as long as the cursor stays.
bool IsContinuousEffect(Effect effect);

// One cycle of the effect.
base::TimeDelta EffectDuration(Effect effect);

// One pickable workspace icon.
struct WorkspaceIcon {
  // Stable identifier, persisted in ZephyrusWorkspace::emoji.
  //
  // Reusing that field rather than adding one is deliberate: it is already a
  // string, already persisted, and already round-trips through the pref. A
  // value that does not match any key here is still treated as an emoji, so
  // every workspace that predates this set keeps rendering exactly as before
  // and no migration is needed.
  const char* key;
  // Shown in the picker's tooltip.
  const char16_t* label;
  Shape shape;
  Effect effect;
  // Unused by the painter today -- the icon takes the workspace's own colour.
  // Kept because it is the hue the set was designed around, and dropping it
  // would mean re-deriving it if per-icon tinting ever lands.
  int hue;
  // SVG path data in a 24x24 box, STROKED (never filled). Circles and rects
  // from the source set were rewritten as arcs and rounded rects so the whole
  // glyph is one parseable string.
  const char* path_data;
};

// Every icon in the set, in picker order.
base::span<const WorkspaceIcon> AllWorkspaceIcons();

// The icon `key` names, or null when it names none -- which is the signal that
// the stored string is an emoji rather than an icon key.
const WorkspaceIcon* FindWorkspaceIcon(std::string_view key);
const WorkspaceIcon* FindWorkspaceIcon(const std::u16string& key);

// A spring, as Material 3 Expressive specifies motion.
//
// Ported from the source set's springToCss(): it replaced duration+easing with
// springs, and gfx::Tween has no spring or elastic curve to approximate one
// with. Since the closed form is three lines, porting it is both shorter and
// more faithful than picking the nearest bezier.
struct Spring {
  double damping;
  double stiffness;
};

// SPATIAL springs move position, size and shape, and are ALLOWED to overshoot.
// Effect springs (colour, opacity) are damped to 1.0 so they never do -- a
// bouncing opacity reads as a flicker. Only spatial ones are needed here.
inline constexpr Spring kSpringSpatialDefault{0.8, 380.0};
inline constexpr Spring kSpringSpatialFast{0.6, 800.0};

// Position of a unit spring at `t` seconds. May exceed 1 while it rings.
double SpringAt(const Spring& spring, double t);

// How long until the spring has settled to within 0.2%.
double SpringSettleSeconds(const Spring& spring);

// The container silhouette, as a path inscribed in `bounds`.
//
// `morph` runs 0 (a circle) to 1 (the shape in full), and every value between
// is the shape the container wears mid-transition. Interpolating the RADIUS at
// each angle -- rather than tweening two finished paths -- is what keeps the
// outline smooth at every step: both endpoints are sampled from the same
// function, so no vertex ever has to travel to a partner that is somewhere
// else entirely.
SkPath ShapePath(Shape shape, const gfx::RectF& bounds, float morph);

// Strokes `icon` centred in `box`, at `stroke_width` device pixels.
//
// Stroke width is passed rather than derived from the box because a glyph
// scaled linearly to a 20px cell carries a sub-pixel line and turns to mush.
// The caller picks it optically; see kGlyphStrokeFor in the toolbar.
// `progress` runs 0..1 through one cycle of the icon's effect. Pass a NEGATIVE
// value for the resting glyph -- that is the state a cell spends almost all of
// its life in, and it skips the effect machinery entirely.
void PaintWorkspaceGlyph(gfx::Canvas* canvas,
                         const WorkspaceIcon& icon,
                         const gfx::RectF& box,
                         SkColor color,
                         float stroke_width,
                         float progress = -1.f);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_ICONS_H_

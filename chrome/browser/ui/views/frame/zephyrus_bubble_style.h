// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BUBBLE_STYLE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BUBBLE_STYLE_H_

#include "third_party/skia/include/core/SkColor.h"

namespace views {
class BubbleDialogDelegate;
}

#include "chrome/browser/ui/zephyrus_version.h"

namespace zephyrus {

// zephyrus::kVersion is defined in zephyrus_version.h (included above), kept in
// chrome/browser/ui so the settings WebUI can reach it too.

// The radius every Zephyrus card surface uses — bubbles, dialogs, rows,
// buttons, and the omnibox results card.
inline constexpr int kCornerRadius = 10;

// Shared accent. Used for the Shield's glyph and toggle tracks, the settings
// rail, and omnibox selection.
inline constexpr SkColor kAccent = SkColorSetRGB(0x8B, 0x5C, 0xF6);

// Zephyrus bubble chrome, in the two phases the Views API forces on us.
//
// Applying this is NOT optional styling: the frame's default 1px border stroke
// comes from a themed color that renders as a hard outline around the card,
// and separation is meant to come from the drop shadow instead.
//
// Phase 1 — before the widget exists. `set_corner_radius()` only writes into
// the delegate's params, which the frame reads once at creation, so calling it
// afterwards silently does nothing.
void ConfigureBubble(views::BubbleDialogDelegate* bubble);

// Phase 2 — after the widget exists, because the frame view is created with
// it. For a BubbleDialogDelegateView subclass call this from
// OnWidgetInitialized(); for a plain delegate, right after CreateBubble*().
void ApplyBubbleFrame(views::BubbleDialogDelegate* bubble);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BUBBLE_STYLE_H_

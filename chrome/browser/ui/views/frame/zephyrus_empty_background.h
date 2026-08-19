// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_EMPTY_BACKGROUND_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_EMPTY_BACKGROUND_H_

namespace views {
class View;
}

namespace zephyrus {

// Zephyrus windows have no new-tab page, so a window with no tabs shows nothing
// at all in its content area. Instead of leaving a flat void there, this paints
// the user's desktop wallpaper, blurred and dimmed, behind the web contents —
// so an empty window reads as a piece of the desktop showing through.
//
// This is a painted background, NOT window transparency: real per-pixel alpha
// would render solid black while DirectComposition is disabled on AMD (the same
// root cause as the black popup borders). The blur is done once on the decoded
// bitmap, so painting stays cheap.
//
// Safe to call before the wallpaper has loaded: the first paint uses the flat
// fallback colour and the view is repainted once the image arrives.
void InstallEmptyWindowBackground(views::View* view);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_EMPTY_BACKGROUND_H_

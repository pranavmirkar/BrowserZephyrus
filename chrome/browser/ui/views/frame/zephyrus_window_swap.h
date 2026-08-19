// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WINDOW_SWAP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WINDOW_SWAP_H_

class Browser;

namespace gfx {
class Rect;
}

// Zephyrus seamless window swap. Shared by the Private Workspace and the
// in-window Profile switcher: both keep two profile-bound Browser windows and
// make switching read as *one window changing content* by having the incoming
// window take over the outgoing window's exact footprint, then hiding the
// outgoing one. Centralized here so the flicker/seam fixes live in one place.
namespace zephyrus {

// Suppresses the OS minimize/maximize/restore animation on `browser`'s window so
// the bounds-takeover below does not read as a zooming window. Persistent
// per-window attribute; idempotent. No-op off Windows / if the window is gone.
void DisableWindowTransitions(Browser* browser);

// Moves `browser`'s window onto `bounds` (or maximizes it) and brings it
// forward. Only changes window state when it actually differs — re-maximizing an
// already-maximized (merely hidden) window forces a full non-client frame
// recompute (WM_NCCALCSIZE) that flashes the whole window, so the redundant call
// is skipped.
void ShowWindowAt(Browser* browser, const gfx::Rect& bounds, bool maximized);

// The full swap: show `incoming` at `bounds`/`maximized` taking over the
// footprint, then hide `outgoing`. `incoming` is shown first so the outgoing
// window is never uncovered mid-swap. Transitions are disabled on both sides.
void SwapWindows(Browser* incoming,
                 Browser* outgoing,
                 const gfx::Rect& bounds,
                 bool maximized);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WINDOW_SWAP_H_

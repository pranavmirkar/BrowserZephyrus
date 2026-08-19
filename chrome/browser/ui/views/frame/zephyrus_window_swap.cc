// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_window_swap.h"

#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include <dwmapi.h>

#include "ui/views/win/hwnd_util.h"
#endif

namespace zephyrus {
namespace {

views::Widget* WidgetFor(Browser* browser) {
  if (!browser) {
    return nullptr;
  }
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
  return view ? view->GetWidget() : nullptr;
}

}  // namespace

void DisableWindowTransitions(Browser* browser) {
#if BUILDFLAG(IS_WIN)
  views::Widget* widget = WidgetFor(browser);
  if (!widget) {
    return;
  }
  if (HWND hwnd = views::HWNDForNativeWindow(widget->GetNativeWindow())) {
    const BOOL disable = TRUE;
    ::DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disable,
                            sizeof(disable));
  }
#endif
}

void ShowWindowAt(Browser* browser, const gfx::Rect& bounds, bool maximized) {
  views::Widget* widget = WidgetFor(browser);
  if (!widget) {
    return;
  }
  // Take over the other window's footprint so the swap reads as one window
  // changing rather than two windows trading places. Only change window state
  // when it actually differs: re-maximizing an already-maximized (merely
  // hidden) window forces a full non-client frame recompute (WM_NCCALCSIZE),
  // which flashes the whole window on exit even with DWM transitions disabled.
  if (maximized) {
    if (!widget->IsMaximized()) {
      widget->Maximize();
    }
  } else {
    if (widget->IsMaximized()) {
      widget->Restore();
    }
    widget->SetBounds(bounds);
  }
  widget->Show();
  widget->Activate();
}

void SwapWindows(Browser* incoming,
                 Browser* outgoing,
                 const gfx::Rect& bounds,
                 bool maximized) {
  // Kill the window-zoom animation on both sides so the swap reads as one
  // window changing content, not two windows trading places.
  DisableWindowTransitions(incoming);
  DisableWindowTransitions(outgoing);
  // Show the incoming window first (it covers the outgoing one at the same
  // bounds), then hide the outgoing window underneath so nothing is uncovered.
  ShowWindowAt(incoming, bounds, maximized);
  if (views::Widget* outgoing_widget = WidgetFor(outgoing)) {
    outgoing_widget->Hide();
  }
}

}  // namespace zephyrus

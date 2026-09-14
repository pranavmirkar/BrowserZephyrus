// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WINDOW_BACKDROP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WINDOW_BACKDROP_H_

namespace views { class Widget; }
namespace zephyrus {
// Set on a browser HWND only after DWM accepts the backdrop. An HWND property
// keeps this state local to the native window, including separate profiles.
inline constexpr wchar_t kWindowBackdropProperty[] = L"Zephyrus.WindowBackdrop";
bool HasWindowBackdrop(const views::Widget* widget);
}  // namespace zephyrus
#endif

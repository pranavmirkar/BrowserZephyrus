// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chrome/browser/ui/views/frame/zephyrus_window_backdrop.h"

#include "build/build_config.h"
#include "ui/views/widget/widget.h"
#if BUILDFLAG(IS_WIN)
#include <windows.h>
#include "ui/views/win/hwnd_util.h"
#endif

namespace zephyrus {
bool HasWindowBackdrop(const views::Widget* widget) {
#if BUILDFLAG(IS_WIN)
  return widget && widget->GetNativeWindow() && !widget->IsFullscreen() &&
         ::GetPropW(views::HWNDForWidget(widget), kWindowBackdropProperty);
#else
  return false;
#endif
}
}  // namespace zephyrus

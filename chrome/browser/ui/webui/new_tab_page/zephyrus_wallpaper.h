// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_NEW_TAB_PAGE_ZEPHYRUS_WALLPAPER_H_
#define CHROME_BROWSER_UI_WEBUI_NEW_TAB_PAGE_ZEPHYRUS_WALLPAPER_H_

#include "base/functional/callback_forward.h"
#include "base/memory/ref_counted_memory.h"

namespace zephyrus {

// Reads the current Windows desktop wallpaper image off the disk on a blocking
// thread pool sequence, then runs `callback` on the caller's sequence with the
// raw file bytes (any of jpeg/png/bmp — the image decoder sniffs the real
// format). Runs the callback with nullptr on any failure (no wallpaper set, a
// solid-colour desktop, a slideshow with no cached file, or a read error), in
// which case the new-tab page falls back to its dark background.
//
// Windows-only: on other platforms this immediately replies with nullptr.
void ReadDesktopWallpaperBytes(
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> callback);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_WEBUI_NEW_TAB_PAGE_ZEPHYRUS_WALLPAPER_H_

// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/new_tab_page/zephyrus_wallpaper.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#endif  // BUILDFLAG(IS_WIN)

namespace zephyrus {

namespace {

#if BUILDFLAG(IS_WIN)

// Runs on a MayBlock() thread pool sequence.
scoped_refptr<base::RefCountedMemory> ReadWallpaperOnBlockingSequence() {
  // Primary source: the path the user actually set.
  wchar_t buffer[MAX_PATH] = {0};
  base::FilePath path;
  if (::SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, buffer, 0) &&
      buffer[0] != L'\0') {
    path = base::FilePath(buffer);
  }

  // Fallback: Windows caches the active wallpaper (even for themes/slideshows)
  // as a transcoded JPEG under the roaming profile.
  if (path.empty() || !base::PathExists(path)) {
    base::FilePath appdata;
    if (base::PathService::Get(base::DIR_ROAMING_APP_DATA, &appdata)) {
      base::FilePath transcoded = appdata.Append(L"Microsoft")
                                      .Append(L"Windows")
                                      .Append(L"Themes")
                                      .Append(L"TranscodedWallpaper");
      if (base::PathExists(transcoded)) {
        path = transcoded;
      }
    }
  }

  if (path.empty() || !base::PathExists(path)) {
    return nullptr;
  }

  // Bound the read. The path comes from HKCU\Control Panel\Desktop\Wallpaper,
  // which any process running as this user can point at any readable file, and
  // this runs on EVERY new-tab load. Without a cap, one registry write turns
  // opening a tab into an arbitrary-size allocation in the browser process.
  //
  // 64 MB is far above any real wallpaper (a 4K JPEG is single-digit MB) and
  // far below anything that threatens the process.
  // The cap is enforced by the READ itself, not by a stat beforehand: a
  // size check followed by an unbounded read leaves a window in which the file
  // is swapped for a larger one, and the whole point here is that the path is
  // attacker-influencable.
  static constexpr size_t kMaxWallpaperBytes = 64u * 1024 * 1024;
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents,
                                         kMaxWallpaperBytes) ||
      contents.empty()) {
    return nullptr;
  }
  return base::MakeRefCounted<base::RefCountedBytes>(
      base::as_byte_span(contents));
}

#endif  // BUILDFLAG(IS_WIN)

}  // namespace

void ReadDesktopWallpaperBytes(
    base::OnceCallback<void(scoped_refptr<base::RefCountedMemory>)> callback) {
#if BUILDFLAG(IS_WIN)
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadWallpaperOnBlockingSequence), std::move(callback));
#else
  std::move(callback).Run(nullptr);
#endif  // BUILDFLAG(IS_WIN)
}

}  // namespace zephyrus

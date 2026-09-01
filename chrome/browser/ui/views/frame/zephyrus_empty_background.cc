// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_empty_background.h"

#include "chrome/browser/ui/views/frame/browser_view.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "skia/ext/image_operations.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/background.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/base_paths.h"
#include "base/path_service.h"
#endif

namespace zephyrus {

namespace {

// The flat colour behind everything: what a window shows before the wallpaper
// resolves, and permanently if there is none to read. Tracks the one permanent
// theme colour rather than repeating it, so a palette change cannot leave this
// surface behind -- which is exactly what happened when the theme went from
// navy to cream and this constant stayed #0E1123.
SkColor Fallback() {
  return zephyrus::Ground();
}

// Wash applied over the wallpaper so it stays a backdrop rather than a picture.
//
// This LIGHTENS. It used to be a near-black dim, which was right when the ink
// over it was white; the warm light theme inks in wine, so a dark wash would
// bury the very text it exists to make legible. Same purpose, opposite
// direction -- the wallpaper is pushed toward the cream ground instead of away
// from it.
// Wash over the wallpaper. Pushes it toward the ground in BOTH themes, so
// it lightens on light and darkens on dark -- a fixed cream wash would have
// been a bright rectangle in a dark browser.
SkColor Scrim() {
  return SkColorSetA(zephyrus::Ground(), 0xC4);
}

// The blur is produced by shrinking the bitmap and letting the GPU scale it
// back up with linear filtering — far cheaper than a real blur filter, and
// visually identical at this radius. Smaller divisor = softer.
constexpr int kBlurDivisor = 22;
constexpr int kMinBlurEdge = 12;

#if BUILDFLAG(IS_WIN)
std::optional<base::FilePath> WallpaperPath() {
  wchar_t buffer[MAX_PATH] = {0};
  if (::SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, buffer, 0) &&
      buffer[0] != L'\0') {
    base::FilePath path(buffer);
    if (base::PathExists(path)) {
      return path;
    }
  }
  // Themes and slideshows leave the active image only in this cache.
  base::FilePath appdata;
  if (base::PathService::Get(base::DIR_ROAMING_APP_DATA, &appdata)) {
    base::FilePath transcoded = appdata.Append(L"Microsoft")
                                    .Append(L"Windows")
                                    .Append(L"Themes")
                                    .Append(L"TranscodedWallpaper");
    if (base::PathExists(transcoded)) {
      return transcoded;
    }
  }
  return std::nullopt;
}
#endif  // BUILDFLAG(IS_WIN)

// Runs on a blocking sequence: read, decode, and pre-blur once.
gfx::ImageSkia LoadBlurredWallpaper() {
#if BUILDFLAG(IS_WIN)
  std::optional<base::FilePath> path = WallpaperPath();
  if (!path) {
    return gfx::ImageSkia();
  }
  std::optional<std::vector<uint8_t>> bytes = base::ReadFileToBytes(*path);
  if (!bytes || bytes->empty()) {
    return gfx::ImageSkia();
  }

  SkBitmap decoded = gfx::JPEGCodec::Decode(*bytes);
  if (decoded.isNull()) {
    decoded = gfx::PNGCodec::Decode(*bytes);
  }
  if (decoded.isNull() || decoded.width() <= 0 || decoded.height() <= 0) {
    return gfx::ImageSkia();
  }

  const int w = std::max(kMinBlurEdge, decoded.width() / kBlurDivisor);
  const int h = std::max(kMinBlurEdge, decoded.height() / kBlurDivisor);
  SkBitmap small = skia::ImageOperations::Resize(
      decoded, skia::ImageOperations::RESIZE_GOOD, w, h);
  if (small.isNull()) {
    return gfx::ImageSkia();
  }
  small.setImmutable();
  return gfx::ImageSkia::CreateFrom1xBitmap(small);
#else
  return gfx::ImageSkia();
#endif
}

// Process-wide cache: every window paints the same desktop.
class WallpaperCache {
 public:
  static WallpaperCache& Get() {
    static base::NoDestructor<WallpaperCache> instance;
    return *instance;
  }

  const gfx::ImageSkia& image() const { return image_; }

  // Kicks off the one-time load, repainting `view` when it lands. views::View
  // has no weak pointers, so a ViewTracker carries it across the hop and nulls
  // itself out if the window is torn down first.
  void EnsureLoaded(views::View* view) {
    if (loaded_ || pending_) {
      return;
    }
    pending_ = true;
    auto tracker = std::make_unique<views::ViewTracker>(view);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&LoadBlurredWallpaper),
        base::BindOnce(&WallpaperCache::OnLoaded, base::Unretained(this),
                       std::move(tracker)));
  }

 private:
  friend class base::NoDestructor<WallpaperCache>;
  WallpaperCache() = default;

  void OnLoaded(std::unique_ptr<views::ViewTracker> tracker,
                gfx::ImageSkia image) {
    pending_ = false;
    loaded_ = true;
    image_ = std::move(image);
    if (tracker->view()) {
      tracker->view()->SchedulePaint();
    }
  }

  gfx::ImageSkia image_;
  bool loaded_ = false;
  bool pending_ = false;
};

class ZephyrusEmptyBackground : public views::Background {
 public:
  ZephyrusEmptyBackground() = default;

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    const gfx::Rect bounds = view->GetLocalBounds();
    canvas->FillRect(bounds, Fallback());

    WallpaperCache& cache = WallpaperCache::Get();
    cache.EnsureLoaded(view);
    const gfx::ImageSkia& image = cache.image();
    if (image.isNull() || bounds.IsEmpty()) {
      return;
    }

    // Cover: scale so the image fills the view, cropping the overflow, so the
    // wallpaper keeps its aspect ratio however the window is shaped.
    const float scale =
        std::max(static_cast<float>(bounds.width()) / image.width(),
                 static_cast<float>(bounds.height()) / image.height());
    const int dest_w = static_cast<int>(image.width() * scale) + 1;
    const int dest_h = static_cast<int>(image.height() * scale) + 1;
    canvas->DrawImageInt(image, 0, 0, image.width(), image.height(),
                         bounds.x() + (bounds.width() - dest_w) / 2,
                         bounds.y() + (bounds.height() - dest_h) / 2, dest_w,
                         dest_h, /*filter=*/true);
    canvas->FillRect(bounds, Scrim());
  }
};

}  // namespace

void InstallEmptyWindowBackground(views::View* view) {
  if (!view) {
    return;
  }
  view->SetBackground(std::make_unique<ZephyrusEmptyBackground>());
}

}  // namespace zephyrus

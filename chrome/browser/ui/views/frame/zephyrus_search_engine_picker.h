// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SEARCH_ENGINE_PICKER_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SEARCH_ENGINE_PICKER_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/gfx/image/image_skia.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

class Profile;
class TemplateURL;

namespace favicon_base {
struct FaviconImageResult;
}

namespace views {
class LabelButton;
class View;
}

namespace zephyrus {

// The short name of the profile's current default engine ("DuckDuckGo"), or an
// empty string if there is none. Both search surfaces label themselves with
// this, so they always name the engine that will actually run the query.
std::u16string GetDefaultSearchEngineName(Profile* profile);

// The bundled mark for a preset engine (Google, Bing, Yahoo, DuckDuckGo,
// Brave), matched by prepopulate id; an empty image for anything else. Shipped
// with the browser so the picker and the omnibox show real marks from first
// launch, before the favicon database has ever seen these sites -- and with
// no network request to get them.
gfx::ImageSkia GetBundledEngineIcon(const TemplateURL* engine);

}  // namespace zephyrus

// Lets the user change the default search engine from the search UI instead of
// digging through Settings.
//
// Picking an engine sets the profile default and PERSISTS it — the choice
// applies to the omnibox, the Ctrl+T search card, and right-click search alike.
// It is deliberately not a per-query override: one visible setting, one source
// of truth, and the surfaces can honestly name the engine they will use. The
// cost is that someone wanting a single off-default search changes their
// default, which is why every entry point labels itself with the current engine
// rather than presenting an anonymous chooser.
//
// The list comes from TemplateURLService (filtered by ShowInDefaultList), so it
// matches Settings exactly and includes engines the user added themselves.
class ZephyrusSearchEnginePicker : public views::BubbleDialogDelegateView {
  METADATA_HEADER(ZephyrusSearchEnginePicker, views::BubbleDialogDelegateView)

 public:
  // Runs when the picker closes, however it closes — chosen, dismissed, or
  // destroyed with its parent. Callers use it to refresh their label AND to
  // undo whatever they did to stay alive while it was open, so it must fire on
  // every path; it is invoked from the destructor for that reason.
  using FinishedCallback = base::OnceClosure;

  ZephyrusSearchEnginePicker(Profile* profile, views::View* anchor,
                             FinishedCallback on_finished);
  ZephyrusSearchEnginePicker(const ZephyrusSearchEnginePicker&) = delete;
  ZephyrusSearchEnginePicker& operator=(const ZephyrusSearchEnginePicker&) =
      delete;
  ~ZephyrusSearchEnginePicker() override;

  // Shows the picker anchored to `anchor`. Ignored if one is already open, so a
  // double-click cannot stack two.
  static void Show(Profile* profile, views::View* anchor,
                   FinishedCallback on_finished);

  // views::WidgetDelegate:
  void OnWidgetInitialized() override;

 private:
  // Makes `engine` the profile default and closes.
  void Choose(const std::u16string& keyword);

  // Applies a fetched favicon to `row`. Weakly bound: favicon lookups are async
  // and routinely outlive a bubble the user dismissed immediately.
  void OnFaviconReady(views::LabelButton* row,
                      const favicon_base::FaviconImageResult& result);

  const raw_ptr<Profile> profile_;
  FinishedCallback on_finished_;

  // Destroying this cancels every in-flight favicon lookup, so no callback can
  // land on a destroyed row.
  base::CancelableTaskTracker favicon_tracker_;
  base::WeakPtrFactory<ZephyrusSearchEnginePicker> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SEARCH_ENGINE_PICKER_H_

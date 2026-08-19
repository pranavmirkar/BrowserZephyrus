// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZEPHYRUS_ENGINE_PILL_H_
#define CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZEPHYRUS_ENGINE_PILL_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_service_observer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/label_button.h"

class Profile;

namespace favicon_base {
struct FaviconImageResult;
}

// A pill inside the omnibox naming the current search engine; clicking it opens
// the engine picker.
//
// This exists as its own control rather than as a click handler on the leading
// magnifier. That was tried first and is wrong: the magnifier IS the security
// indicator on a settled page, where clicking it must open Page Info — the only
// route to permissions and cookie controls. Scoping the picker to the
// editing/empty state avoided the conflict but made the picker unreachable
// exactly when a user is looking at a page and wondering which engine they are
// about to search with. A separate pill has one meaning and never competes.
//
// The pill keeps itself current by observing TemplateURLService, so changing the
// engine anywhere — this picker, the Ctrl+T card, or Settings — relabels it.
class ZephyrusEnginePill : public views::LabelButton,
                           public TemplateURLServiceObserver {
  METADATA_HEADER(ZephyrusEnginePill, views::LabelButton)

 public:
  explicit ZephyrusEnginePill(Profile* profile);
  ZephyrusEnginePill(const ZephyrusEnginePill&) = delete;
  ZephyrusEnginePill& operator=(const ZephyrusEnginePill&) = delete;
  ~ZephyrusEnginePill() override;

  // TemplateURLServiceObserver:
  void OnTemplateURLServiceChanged() override;
  void OnTemplateURLServiceShuttingDown() override;

 private:
  void OpenPicker();
  void Refresh();
  void OnFaviconReady(const favicon_base::FaviconImageResult& result);

  const raw_ptr<Profile> profile_;
  base::ScopedObservation<TemplateURLService, TemplateURLServiceObserver>
      observation_{this};
  // Cancels any in-flight favicon lookup when this view goes away, and when the
  // engine changes before the previous lookup lands.
  base::CancelableTaskTracker favicon_tracker_;
  base::WeakPtrFactory<ZephyrusEnginePill> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZEPHYRUS_ENGINE_PILL_H_

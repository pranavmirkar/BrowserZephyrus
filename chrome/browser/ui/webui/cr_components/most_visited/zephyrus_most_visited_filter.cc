// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/cr_components/most_visited/zephyrus_most_visited_filter.h"

#include "base/no_destructor.h"
#include "url/gurl.h"

namespace zephyrus {

namespace {

MostVisitedTileFilter& Filter() {
  static base::NoDestructor<MostVisitedTileFilter> filter;
  return *filter;
}

}  // namespace

void SetMostVisitedTileFilter(MostVisitedTileFilter filter) {
  Filter() = std::move(filter);
}

bool KeepMostVisitedTile(content::WebContents* web_contents, const GURL& url) {
  const MostVisitedTileFilter& filter = Filter();
  return !filter || filter.Run(web_contents, url);
}

}  // namespace zephyrus

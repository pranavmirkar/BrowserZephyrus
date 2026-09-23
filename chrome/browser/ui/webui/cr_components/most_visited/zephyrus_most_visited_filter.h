// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CR_COMPONENTS_MOST_VISITED_ZEPHYRUS_MOST_VISITED_FILTER_H_
#define CHROME_BROWSER_UI_WEBUI_CR_COMPONENTS_MOST_VISITED_ZEPHYRUS_MOST_VISITED_FILTER_H_

#include "base/functional/callback.h"

class GURL;

namespace content {
class WebContents;
}  // namespace content

namespace zephyrus {

// Decides whether a history-derived most-visited tile may appear on the New
// Tab Page shown in `web_contents`.
//
// A hook rather than a direct call because the workspace store lives in the
// views layer, which this target sits below: the store registers the filter
// when it is created, and the tile handler asks it. With nothing registered
// every tile is kept, which is stock behaviour.
using MostVisitedTileFilter =
    base::RepeatingCallback<bool(content::WebContents* web_contents,
                                 const GURL& url)>;

void SetMostVisitedTileFilter(MostVisitedTileFilter filter);
bool KeepMostVisitedTile(content::WebContents* web_contents, const GURL& url);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_WEBUI_CR_COMPONENTS_MOST_VISITED_ZEPHYRUS_MOST_VISITED_FILTER_H_

// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_CONFIG_H_
#define CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_CONFIG_H_

#include "url/gurl.h"

namespace zephyrus::search {

// The only location of the development API address. A deployment can set
// --zepsearch-api-base-url=https://search.example without changing templates.
GURL GetApiBaseUrl();

}  // namespace zephyrus::search

#endif  // CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_CONFIG_H_

// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license.

#include "chrome/browser/ui/webui/zephyrus/zepsearch_config.h"

#include "base/command_line.h"

namespace zephyrus::search {

namespace {

constexpr char kApiBaseUrlSwitch[] = "zepsearch-api-base-url";
constexpr char kDevelopmentApiBaseUrl[] = "http://192.168.137.1:8000/";

bool IsValidBaseUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.host().empty() &&
         !url.has_username() && !url.has_password() &&
         (url.path().empty() || url.path() == "/") && !url.has_query() &&
         !url.has_ref();
}

}  // namespace

GURL GetApiBaseUrl() {
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kApiBaseUrlSwitch)) {
    GURL configured(command_line->GetSwitchValueASCII(kApiBaseUrlSwitch));
    // Fail closed on a mistyped override; silently using a different backend
    // would send searches to an address the user did not choose.
    return IsValidBaseUrl(configured) ? configured : GURL();
  }
  return GURL(kDevelopmentApiBaseUrl);
}

}  // namespace zephyrus::search

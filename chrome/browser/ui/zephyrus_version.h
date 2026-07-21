// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ZEPHYRUS_VERSION_H_
#define CHROME_BROWSER_UI_ZEPHYRUS_VERSION_H_

namespace zephyrus {

// Zephyrus product version — independent of the underlying Chromium engine
// version (chrome/VERSION). Bump per release: PATCH for fixes, MINOR for
// features, MAJOR for big changes. Lives in chrome/browser/ui (not under
// views/) so both the views UI and the settings WebUI can include it without
// crossing a layering boundary.
inline constexpr char16_t kVersion[] = u"0.1.0";

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_ZEPHYRUS_VERSION_H_

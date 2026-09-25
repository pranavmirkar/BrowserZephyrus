// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_ui_layout.h"

#include "components/prefs/pref_service.h"

namespace zephyrus {

UiLayout GetUiLayout(const PrefService* prefs) {
  if (!prefs) {
    return kDefaultUiLayout;
  }
  const int value = prefs->GetInteger(kUiLayoutPref);
  if (value < 0 || value > static_cast<int>(UiLayout::kMaxValue)) {
    return kDefaultUiLayout;
  }
  return static_cast<UiLayout>(value);
}

void SetUiLayout(PrefService* prefs, UiLayout layout) {
  if (prefs) {
    prefs->SetInteger(kUiLayoutPref, static_cast<int>(layout));
  }
}

}  // namespace zephyrus

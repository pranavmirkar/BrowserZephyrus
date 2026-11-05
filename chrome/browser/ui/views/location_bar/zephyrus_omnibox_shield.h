// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZEPHYRUS_OMNIBOX_SHIELD_H_
#define CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZEPHYRUS_OMNIBOX_SHIELD_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"

namespace content {
class WebContents;
}  // namespace content

// The Shield, at the leading end of the omnibox: where the page-info icon was.
// It shows how many requests the blocker stopped on this page as a badge on the
// glyph, and opens the Shield popup, which holds the site info (connection,
// cookies and site data, permissions) as well as the blocker's own controls.
//
// Shown on a settled, ordinary page. While the field is being edited the search
// magnifier takes this spot back, and a page with a security warning keeps its
// warning glyph (which opens the same popup): neither is the shield's to mask.
class ZephyrusOmniboxShield : public views::Button {
  METADATA_HEADER(ZephyrusOmniboxShield, views::Button)

 public:
  explicit ZephyrusOmniboxShield(PressedCallback callback);
  ZephyrusOmniboxShield(const ZephyrusOmniboxShield&) = delete;
  ZephyrusOmniboxShield& operator=(const ZephyrusOmniboxShield&) = delete;
  ~ZephyrusOmniboxShield() override;

  // Follows the active tab's blocked count. Null clears it.
  void SetContents(content::WebContents* contents);

  // The bar's own ink, so the glyph reads like the magnifier it replaces.
  void SetInk(SkColor ink);

  // views::View:
  void OnThemeChanged() override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  void UpdateCount();

  int count_ = 0;
  SkColor ink_ = SK_ColorGRAY;
  raw_ptr<content::WebContents> contents_ = nullptr;
  base::CallbackListSubscription subscription_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_LOCATION_BAR_ZEPHYRUS_OMNIBOX_SHIELD_H_

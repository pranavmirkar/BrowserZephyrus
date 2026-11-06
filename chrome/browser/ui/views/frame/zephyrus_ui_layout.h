// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_UI_LAYOUT_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_UI_LAYOUT_H_

class PrefService;

namespace zephyrus {

// Where the tabs live. One choice for the whole profile, picked from the
// three-dots menu; every window follows it live.
//
// Stored as an int, so values are APPEND ONLY: a renumbering would silently
// move every user onto the other layout.
enum class UiLayout : int {
  // The CLIPPING sidebar. It replaces the floating-card layout (the stored value
  // is the same, so those users land here). The same flush column as classic,
  // but the page is never resized or reflowed by it: revealed by hover it
  // covers the page's leading edge, pinned it docks beside the page. Compact
  // mode (the title-bar pin) works as in classic, and the hidden title bar
  // clips the same way: a hover reveal slides it over the page's top edge.
  kClippingSidebar = 0,
  // Tabs in a strip under the title bar. The title-bar pin only hides the
  // title bar; nothing is lent anywhere.
  kHorizontalTabs = 1,
  // The original Zephyrus sidebar, and the DEFAULT: a flush column of window
  // chrome on the leading edge that pushes the page aside whenever it is out.
  // Kept so the newer layouts are a choice rather than a replacement -- nobody
  // loses the UI they had.
  kClassicSidebar = 2,
  // ALL IN ONE STRIP (Safari's Compact). The title bar IS the tab strip: back
  // and forward, then the tabs as favicon chips, with the ACTIVE tab expanded
  // into the address field, then the actions and the window controls, all in
  // one row. There is no sidebar and no second row, and because the row holds
  // the tabs the title bar cannot be hidden.
  kOneStrip = 3,
  kMaxValue = kOneStrip,
};

inline constexpr UiLayout kDefaultUiLayout = UiLayout::kClassicSidebar;

// Registered in browser_prefs.cc as a literal (layering), so keep in step.
inline constexpr char kUiLayoutPref[] = "zephyrus.ui_layout";

// Clamped: an out-of-range stored value reads as the default rather than as
// a layout that does not exist.
UiLayout GetUiLayout(const PrefService* prefs);
void SetUiLayout(PrefService* prefs, UiLayout layout);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_UI_LAYOUT_H_

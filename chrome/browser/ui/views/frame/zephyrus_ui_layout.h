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
  // Tabs in a panel on the leading edge. Hover reveals it over the page; the
  // pin docks it beside the page. The title-bar pin lends the toolbar's
  // controls to the panel (compact mode).
  kFloatingSidebar = 0,
  // Tabs in a strip under the title bar. The title-bar pin only hides the
  // title bar; nothing is lent anywhere.
  kHorizontalTabs = 1,
  // The original Zephyrus sidebar, and the DEFAULT: a flush column of window
  // chrome on the leading edge that pushes the page aside whenever it is out.
  // Kept so the two newer layouts are a choice rather than a replacement --
  // nobody loses the UI they had.
  kClassicSidebar = 2,
  kMaxValue = kClassicSidebar,
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

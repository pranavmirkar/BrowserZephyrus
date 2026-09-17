// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PRIVACY_POPUP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PRIVACY_POPUP_H_

namespace views {
class View;
}

class Browser;

namespace zephyrus_privacy {

// Privacy Intelligence panel, §6.1 (summary) and §6.2 (per-tracker rows).
//
// **Every number here traces to a row, and every string to a status.** The two
// scores come from privacy_scores.h, which is a pure function of counts this
// panel also displays, so the expandable breakdown cannot disagree with the
// score above it. The per-tracker statuses come from TrackerStatus, and each
// one maps to exactly one translated string — see the §2.4 comment block in
// generated_resources.grd. Nothing on this panel is computed here.
//
// **Shown as an M3 modal SIDE SHEET**, not a popup: it slides in from the
// window's trailing edge over a scrim, holds the summary and the per-tracker
// breakdown in one scrolling column, and closes on its close button, Escape, a
// click on the scrim, or a change of active tab (the analysis belongs to one
// page). The information architecture and the accuracy rules are unchanged;
// only the host and the components are M3.
//
// Does nothing when the feature is off or the profile has no service
// (incognito), which is a legitimate state and not an error — see
// PrivacyIntelligenceServiceFactory. `anchor` is the control the user came
// from; the sheet is not attached to it, and it is kept so callers need not
// change.
void ShowPrivacyPopup(Browser* browser, views::View* anchor);

// The open sheet's surface in `browser`'s window, or null. For tests: the sheet
// is a view inside the browser window, not a widget, so a widget observer does
// not see it open.
views::View* GetPrivacySheetForTesting(Browser* browser);

// Removes the open sheet at once, without its exit animation. For tests.
void ClosePrivacySheetForTesting(Browser* browser);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PRIVACY_POPUP_H_

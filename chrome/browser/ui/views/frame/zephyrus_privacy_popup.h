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
// **The design is provisional.** The Figma for this screen does not exist yet;
// what is settled is the information architecture and the accuracy rules, and
// those are what the code enforces. The visual treatment reuses the Shield
// panel's material so it does not read as a foreign surface in the meantime,
// and is expected to be replaced wholesale.
//
// Shows the panel anchored to `anchor`. Does nothing when the feature is off or
// the profile has no service (incognito), which is a legitimate state and not
// an error — see PrivacyIntelligenceServiceFactory.
void ShowPrivacyPopup(Browser* browser, views::View* anchor);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PRIVACY_POPUP_H_

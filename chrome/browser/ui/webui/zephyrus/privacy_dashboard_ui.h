// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_PRIVACY_DASHBOARD_UI_H_
#define CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_PRIVACY_DASHBOARD_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace zephyrus_privacy {

class PrivacyDashboardUI;

class PrivacyDashboardUIConfig
    : public content::DefaultWebUIConfig<PrivacyDashboardUI> {
 public:
  PrivacyDashboardUIConfig();
};

// chrome://privacy — the user-facing dashboard.
//
// **Not the same page as chrome://privacy-internals, and the difference is the
// point.** The internals page is diagnostics and is forbidden from showing
// domains, sites or entities (§13.4): it is the page a support conversation
// asks a stranger to open, and it must be safe to screenshot. This one is the
// user's own record and necessarily names sites and companies — §6.4 cannot
// claim "this company followed you across these sites" without saying which.
// Keeping them as two hosts is what keeps that boundary enforceable.
//
// **No JavaScript and no packaged resources**, same as the internals page. The
// HTML is generated in the browser process and served through a request
// filter, which sidesteps the "page script 404s in an official build unless it
// is listed in optimize_webui_in_files" failure mode, needs no grit entry, and
// leaves no script for a CSP or Trusted Types policy to argue with.
//
// Everything shown is read asynchronously — the database and the entity dataset
// are both off the UI thread (§8.9) — and rendered server-side once the data
// arrives.
class PrivacyDashboardUI : public content::WebUIController {
 public:
  explicit PrivacyDashboardUI(content::WebUI* web_ui);
  PrivacyDashboardUI(const PrivacyDashboardUI&) = delete;
  PrivacyDashboardUI& operator=(const PrivacyDashboardUI&) = delete;
  ~PrivacyDashboardUI() override;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_PRIVACY_DASHBOARD_UI_H_

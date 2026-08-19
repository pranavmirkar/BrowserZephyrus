// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_PRIVACY_INTERNALS_UI_H_
#define CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_PRIVACY_INTERNALS_UI_H_

#include "content/public/browser/webui_config.h"
#include "content/public/browser/web_ui_controller.h"

namespace zephyrus_privacy {

class PrivacyInternalsUI;

// Deliberately NOT a DefaultInternalWebUIConfig. Chromium hides those behind
// the chrome://chrome-urls debug-pages toggle, which is right for a page that
// might expose internals — but this one is counters and nothing else (§13.4),
// and it is the page a support conversation would ask a user to open. Gating it
// would buy no privacy and cost every user who needs it three extra steps.
class PrivacyInternalsUIConfig
    : public content::DefaultWebUIConfig<PrivacyInternalsUI> {
 public:
  PrivacyInternalsUIConfig();
};

// chrome://privacy-internals — the diagnostic surface for the Privacy
// Intelligence pipeline (§8.11, §11.1).
//
// **Counters only.** No domains, no sites, no entities, ever. A diagnostic page
// that lists the user's browsing is a worse privacy leak than the problem the
// feature exists to solve (§13.4), and it would be reachable from any
// screenshot or screen-share. Everything rendered here is an integer, an
// enum name, or a boolean.
//
// **No JavaScript and no packaged resources.** The page is generated as a
// string in the browser process and served through a request filter. That
// keeps it immune to the "page script 404s in an official build unless it is
// listed in optimize_webui_in_files" failure mode, needs no grit entry, and
// means there is no script for a CSP or Trusted Types policy to argue with.
// It refreshes itself with a meta tag.
class PrivacyInternalsUI : public content::WebUIController {
 public:
  explicit PrivacyInternalsUI(content::WebUI* web_ui);
  PrivacyInternalsUI(const PrivacyInternalsUI&) = delete;
  PrivacyInternalsUI& operator=(const PrivacyInternalsUI&) = delete;
  ~PrivacyInternalsUI() override;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_PRIVACY_INTERNALS_UI_H_

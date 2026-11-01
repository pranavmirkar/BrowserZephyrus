// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_UI_H_
#define CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace zephyrus::search {

class ZepSearchUI;

class ZepSearchUIConfig : public content::DefaultWebUIConfig<ZepSearchUI> {
 public:
  ZepSearchUIConfig();
};

class ZepSearchUI : public content::WebUIController {
 public:
  explicit ZepSearchUI(content::WebUI* web_ui);
  ZepSearchUI(const ZepSearchUI&) = delete;
  ZepSearchUI& operator=(const ZepSearchUI&) = delete;
  ~ZepSearchUI() override;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

}  // namespace zephyrus::search

#endif  // CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_UI_H_

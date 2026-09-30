// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_SETTINGS_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_SETTINGS_H_

class BrowserView;

namespace views {
class View;
}  // namespace views

namespace zephyrus {

// The agent's model settings: which cloud provider and model, the API key, the
// spending limit per task, and whether the Workspace this window shows may use
// it (ADR 0004: off until the user turns it on here).
//
// The key is written encrypted and never shown again; an empty key field keeps
// the saved one. Anchored to `anchor`, which must be in `browser_view`'s
// window.
void ShowAgentSettings(BrowserView* browser_view, views::View* anchor);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_SETTINGS_H_

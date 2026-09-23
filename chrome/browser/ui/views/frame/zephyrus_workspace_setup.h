// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_SETUP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_SETUP_H_

class BrowserView;

namespace views {
class View;
}  // namespace views

namespace zephyrus {

// The workspace setup card: name, icon, theme colour, light or dark, and --
// for a new workspace -- whether it keeps its own sign-ins.
//
// "+" used to create a workspace on the spot, identical to the others, and
// leave the user to find the icon picker and Customize Chrome afterwards. Now
// it opens this card first, and a workspace arrives already recognisable.
//
// `workspace_id` 0 creates a new workspace; any other id edits that one (the
// strip's "Edit workspace…" item). Anchored to `anchor`, which must be in
// `browser_view`'s window.
void ShowWorkspaceSetup(BrowserView* browser_view,
                        views::View* anchor,
                        int workspace_id);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_SETUP_H_

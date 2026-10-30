// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_SETUP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_SETUP_H_

#include <memory>

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

// The same editor as that card (preview, name, icon, colour, light or dark,
// options), for creating a workspace, to host inline: the onboarding's
// Workspaces screen IS this editor. Null in a window without workspaces.
std::unique_ptr<views::View> CreateWorkspaceEditor(BrowserView* browser_view);

// Creates the workspace an editor from CreateWorkspaceEditor describes.
void CommitWorkspaceEditor(views::View* editor);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_SETUP_H_

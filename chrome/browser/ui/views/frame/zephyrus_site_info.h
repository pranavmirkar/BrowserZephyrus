// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SITE_INFO_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SITE_INFO_H_

namespace content {
class WebContents;
}  // namespace content

namespace views {
class View;
}  // namespace views

// The page's SITE INFO -- what Chromium's page-info bubble shows (is the
// connection secure, cookies and site data, permissions, site settings) -- in
// Material 3 Expressive, for the Shield popup. It reads the same data through
// Chromium's own PageInfo presenter, so what it says is what the page-info
// bubble would say, and every action still goes through the presenter.
namespace zephyrus_site {

// Adds the site info to `content`, the Shield popup's column: a tonal list of
// rows (Connection, Cookies and site data, any permissions the page holds, Site
// settings) in a section of its own, and the sub-pages those rows open. A
// sub-page replaces everything else in `content` until its back arrow is
// pressed.
//
// `width` is the column's width; the column's preferred size is refitted when
// the rows or a sub-page change it, and the bubble follows. Adds nothing for a
// page that has no site info (an internal page, no page at all).
void AttachSiteInfo(views::View* content,
                    content::WebContents* contents,
                    int width);

}  // namespace zephyrus_site

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SITE_INFO_H_

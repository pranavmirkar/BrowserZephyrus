// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §15: "Neither WebUI host is reachable or linkable from web content."
//
// **What this test covers and what it does not.** Chromium's own navigation
// stack is what actually stops `https://evil.test` reaching a `chrome://` URL,
// and that is tested upstream — re-testing it here would assert someone else's
// invariant. What is genuinely ours, and what we could plausibly get wrong, is
// the REGISTRATION: which scheme the config is bound to, and whether the same
// host resolves under a web scheme. A page registered under `chrome-untrusted`
// or reachable at `https://privacy-internals` would be our bug, not Chromium's.
//
// The end-to-end navigation assertion needs a browser test; it is not written
// yet, and this file is deliberately explicit about that rather than implying
// the property is fully covered.

#include "chrome/browser/ui/webui/zephyrus/privacy_internals_ui.h"

#include "chrome/common/webui_url_constants.h"
#include "content/public/common/url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

TEST(PrivacyInternalsUIConfigTest, BoundToTheChromeSchemeOnly) {
  PrivacyInternalsUIConfig config;
  EXPECT_EQ(content::kChromeUIScheme, config.scheme())
      << "the diagnostics page must live on chrome://, not chrome-untrusted:// "
         "and certainly not a web scheme";
  EXPECT_EQ(chrome::kChromeUIZephyrusPrivacyInternalsHost, config.host());
}

// The host string must not be something a site could serve. If this ever became
// a real registrable domain, a page at that name would look like the browser's
// own diagnostics surface.
TEST(PrivacyInternalsUIConfigTest, HostIsNotAWebResolvableName) {
  const std::string host = chrome::kChromeUIZephyrusPrivacyInternalsHost;
  EXPECT_EQ(std::string::npos, host.find('.'))
      << "a dotted host could resolve on the public internet";
  EXPECT_EQ(std::string::npos, host.find('/'));
  EXPECT_FALSE(host.empty());
}

// A URL with our host under a web scheme is a different origin entirely, and
// must not be confusable with the chrome:// one.
TEST(PrivacyInternalsUIConfigTest, WebSchemeUrlIsADifferentOrigin) {
  const GURL internal(std::string(content::kChromeUIScheme) + "://" +
                      chrome::kChromeUIZephyrusPrivacyInternalsHost);
  const GURL web("https://" +
                 std::string(chrome::kChromeUIZephyrusPrivacyInternalsHost));
  ASSERT_TRUE(internal.is_valid());
  EXPECT_NE(internal.DeprecatedGetOriginAsURL(),
            web.DeprecatedGetOriginAsURL());
  EXPECT_EQ(content::kChromeUIScheme, internal.scheme());
}

}  // namespace
}  // namespace zephyrus_privacy

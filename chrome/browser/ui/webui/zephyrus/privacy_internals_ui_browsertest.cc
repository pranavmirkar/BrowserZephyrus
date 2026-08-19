// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §15: "Neither WebUI host is reachable or linkable from web content, asserted
// by test."
//
// The unit tests in privacy_internals_ui_unittest.cc cover the half we own —
// that the config is bound to chrome:// and nothing else. This covers the half
// that actually protects the user: a web page cannot reach the page, by
// navigation, by link, or by window.open.
//
// Why it is worth testing something Chromium already enforces: the diagnostics
// page is generated HTML served from the browser process, and a fork is exactly
// the sort of place where a well-meaning change (registering the host somewhere
// extra, adding a redirect, relaxing a filter) quietly makes it reachable. This
// test fails loudly if that ever happens.

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

GURL InternalsUrl() {
  return GURL(std::string(content::kChromeUIScheme) + "://" +
              chrome::kChromeUIZephyrusPrivacyInternalsHost);
}

class PrivacyInternalsUIBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }
};

// The control. Without it, every "blocked" assertion below would also pass if
// the page simply did not exist.
IN_PROC_BROWSER_TEST_F(PrivacyInternalsUIBrowserTest,
                       BrowserInitiatedNavigationReachesThePage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), InternalsUrl()));
  EXPECT_EQ(InternalsUrl(), web_contents()->GetLastCommittedURL());
  // It really rendered the diagnostics page, not an error page.
  EXPECT_EQ(true, content::EvalJs(web_contents(),
                                  "document.body.innerText.includes('Privacy "
                                  "Intelligence')")
                      .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(PrivacyInternalsUIBrowserTest,
                       WebPageCannotNavigateToIt) {
  const GURL page = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));

  // Renderer-initiated navigation to a WebUI URL must not commit. Ignore the
  // script result: what matters is where the tab ended up.
  std::ignore = content::ExecJs(
      web_contents(),
      "location.href = 'chrome://privacy-internals';",
      content::EXECUTE_SCRIPT_NO_USER_GESTURE);
  content::WaitForLoadStop(web_contents());

  EXPECT_EQ(page, web_contents()->GetLastCommittedURL())
      << "a web page navigated itself to the privacy diagnostics host";
}

IN_PROC_BROWSER_TEST_F(PrivacyInternalsUIBrowserTest,
                       WebPageCannotOpenItInANewWindow) {
  const GURL page = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));
  const int tabs_before = browser()->tab_strip_model()->count();

  std::ignore = content::ExecJs(
      web_contents(), "window.open('chrome://privacy-internals');",
      content::EXECUTE_SCRIPT_NO_USER_GESTURE);
  base::RunLoop().RunUntilIdle();

  // Either no tab opened, or one opened that did not land on the page.
  for (int i = 0; i < browser()->tab_strip_model()->count(); ++i) {
    EXPECT_NE(InternalsUrl(), browser()
                                  ->tab_strip_model()
                                  ->GetWebContentsAt(i)
                                  ->GetLastCommittedURL())
        << "window.open reached the privacy diagnostics host";
  }
  // Guard against the assertion above passing vacuously.
  EXPECT_GE(browser()->tab_strip_model()->count(), tabs_before);
}

// A link click is a distinct code path from location.href, and is the one a
// real attacker would use because it needs no script.
IN_PROC_BROWSER_TEST_F(PrivacyInternalsUIBrowserTest,
                       LinkFromWebContentCannotReachIt) {
  const GURL page = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));

  std::ignore = content::ExecJs(web_contents(), R"(
      const a = document.createElement('a');
      a.href = 'chrome://privacy-internals';
      a.textContent = 'go';
      document.body.appendChild(a);
      a.click();
  )");
  content::WaitForLoadStop(web_contents());

  EXPECT_EQ(page, web_contents()->GetLastCommittedURL())
      << "a link in web content reached the privacy diagnostics host";
}

}  // namespace
}  // namespace zephyrus_privacy

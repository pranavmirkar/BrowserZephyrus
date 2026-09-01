// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Opens the Privacy Intelligence panel for real.
//
// **Why this exists.** Every other Phase 2 test covers the data layer — the
// scores, the tab helper, the page signals, the WebRTC path — and all of them
// passed while the panel itself had a DCHECK failure in its constructor:
// views::LabelButton rejects gfx::ALIGN_TO_HEAD, which only Label accepts. The
// first thing that ever built a PrivacyPanel was a human clicking the button,
// and it took the browser down.
//
// So the point here is not to assert a layout. It is that the panel gets
// CONSTRUCTED under DCHECKs, on each of the states it can be opened in, with
// both layers realised. Views is full of contracts like that one, and none of
// them are visible from a unit test of the model.

#include "chrome/browser/ui/views/frame/zephyrus_privacy_popup.h"

#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/browser/zephyrus/privacy/privacy_tab_helper.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/ui_base_switches.h"
#include "ui/views/view.h"
#include "ui/views/widget/any_widget_observer.h"
#include "ui/views/widget/widget.h"

namespace zephyrus_privacy {
namespace {

class PrivacyPopupBrowserTest : public InProcessBrowserTest {
 public:
  PrivacyPopupBrowserTest() {
    features_.InitAndEnableFeature(kZephyrusPrivacyIntelligence);
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  views::View* anchor() {
    return BrowserView::GetBrowserViewForBrowser(browser())->toolbar();
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Feeds the per-page accumulator directly. The alternative is loading a page
  // with real third parties, which would make the test depend on the network.
  void RecordRequest(std::string_view domain, TrackerStatus status) {
    PrivacyTabHelper::CreateForWebContents(web_contents());
    PrivacyTabHelper::FromWebContents(web_contents())
        ->RecordRequest(domain, status);
  }

  // The panel is built after a hop to the privacy sequence for attribution, so
  // the widget does not exist when ShowPrivacyPopup() returns.
  views::Widget* ShowAndWait() {
    // AnyWidgetObserver rather than NamedWidgetShownWaiter: the bubble is a
    // plain views::BubbleDialogDelegate with no subclass, so it has no
    // distinctive widget name to wait on, and matching the wrong name is
    // indistinguishable from the panel never opening at all.
    views::Widget* shown = nullptr;
    base::RunLoop loop;
    views::AnyWidgetObserver observer{views::test::AnyWidgetTestPasskey{}};
    observer.set_shown_callback(
        base::BindLambdaForTesting([&](views::Widget* widget) {
          shown = widget;
          loop.Quit();
        }));
    ShowPrivacyPopup(browser(), anchor());
    loop.Run();
    return shown;
  }

  base::test::ScopedFeatureList features_;
};

// A page with third parties, some blocked and some not: the state that
// exercises the most of the panel — headline, counts, scores, the arithmetic
// rows, and the quick control.
IN_PROC_BROWSER_TEST_F(PrivacyPopupBrowserTest, OpensOnAPageWithTrackers) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  RecordRequest("ads.example", TrackerStatus::kBlocked);
  RecordRequest("ads.example", TrackerStatus::kBlocked);
  RecordRequest("analytics.example", TrackerStatus::kDetected);
  RecordRequest("cdn.example", TrackerStatus::kAllowed);

  views::Widget* widget = ShowAndWait();
  ASSERT_TRUE(widget);
  EXPECT_FALSE(widget->IsClosed());
  widget->CloseNow();
}

// The empty state. Distinct code path: no rows, and Protection Applied is
// nullopt rather than a percentage, so the panel must render without one.
IN_PROC_BROWSER_TEST_F(PrivacyPopupBrowserTest, OpensOnACleanPage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("clean.test", "/title1.html")));

  views::Widget* widget = ShowAndWait();
  ASSERT_TRUE(widget);
  EXPECT_FALSE(widget->IsClosed());
  widget->CloseNow();
}

// Layer 2 is built in the constructor but starts hidden, so it is realised on
// every open regardless — including the scroll view and every tracker row,
// which is where a per-row Views contract would bite.
IN_PROC_BROWSER_TEST_F(PrivacyPopupBrowserTest, BuildsBothLayersWithManyRows) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("busy.test", "/title1.html")));
  // Past the per-page cap, so the "(other)" tail row is built too.
  for (int i = 0; i < 300; ++i) {
    RecordRequest(base::StringPrintf("d%d.example", i),
                  i % 2 ? TrackerStatus::kBlocked : TrackerStatus::kAllowed);
  }

  views::Widget* widget = ShowAndWait();
  ASSERT_TRUE(widget);
  EXPECT_FALSE(widget->IsClosed());
  widget->CloseNow();
}

// §6.10's partial case: one domain where some requests were blocked and some
// got through. It is a distinct explanation branch — the only one that takes
// four substitutions and the only one that must name a leak — and no other
// test produces a row with `blocked` strictly between zero and `requests`.
IN_PROC_BROWSER_TEST_F(PrivacyPopupBrowserTest, OpensWithAPartiallyBlockedRow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("mixed.test", "/title1.html")));
  RecordRequest("partial.example", TrackerStatus::kBlocked);
  RecordRequest("partial.example", TrackerStatus::kBlocked);
  RecordRequest("partial.example", TrackerStatus::kAllowed);

  views::Widget* widget = ShowAndWait();
  ASSERT_TRUE(widget);
  EXPECT_FALSE(widget->IsClosed());
  widget->CloseNow();
}

// A URL with no registrable domain has no site to describe (§9.7). The panel
// must still open rather than crash on an empty site key.
IN_PROC_BROWSER_TEST_F(PrivacyPopupBrowserTest, OpensOnAPageWithNoSite) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));

  views::Widget* widget = ShowAndWait();
  ASSERT_TRUE(widget);
  EXPECT_FALSE(widget->IsClosed());
  widget->CloseNow();
}

// §14.1: "RTL layout tested for the popup, the tracker list, and the
// dashboard." Alignment constants are the easiest thing in this file to get
// wrong — gfx::ALIGN_TO_HEAD is correct on Label and fatal on LabelButton, and
// that difference is invisible until something builds the panel in an RTL
// locale. This is the test that does.
class PrivacyPopupRtlBrowserTest : public PrivacyPopupBrowserTest {
 public:
  // --lang, not SetICUDefaultLocale() in the constructor: the browser loads its
  // application locale during startup and overwrites anything set beforehand,
  // so the ICU call left IsRTL() false and the test passed without ever
  // exercising RTL. The ASSERT below is what caught that.
  void SetUpCommandLine(base::CommandLine* command_line) override {
    PrivacyPopupBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kLang, "he");
  }
};

IN_PROC_BROWSER_TEST_F(PrivacyPopupRtlBrowserTest, OpensAndLaysOutInRtl) {
  ASSERT_TRUE(base::i18n::IsRTL()) << "the locale did not take; this test "
                                      "would pass without exercising RTL";
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("rtl.test", "/title1.html")));
  RecordRequest("ads.example", TrackerStatus::kBlocked);
  RecordRequest("analytics.example", TrackerStatus::kAllowed);

  views::Widget* widget = ShowAndWait();
  ASSERT_TRUE(widget);
  EXPECT_FALSE(widget->IsClosed());
  // The panel must have real width in RTL too: a mirrored layout that collapses
  // is a layout bug that "it did not crash" would happily miss.
  EXPECT_GT(widget->GetContentsView()->bounds().width(), 0);
  widget->CloseNow();
}

}  // namespace
}  // namespace zephyrus_privacy

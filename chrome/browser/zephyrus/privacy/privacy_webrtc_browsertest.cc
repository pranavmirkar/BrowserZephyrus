// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §16 Phase 2 acceptance, verbatim: "a test page creating an RTCPeerConnection
// is detected and reported, and local addresses are verified withheld under the
// default policy."
//
// Both halves matter and they fail in opposite directions. If detection breaks,
// the panel reports "no trackers detected" on a page that just took the user's
// network address — a §2 false claim. If the policy check breaks, the panel
// says "local addresses were withheld" when they were not, which is worse: an
// unearned promise of protection.
//
// This is a browser test rather than a unit test because neither half exists in
// isolation. Detection runs through content::PeerConnectionTrackerHostObserver,
// which only fires for a real peer connection in a real renderer, and the
// policy is a profile pref read at the moment the event arrives.

#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/browser/zephyrus/privacy/zephyrus_webrtc_privacy_observer_factory.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/peerconnection/webrtc_ip_handling_policy.h"

namespace zephyrus_privacy {
namespace {

class PrivacyWebrtcBrowserTest : public InProcessBrowserTest {
 public:
  PrivacyWebrtcBrowserTest() {
    features_.InitAndEnableFeature(kZephyrusPrivacyIntelligence);
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  PrivacyIntelligenceService* service() {
    return PrivacyIntelligenceServiceFactory::GetForBrowserContext(
        browser()->profile());
  }

  content::WebContents* web_contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Constructs a peer connection and starts ICE gathering. createDataChannel
  // before createOffer is what makes the offer gather candidates at all — an
  // offer with no media and no data channel does nothing observable.
  //
  // A STUN server is configured but never reachable in a test; that is fine,
  // because the event under test is the CONSTRUCTION of the connection, not a
  // successful negotiation.
  [[nodiscard]] bool CreatePeerConnection() {
    return content::ExecJs(web_contents(), R"(
        (async () => {
          const pc = new RTCPeerConnection(
              {iceServers: [{urls: 'stun:stun.l.google.com:19302'}]});
          pc.createDataChannel('zephyrus');
          await pc.setLocalDescription(await pc.createOffer());
          window.__zephyrusPc = pc;  // keep it alive past this scope
        })();
    )");
  }

  // The observer hop is UI-thread synchronous once the renderer reports, but
  // the renderer's report is asynchronous. Poll rather than sleep a fixed
  // amount, so a slow bot does not turn into a flaky failure.
  bool WaitForWebrtcSignal(const GURL& page) {
    for (int attempt = 0; attempt < 100; ++attempt) {
      if (service()->GetPageSignals(page).webrtc_address_requests > 0) {
        return true;
      }
      base::RunLoop loop;
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, loop.QuitClosure(), base::Milliseconds(100));
      loop.Run();
    }
    return false;
  }

  base::test::ScopedFeatureList features_;
};

// §9.2.1's mitigation. Asserted separately from detection because a regression
// here is silent: everything still works, the panel still says the reassuring
// sentence, and local addresses quietly start leaking.
IN_PROC_BROWSER_TEST_F(PrivacyWebrtcBrowserTest, DefaultPolicyWithholdsLocal) {
  // Force the observer to exist. It is created with the browser context in
  // production; asking for it here also proves the factory is registered.
  ASSERT_TRUE(ZephyrusWebrtcPrivacyObserverFactory::GetForBrowserContext(
      browser()->profile()));

  EXPECT_EQ(blink::kWebRTCIPHandlingDefaultPublicInterfaceOnly,
            browser()->profile()->GetPrefs()->GetString(
                prefs::kWebRTCIPHandlingPolicy))
      << "§9.2.1: the default must be the policy that exposes only the public "
         "interface. Chromium's own default exposes more.";
}

// The acceptance criterion itself.
IN_PROC_BROWSER_TEST_F(PrivacyWebrtcBrowserTest,
                       PeerConnectionIsDetectedAndReported) {
  ASSERT_TRUE(service());
  const GURL page = embedded_test_server()->GetURL("a.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));

  // The control. Without it, the assertion below would also pass if the signal
  // were somehow set before the page did anything.
  ASSERT_EQ(0u, service()->GetPageSignals(page).webrtc_address_requests)
      << "a page that has not touched WebRTC must report nothing";

  ASSERT_TRUE(CreatePeerConnection());
  ASSERT_TRUE(WaitForWebrtcSignal(page))
      << "a page constructed an RTCPeerConnection and it was never reported; "
         "the panel would say 'no trackers detected' on a page that just asked "
         "for the user's network address";

  const PrivacyIntelligenceService::PageSignals signals =
      service()->GetPageSignals(page);
  EXPECT_GT(signals.webrtc_address_requests, 0u);
  EXPECT_TRUE(signals.local_addresses_withheld)
      << "under the default policy the panel is entitled to say local "
         "addresses were withheld; if this fails the string becomes a lie";
}

// Attribution comes from the browser's own record of the frame, never from
// anything the renderer says. A page must not be able to make another site's
// record show a WebRTC request.
IN_PROC_BROWSER_TEST_F(PrivacyWebrtcBrowserTest,
                       AttributedToTheSiteThatDidIt) {
  ASSERT_TRUE(service());
  const GURL page = embedded_test_server()->GetURL("a.test", "/title1.html");
  const GURL other = embedded_test_server()->GetURL("b.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));
  ASSERT_TRUE(CreatePeerConnection());
  ASSERT_TRUE(WaitForWebrtcSignal(page));

  EXPECT_EQ(0u, service()->GetPageSignals(other).webrtc_address_requests)
      << "one site's peer connection was attributed to another";
}

// §9.2 attributes to the top-level site: a third-party iframe opening a peer
// connection is something that happened to the user on the page they are
// looking at, and that is the page the panel describes.
IN_PROC_BROWSER_TEST_F(PrivacyWebrtcBrowserTest,
                       SubframeAttributesToTheTopLevelSite) {
  ASSERT_TRUE(service());
  const GURL page =
      embedded_test_server()->GetURL("a.test", "/iframe_blank.html");
  const GURL frame_url =
      embedded_test_server()->GetURL("third-party.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page));
  // "test" is the iframe id in chrome/test/data/iframe_blank.html.
  ASSERT_TRUE(
      content::NavigateIframeToURL(web_contents(), "test", frame_url));

  content::RenderFrameHost* subframe =
      content::ChildFrameAt(web_contents()->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(subframe);
  ASSERT_TRUE(content::ExecJs(subframe, R"(
      (async () => {
        const pc = new RTCPeerConnection();
        pc.createDataChannel('zephyrus');
        await pc.setLocalDescription(await pc.createOffer());
        window.__zephyrusPc = pc;
      })();
  )"));

  EXPECT_TRUE(WaitForWebrtcSignal(page))
      << "an iframe's peer connection must appear on the top-level site's "
         "record, which is the only place the user would look for it";
  EXPECT_EQ(0u, service()->GetPageSignals(frame_url).webrtc_address_requests)
      << "and must not create a record under the iframe's own site";
}

}  // namespace
}  // namespace zephyrus_privacy

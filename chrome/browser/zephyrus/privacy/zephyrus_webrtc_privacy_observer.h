// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_WEBRTC_PRIVACY_OBSERVER_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_WEBRTC_PRIVACY_OBSERVER_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/peer_connection_tracker_host_observer.h"

namespace content {
class BrowserContext;
}

namespace zephyrus_privacy {

// §9.2.1: detects that a page constructed an RTCPeerConnection, and records
// what the WebRTC IP handling policy actually withheld.
//
// **Why this matters.** ICE candidate gathering hands a page the device's local
// and public addresses with no permission prompt and no HTTP request the
// blocker ever sees. It defeats a VPN and yields a stable cross-site
// identifier. Without this, the feature would report "no trackers detected" on
// a page that had just taken the user's network address.
//
// **Why no Blink hook.** content::PeerConnectionTrackerHostObserver already
// reports peer connection construction to the browser, with the frame and the
// serialized RTCConfiguration. Patching Blink to re-report what content already
// reports would add fork delta, carry less information, and have to be
// re-merged forever. The renderer -> browser channel §9.2.1 also asks for is
// built separately and carries the surfaces content genuinely cannot see; see
// zephyrus_privacy_reporter_host.h.
//
// **Not a blocker.** Nothing here cancels or alters a connection. §3.1 keeps
// the analysis layer one-directional, and §9.2.1's mitigation is a policy knob
// Chromium already owns, not a new subsystem.
class ZephyrusWebrtcPrivacyObserver
    : public KeyedService,
      public content::PeerConnectionTrackerHostObserver {
 public:
  explicit ZephyrusWebrtcPrivacyObserver(content::BrowserContext* context);
  ZephyrusWebrtcPrivacyObserver(const ZephyrusWebrtcPrivacyObserver&) = delete;
  ZephyrusWebrtcPrivacyObserver& operator=(
      const ZephyrusWebrtcPrivacyObserver&) = delete;
  ~ZephyrusWebrtcPrivacyObserver() override;

  // content::PeerConnectionTrackerHostObserver:
  void OnPeerConnectionAdded(content::GlobalRenderFrameHostId frame_id,
                             int lid,
                             base::ProcessId pid,
                             const std::string& url,
                             const std::string& rtc_configuration) override;

 private:
  // Whether the policy in force for this profile withholds local addresses.
  //
  // Read per event rather than cached: enterprise policy or an extension can
  // change it at any time, and the UI string that follows from it is a claim
  // about what happened to THIS connection. A stale cached answer would make
  // that claim false without anything looking broken.
  bool LocalAddressesWithheld() const;

  const raw_ptr<content::BrowserContext> context_;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_WEBRTC_PRIVACY_OBSERVER_H_

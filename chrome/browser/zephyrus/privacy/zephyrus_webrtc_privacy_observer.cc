// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/zephyrus_webrtc_privacy_observer.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_frame_host.h"
#include "third_party/blink/public/common/peerconnection/webrtc_ip_handling_policy.h"
#include "url/gurl.h"

namespace zephyrus_privacy {

ZephyrusWebrtcPrivacyObserver::ZephyrusWebrtcPrivacyObserver(
    content::BrowserContext* context)
    : context_(context) {
  // §9.2.1's mitigation, in one line: default to the policy that uses the
  // default route and exposes no local addresses.
  //
  // SetDefaultPrefValue rather than SetString, deliberately. It moves the
  // DEFAULT, so an explicit user choice and an enterprise policy both still
  // win, and a user who has never touched the setting gets the safer behaviour
  // without us having overwritten anything of theirs. Writing the value
  // directly would silently clobber a deliberate choice on every launch.
  Profile* profile = Profile::FromBrowserContext(context);
  // FindPreference first: SetDefaultPrefValue on an unregistered path is fatal,
  // and this runs during profile construction. The pref is registered by
  // upstream Chromium, so an absent one means an upstream change moved or
  // renamed it — in which case the honest outcome is to leave the policy alone
  // and keep reporting truthfully about whatever is in force, not to take the
  // browser down at startup.
  if (profile && profile->GetPrefs()->FindPreference(
                     prefs::kWebRTCIPHandlingPolicy)) {
    profile->GetPrefs()->SetDefaultPrefValue(
        prefs::kWebRTCIPHandlingPolicy,
        base::Value(blink::kWebRTCIPHandlingDefaultPublicInterfaceOnly));
  }
}

ZephyrusWebrtcPrivacyObserver::~ZephyrusWebrtcPrivacyObserver() = default;

bool ZephyrusWebrtcPrivacyObserver::LocalAddressesWithheld() const {
  Profile* profile = Profile::FromBrowserContext(context_);
  if (!profile) {
    return false;
  }
  const std::string policy =
      profile->GetPrefs()->GetString(prefs::kWebRTCIPHandlingPolicy);
  // Only these two withhold local addresses, per the declarations in
  // webrtc_ip_handling_policy.h. An allowlist, not a denylist: an unrecognised
  // value must read as "not withheld", so a policy we do not know about can
  // never produce a false claim of protection.
  return policy == blink::kWebRTCIPHandlingDefaultPublicInterfaceOnly ||
         policy == blink::kWebRTCIPHandlingDisableNonProxiedUdp;
}

void ZephyrusWebrtcPrivacyObserver::OnPeerConnectionAdded(
    content::GlobalRenderFrameHostId frame_id,
    int lid,
    base::ProcessId pid,
    const std::string& url,
    const std::string& rtc_configuration) {
  // The observer list is global: every profile's instance sees every peer
  // connection in the browser. Resolve the frame and keep only our own, or one
  // profile's popup would report another profile's browsing.
  content::RenderFrameHost* rfh = content::RenderFrameHost::FromID(frame_id);
  if (!rfh || rfh->GetBrowserContext() != context_) {
    return;
  }

  PrivacyIntelligenceService* service =
      PrivacyIntelligenceServiceFactory::GetForBrowserContext(context_);
  if (!service) {
    return;
  }

  // `url` is supplied by the tracker alongside the event, but it is derived
  // from the renderer. Take the committed URL from the RenderFrameHost
  // instead — it is the browser's own record and cannot be forged.
  //
  // Outermost main frame, per §9.2: a peer connection opened inside a
  // third-party iframe is still something that happened to the user on the page
  // they are looking at, and that is the page the popup describes.
  const GURL page_url = rfh->GetOutermostMainFrame()->GetLastCommittedURL();
  service->RecordWebrtcAddressRequest(page_url, LocalAddressesWithheld());
}

}  // namespace zephyrus_privacy

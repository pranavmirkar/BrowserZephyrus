// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/zephyrus_webrtc_privacy_observer_factory.h"

#include <memory>

#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/browser/zephyrus/privacy/zephyrus_webrtc_privacy_observer.h"
#include "content/public/browser/browser_context.h"

namespace zephyrus_privacy {

// static
ZephyrusWebrtcPrivacyObserver*
ZephyrusWebrtcPrivacyObserverFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  if (!context) {
    return nullptr;
  }
  return static_cast<ZephyrusWebrtcPrivacyObserver*>(
      GetInstance()->GetServiceForBrowserContext(context, /*create=*/true));
}

// static
ZephyrusWebrtcPrivacyObserverFactory*
ZephyrusWebrtcPrivacyObserverFactory::GetInstance() {
  static base::NoDestructor<ZephyrusWebrtcPrivacyObserverFactory> instance;
  return instance.get();
}

ZephyrusWebrtcPrivacyObserverFactory::ZephyrusWebrtcPrivacyObserverFactory()
    : ProfileKeyedServiceFactory(
          "ZephyrusWebrtcPrivacyObserver",
          // Mirrors PrivacyIntelligenceServiceFactory exactly. Incognito must
          // record nothing (§5.2), and the strictest way to guarantee that is
          // for the observer not to exist there at all.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {
  DependsOn(PrivacyIntelligenceServiceFactory::GetInstance());
}

ZephyrusWebrtcPrivacyObserverFactory::~ZephyrusWebrtcPrivacyObserverFactory() =
    default;

std::unique_ptr<KeyedService>
ZephyrusWebrtcPrivacyObserverFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  // §13.1 kDisabled means zero cost, so not even an observer registration.
  if (!IsCollectionEnabled()) {
    return nullptr;
  }
  if (context->IsOffTheRecord()) {
    return nullptr;
  }
  return std::make_unique<ZephyrusWebrtcPrivacyObserver>(context);
}

bool ZephyrusWebrtcPrivacyObserverFactory::ServiceIsCreatedWithBrowserContext()
    const {
  // Load-bearing. See the header: nothing requests this service, so without
  // eager creation it is never built and the WebRTC half of §9.2.1 silently
  // does nothing.
  return true;
}

}  // namespace zephyrus_privacy

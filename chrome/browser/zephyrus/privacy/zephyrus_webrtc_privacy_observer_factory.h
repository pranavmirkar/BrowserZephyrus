// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_WEBRTC_PRIVACY_OBSERVER_FACTORY_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_WEBRTC_PRIVACY_OBSERVER_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

namespace content {
class BrowserContext;
}

namespace zephyrus_privacy {

class ZephyrusWebrtcPrivacyObserver;

// **This service exists to observe, so it must be built eagerly.**
//
// Nothing ever calls GetForBrowserContext() in production: the observer's whole
// job is to register itself with the global peer-connection observer list at
// profile start and then react. A lazily-created keyed service that nobody asks
// for is never created at all, which in this codebase has already produced five
// separate "written, tested, never called" defects. Hence
// ServiceIsCreatedWithBrowserContext() returning true — that override is the
// feature, not boilerplate. Do not remove it.
class ZephyrusWebrtcPrivacyObserverFactory
    : public ProfileKeyedServiceFactory {
 public:
  // Present for tests, which need a handle on the instance the profile built.
  static ZephyrusWebrtcPrivacyObserver* GetForBrowserContext(
      content::BrowserContext* context);
  static ZephyrusWebrtcPrivacyObserverFactory* GetInstance();

  ZephyrusWebrtcPrivacyObserverFactory(
      const ZephyrusWebrtcPrivacyObserverFactory&) = delete;
  ZephyrusWebrtcPrivacyObserverFactory& operator=(
      const ZephyrusWebrtcPrivacyObserverFactory&) = delete;

 private:
  friend base::NoDestructor<ZephyrusWebrtcPrivacyObserverFactory>;

  ZephyrusWebrtcPrivacyObserverFactory();
  ~ZephyrusWebrtcPrivacyObserverFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_WEBRTC_PRIVACY_OBSERVER_FACTORY_H_

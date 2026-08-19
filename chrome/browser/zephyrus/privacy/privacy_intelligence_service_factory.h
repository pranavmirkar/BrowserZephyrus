// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_INTELLIGENCE_SERVICE_FACTORY_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_INTELLIGENCE_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

namespace content {
class BrowserContext;
}

namespace zephyrus_privacy {

class PrivacyIntelligenceService;

class PrivacyIntelligenceServiceFactory : public ProfileKeyedServiceFactory {
 public:
  // Returns null when the feature is off, and null for every off-the-record
  // profile. Callers must handle null rather than assuming a service exists.
  static PrivacyIntelligenceService* GetForBrowserContext(
      content::BrowserContext* context);
  static PrivacyIntelligenceServiceFactory* GetInstance();

  PrivacyIntelligenceServiceFactory(const PrivacyIntelligenceServiceFactory&) =
      delete;
  PrivacyIntelligenceServiceFactory& operator=(
      const PrivacyIntelligenceServiceFactory&) = delete;

 private:
  friend base::NoDestructor<PrivacyIntelligenceServiceFactory>;

  PrivacyIntelligenceServiceFactory();
  ~PrivacyIntelligenceServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_INTELLIGENCE_SERVICE_FACTORY_H_

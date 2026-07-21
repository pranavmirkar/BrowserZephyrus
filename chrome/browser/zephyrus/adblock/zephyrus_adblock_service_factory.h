// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SERVICE_FACTORY_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

namespace content {
class BrowserContext;
}

namespace zephyrus_adblock {

class ZephyrusAdblockService;

class ZephyrusAdblockServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static ZephyrusAdblockService* GetForBrowserContext(
      content::BrowserContext* context);
  static ZephyrusAdblockServiceFactory* GetInstance();

  ZephyrusAdblockServiceFactory(const ZephyrusAdblockServiceFactory&) = delete;
  ZephyrusAdblockServiceFactory& operator=(
      const ZephyrusAdblockServiceFactory&) = delete;

 private:
  friend base::NoDestructor<ZephyrusAdblockServiceFactory>;

  ZephyrusAdblockServiceFactory();
  ~ZephyrusAdblockServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  // Create eagerly at startup so blocking is active from the first navigation
  // and the settings-page stats populate without needing a prior page visit.
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SERVICE_FACTORY_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"

#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace zephyrus_adblock {

// static
ZephyrusAdblockService* ZephyrusAdblockServiceFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<ZephyrusAdblockService*>(
      GetInstance()->GetServiceForBrowserContext(context, /*create=*/true));
}

// static
ZephyrusAdblockServiceFactory* ZephyrusAdblockServiceFactory::GetInstance() {
  static base::NoDestructor<ZephyrusAdblockServiceFactory> instance;
  return instance.get();
}

ZephyrusAdblockServiceFactory::ZephyrusAdblockServiceFactory()
    : ProfileKeyedServiceFactory(
          "ZephyrusAdblockService",
          // Block in incognito too, using its own instance.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .WithGuest(ProfileSelection::kOwnInstance)
              .Build()) {
  // Allowlisting a site also creates a third-party-cookie exception for it.
  DependsOn(HostContentSettingsMapFactory::GetInstance());
}

ZephyrusAdblockServiceFactory::~ZephyrusAdblockServiceFactory() = default;

std::unique_ptr<KeyedService>
ZephyrusAdblockServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  // Only regular (non-incognito) profiles fetch filter-list updates; others
  // reuse the shared downloaded file read-only.
  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory;
  if (!context->IsOffTheRecord()) {
    url_loader_factory = context->GetDefaultStoragePartition()
                             ->GetURLLoaderFactoryForBrowserProcess();
  }
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<ZephyrusAdblockService>(
      std::move(url_loader_factory), profile->GetPrefs(),
      HostContentSettingsMapFactory::GetForProfile(profile));
}

bool ZephyrusAdblockServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  return true;
}

}  // namespace zephyrus_adblock

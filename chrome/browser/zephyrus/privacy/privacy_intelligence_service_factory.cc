// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"

#include <memory>

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/zephyrus/privacy/privacy_crypto_impl.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "content/public/browser/browser_context.h"

namespace zephyrus_privacy {

// static
PrivacyIntelligenceService*
PrivacyIntelligenceServiceFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  if (!context) {
    return nullptr;
  }
  return static_cast<PrivacyIntelligenceService*>(
      GetInstance()->GetServiceForBrowserContext(context, /*create=*/true));
}

// static
PrivacyIntelligenceServiceFactory*
PrivacyIntelligenceServiceFactory::GetInstance() {
  static base::NoDestructor<PrivacyIntelligenceServiceFactory> instance;
  return instance.get();
}

PrivacyIntelligenceServiceFactory::PrivacyIntelligenceServiceFactory()
    : ProfileKeyedServiceFactory(
          "ZephyrusPrivacyIntelligenceService",
          // Regular profiles only. Spec §5.2 requires incognito events to be
          // dropped at the service boundary; building no service at all is the
          // strictest reading and leaves no path by which an OTR event could
          // reach the pipeline, let alone the database. Guest profiles are
          // excluded on the same grounds — they are ephemeral by definition.
          //
          // The consequence is deliberate: Privacy Intelligence surfaces show
          // nothing in incognito. The existing Shield per-tab counter is
          // separate and unaffected.
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOwnInstance)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {}

PrivacyIntelligenceServiceFactory::~PrivacyIntelligenceServiceFactory() =
    default;

std::unique_ptr<KeyedService>
PrivacyIntelligenceServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  // §13.1 kDisabled means "no service, zero cost" — not an idle service.
  if (!IsCollectionEnabled()) {
    return nullptr;
  }
  // Belt and braces. ProfileSelections should already have excluded these, and
  // a regression there would silently start collecting in incognito, which is
  // the one failure in this feature that must never ship.
  if (context->IsOffTheRecord()) {
    return nullptr;
  }
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<PrivacyIntelligenceService>(
      profile->GetPath(), profile->GetPrefs(),
      g_browser_process ? g_browser_process->os_crypt_async() : nullptr);
}

}  // namespace zephyrus_privacy

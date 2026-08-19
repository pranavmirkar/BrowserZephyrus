// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §15: "Incognito writes exactly zero rows, asserted by test."
//
// The mechanism §5.2 chose is not a filter that drops incognito events — it is
// the absence of any boundary at all. No service is built for an off-the-record
// profile, so the emission point finds no sink, nothing is collected, and there
// is no code path that could write a row even if someone later added one.
//
// That makes the factory the right place to assert it: if the factory ever
// starts returning a service for an OTR profile, every other guarantee in §5.2
// is void, and this test is what notices.

#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

class PrivacyIntelligenceServiceFactoryTest : public testing::Test {
 public:
  PrivacyIntelligenceServiceFactoryTest() {
    features_.InitAndEnableFeature(kZephyrusPrivacyIntelligence);
  }

 protected:
  content::BrowserTaskEnvironment task_environment_;
  base::test::ScopedFeatureList features_;
};

TEST_F(PrivacyIntelligenceServiceFactoryTest, RegularProfileGetsAService) {
  TestingProfile profile;
  // The control: without this, "OTR has no service" would also pass if the
  // feature simply never built one for anybody.
  EXPECT_TRUE(
      PrivacyIntelligenceServiceFactory::GetForBrowserContext(&profile));
}

TEST_F(PrivacyIntelligenceServiceFactoryTest, IncognitoGetsNoServiceAtAll) {
  TestingProfile profile;
  Profile* otr =
      profile.GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ASSERT_TRUE(otr);
  ASSERT_TRUE(otr->IsOffTheRecord());

  EXPECT_FALSE(PrivacyIntelligenceServiceFactory::GetForBrowserContext(otr))
      << "§5.2: an off-the-record profile must have NO service. A service that "
         "exists but promises to drop events is one refactor away from not "
         "dropping them.";
}

// A non-primary OTR profile is a different BrowserContext with the same
// requirement, and it is the one an implementation is most likely to miss.
TEST_F(PrivacyIntelligenceServiceFactoryTest, NonPrimaryOtrGetsNoServiceEither) {
  TestingProfile profile;
  Profile* otr = profile.GetOffTheRecordProfile(
      Profile::OTRProfileID::CreateUniqueForTesting(),
      /*create_if_needed=*/true);
  ASSERT_TRUE(otr);
  ASSERT_TRUE(otr->IsOffTheRecord());
  EXPECT_FALSE(PrivacyIntelligenceServiceFactory::GetForBrowserContext(otr));
}

// With the feature off there is no service for anyone, so there is no sink and
// nothing to collect — the "zero cost when disabled" claim in §13.
TEST(PrivacyIntelligenceServiceFactoryDisabledTest, NoServiceWhenFeatureOff) {
  content::BrowserTaskEnvironment task_environment;
  base::test::ScopedFeatureList features;
  features.InitAndDisableFeature(kZephyrusPrivacyIntelligence);
  TestingProfile profile;
  EXPECT_FALSE(
      PrivacyIntelligenceServiceFactory::GetForBrowserContext(&profile));
}

}  // namespace
}  // namespace zephyrus_privacy

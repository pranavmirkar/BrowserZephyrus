// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/model_settings.h"

#include <optional>
#include <string>

#include "chrome/browser/zephyrus/agent/bundled_keys.h"

#include "components/os_crypt/async/browser/test_utils.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus::agent {
namespace {

class ModelSettingsTest : public testing::Test {
 protected:
  void SetUp() override {
    RegisterProfilePrefs(prefs_.registry());
    // The tests below are about the user's own keys. A demo build defaults the
    // provided-keys switch ON, which is tested on its own further down.
    prefs_.SetBoolean(kUseProvidedKeysPref, false);
  }

  TestingPrefServiceSimple prefs_;
  scoped_refptr<os_crypt_async::TestEncryptor> encryptor_ =
      os_crypt_async::GetTestEncryptorForTesting();
};

TEST_F(ModelSettingsTest, NothingIsConfiguredByDefault) {
  EXPECT_FALSE(ReadCloudModelConfig(prefs_));
  EXPECT_FALSE(IsCloudAllowedInWorkspace(prefs_, 1));
  EXPECT_FALSE(HasApiKey(prefs_, "anthropic"));
}

TEST_F(ModelSettingsTest, TheProvidedKeysSwitchIsOnByDefault) {
  TestingPrefServiceSimple fresh;
  RegisterProfilePrefs(fresh.registry());
  EXPECT_TRUE(fresh.GetBoolean(kUseProvidedKeysPref));
}

TEST_F(ModelSettingsTest, ProvidedKeysWorkOutOfTheBoxAndTheSwitchDecides) {
  if (!HasBundledKey(kBundledDefaultKind)) {
    GTEST_SKIP() << "this build bundles no key";
  }
  prefs_.SetBoolean(kUseProvidedKeysPref, true);  // what a new user starts with
  const std::optional<std::string> bundled = BundledKey(kBundledDefaultKind);
  ASSERT_TRUE(bundled);
  EXPECT_FALSE(bundled->empty());
  EXPECT_EQ(bundled->find(char{0}), std::string::npos);

  // Nothing chosen: the demo default, allowed everywhere until the user says.
  std::optional<CloudModelConfig> config = ReadCloudModelConfig(prefs_);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->kind, kBundledDefaultKind);
  EXPECT_TRUE(HasApiKey(prefs_, kBundledDefaultKind));
  EXPECT_TRUE(UsesBundledKey(prefs_, kBundledDefaultKind));
  // The token is for the demo proxy and goes nowhere else.
  EXPECT_EQ(config->base_url, GURL(BundledProxyUrl() + "/api/anthropic"));
  EXPECT_TRUE(IsCloudAllowedInWorkspace(prefs_, 7));
  EXPECT_EQ(LoadApiKey(prefs_, *encryptor_, kBundledDefaultKind), bundled);

  // Switch ON: the provided token is used even if the user saved a key.
  ASSERT_TRUE(
      StoreApiKey(prefs_, *encryptor_, kBundledDefaultKind, "sk-ant-mine"));
  EXPECT_EQ(LoadApiKey(prefs_, *encryptor_, kBundledDefaultKind), bundled);
  EXPECT_EQ(ReadCloudModelConfig(prefs_)->base_url,
            GURL(BundledProxyUrl() + "/api/anthropic"));

  // Switch OFF: the user's own key, straight to the provider, never the proxy.
  prefs_.SetBoolean(kUseProvidedKeysPref, false);
  EXPECT_FALSE(UsesBundledKey(prefs_, kBundledDefaultKind));
  EXPECT_TRUE(HasSavedKey(prefs_, kBundledDefaultKind));
  EXPECT_EQ(LoadApiKey(prefs_, *encryptor_, kBundledDefaultKind),
            "sk-ant-mine");

  // OFF and a saved key that cannot be read: no key at all, NOT the token.
  {
    ScopedDictPrefUpdate update(&prefs_, kCloudKeysPref);
    update->Set(kBundledDefaultKind, "!!not base64!!");
  }
  EXPECT_FALSE(LoadApiKey(prefs_, *encryptor_, kBundledDefaultKind));

  // OFF and nothing saved: nothing is configured, and nothing falls back.
  ForgetApiKey(prefs_, kBundledDefaultKind);
  EXPECT_FALSE(HasApiKey(prefs_, kBundledDefaultKind));
  EXPECT_FALSE(LoadApiKey(prefs_, *encryptor_, kBundledDefaultKind));
  EXPECT_FALSE(ReadCloudModelConfig(prefs_));
  EXPECT_FALSE(IsCloudAllowedInWorkspace(prefs_, 7));

  // Turning it ON pins the provider to the provided one, whatever was chosen.
  CloudModelConfig other;
  other.kind = "openai";
  other.model = "some-model";
  other.base_url = GURL("https://openrouter.ai/api/v1");
  WriteCloudModelConfig(prefs_, other);
  prefs_.SetBoolean(kUseProvidedKeysPref, true);
  EXPECT_EQ(ReadCloudModelConfig(prefs_)->kind, kBundledDefaultKind);
  EXPECT_EQ(ReadCloudModelConfig(prefs_)->model, kBundledDefaultModel);

  // The first change to the workspace switch ends the blanket allowance.
  SetCloudAllowedInWorkspace(prefs_, 1, true);
  EXPECT_TRUE(IsCloudAllowedInWorkspace(prefs_, 1));
  EXPECT_FALSE(IsCloudAllowedInWorkspace(prefs_, 7));
}

TEST_F(ModelSettingsTest, AConfigRoundTrips) {
  CloudModelConfig config;
  config.kind = "openai";
  config.model = "some-model";
  config.base_url = GURL("https://openrouter.ai/api/v1");
  config.force_tool = false;
  config.max_usd_per_task = 2.5;
  config.send_screenshots = false;
  WriteCloudModelConfig(prefs_, config);
  std::optional<CloudModelConfig> read = ReadCloudModelConfig(prefs_);
  ASSERT_TRUE(read);
  EXPECT_EQ(read->kind, "openai");
  EXPECT_EQ(read->base_url, GURL("https://openrouter.ai/api/v1"));
  EXPECT_FALSE(read->force_tool);
  EXPECT_DOUBLE_EQ(read->max_usd_per_task, 2.5);
  EXPECT_FALSE(read->send_screenshots);
}

TEST_F(ModelSettingsTest, OnlyAnOpenAiCompatibleEndpointIsTheUsersToChoose) {
  // A tampered pref cannot send an Anthropic key somewhere else.
  prefs_.SetString(kCloudKindPref, "anthropic");
  prefs_.SetString(kCloudModelPref, "claude-opus-5-5");
  prefs_.SetString(kCloudBaseUrlPref, "https://attacker.example");
  std::optional<CloudModelConfig> read = ReadCloudModelConfig(prefs_);
  ASSERT_TRUE(read);
  // Whatever is stored is ignored: the provider's own address, or in a demo
  // build with no key of the user's, the demo proxy.
  EXPECT_EQ(read->base_url,
            UsesBundledKey(prefs_, "anthropic")
                ? GURL(BundledProxyUrl() + "/api/anthropic")
                : GURL("https://api.anthropic.com"));
}

TEST_F(ModelSettingsTest, AnAbsurdSpendingLimitIsClamped) {
  prefs_.SetString(kCloudKindPref, "gemini");
  prefs_.SetString(kCloudModelPref, "m");
  prefs_.SetDouble(kCloudMaxUsdPref, 1e9);
  EXPECT_LE(ReadCloudModelConfig(prefs_)->max_usd_per_task, 100.0);
  prefs_.SetDouble(kCloudMaxUsdPref, -5);
  EXPECT_GT(ReadCloudModelConfig(prefs_)->max_usd_per_task, 0.0);
}

// These use a kind no build bundles a key for, so they hold in a demo build.
TEST_F(ModelSettingsTest, AKeyIsStoredEncryptedAndReadBack) {
  ASSERT_TRUE(StoreApiKey(prefs_, *encryptor_, "gemini", "sk-ant-secret"));
  EXPECT_TRUE(HasApiKey(prefs_, "gemini"));
  // Not in the clear anywhere in the stored prefs.
  const std::string* stored =
      prefs_.GetDict(kCloudKeysPref).FindString("gemini");
  ASSERT_TRUE(stored);
  EXPECT_EQ(stored->find("sk-ant-secret"), std::string::npos);
  EXPECT_EQ(LoadApiKey(prefs_, *encryptor_, "gemini"), "sk-ant-secret");
  // Another provider's key is separate.
  EXPECT_FALSE(LoadApiKey(prefs_, *encryptor_, "openai"));

  ForgetApiKey(prefs_, "gemini");
  EXPECT_FALSE(HasApiKey(prefs_, "gemini"));
  EXPECT_FALSE(LoadApiKey(prefs_, *encryptor_, "gemini"));
}

TEST_F(ModelSettingsTest, NoKeystoreMeansNoKeyNotAPlaintextOne) {
  scoped_refptr<os_crypt_async::TestEncryptor> no_keys =
      os_crypt_async::GetTestEncryptorWithoutKeysForTesting();
  EXPECT_FALSE(StoreApiKey(prefs_, *no_keys, "gemini", "sk-ant-secret"));
  EXPECT_FALSE(HasApiKey(prefs_, "gemini"));
}

TEST_F(ModelSettingsTest, CloudConsentIsPerWorkspace) {
  SetCloudAllowedInWorkspace(prefs_, 2, true);
  EXPECT_TRUE(IsCloudAllowedInWorkspace(prefs_, 2));
  EXPECT_FALSE(IsCloudAllowedInWorkspace(prefs_, 1));
  SetCloudAllowedInWorkspace(prefs_, 2, true);  // idempotent
  SetCloudAllowedInWorkspace(prefs_, 2, false);
  EXPECT_FALSE(IsCloudAllowedInWorkspace(prefs_, 2));
  EXPECT_TRUE(prefs_.GetList(kCloudWorkspacesPref).empty());
}

TEST_F(ModelSettingsTest, OnlyListedModelsHavePrices) {
  EXPECT_TRUE(KnownPrices("claude-opus-5-5"));
  EXPECT_TRUE(KnownPrices("claude-sonnet-5-5"));
  EXPECT_FALSE(KnownPrices("some-unlisted-model"));
}

}  // namespace
}  // namespace zephyrus::agent

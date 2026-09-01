// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/zephyrus_id_reset.h"

#include <string>

#include "components/autofill/core/common/autofill_prefs.h"
#include "components/enterprise/browser/identifiers/identifiers_prefs.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "components/ukm/ukm_pref_names.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

// The Privacy Intelligence lookup key. Spelled out rather than included from
// privacy_crypto_impl.cc, where it is file-local -- and that is the point: this
// test has to fail if someone adds this pref to the reset list, so it must not
// depend on that list to know the name.
constexpr char kLookupKeyPref[] = "zephyrus.privacy.lookup_key";

class ZephyrusIdResetTest : public testing::Test {
 public:
  ZephyrusIdResetTest() {
    local_state_.registry()->RegisterBooleanPref(kAutoResetIdsPref, true);
    local_state_.registry()->RegisterStringPref(
        metrics::prefs::kMetricsClientID, std::string());
    local_state_.registry()->RegisterStringPref(
        metrics::prefs::kMetricsProvisionalClientID, std::string());
    local_state_.registry()->RegisterIntegerPref(
        metrics::prefs::kMetricsLowEntropySource, 0);
    local_state_.registry()->RegisterIntegerPref(
        metrics::prefs::kMetricsOldLowEntropySource, 0);
    local_state_.registry()->RegisterIntegerPref(
        metrics::prefs::kMetricsPseudoLowEntropySource, 0);
    local_state_.registry()->RegisterStringPref(
        metrics::prefs::kMetricsLimitedEntropyRandomizationSource,
        std::string());
    local_state_.registry()->RegisterStringPref(
        metrics::prefs::kMetricsMachineId, std::string());
    local_state_.registry()->RegisterStringPref(ukm::prefs::kUkmClientId,
                                                std::string());
    local_state_.registry()->RegisterStringPref(
        autofill::prefs::kAutofillAblationSeedPref, std::string());

    profile_prefs_.registry()->RegisterStringPref(enterprise::kProfileGUIDPref,
                                                  std::string());
    profile_prefs_.registry()->RegisterStringPref(kLookupKeyPref,
                                                  std::string());
    profile_prefs_.registry()->RegisterStringPref(
        autofill::prefs::kAutofillAblationSeedPref, std::string());
  }

 protected:
  // Puts a recognisable value in every identifier, so "was reset" is the
  // absence of THIS value rather than the absence of any value -- a pref that
  // was never populated would pass a bare emptiness check for free.
  void SeedIdentifiers() {
    local_state_.SetString(metrics::prefs::kMetricsClientID, "client-guid");
    local_state_.SetString(metrics::prefs::kMetricsProvisionalClientID,
                           "provisional-guid");
    local_state_.SetInteger(metrics::prefs::kMetricsLowEntropySource, 1234);
    local_state_.SetInteger(metrics::prefs::kMetricsOldLowEntropySource, 5678);
    local_state_.SetInteger(metrics::prefs::kMetricsPseudoLowEntropySource,
                            4321);
    local_state_.SetString(
        metrics::prefs::kMetricsLimitedEntropyRandomizationSource, "limited");
    local_state_.SetString(metrics::prefs::kMetricsMachineId, "machine-hash");
    local_state_.SetString(ukm::prefs::kUkmClientId, "ukm-id");
    profile_prefs_.SetString(enterprise::kProfileGUIDPref, "profile-guid");
    profile_prefs_.SetString(kLookupKeyPref, "sealed-hmac-key");
    // Registered in both services; seeded in both so the test would catch
    // clearing only one of them.
    local_state_.SetString(autofill::prefs::kAutofillAblationSeedPref,
                           "ablation-seed-local");
    profile_prefs_.SetString(autofill::prefs::kAutofillAblationSeedPref,
                             "ablation-seed-profile");
  }

  void ResetBoth() {
    MaybeResetLocalStateIdentifiers(&local_state_);
    MaybeResetProfileIdentifiers(&profile_prefs_, &local_state_);
  }

  TestingPrefServiceSimple local_state_;
  TestingPrefServiceSimple profile_prefs_;
};

TEST_F(ZephyrusIdResetTest, ClearsEveryIdentifierWhenEnabled) {
  local_state_.SetBoolean(kAutoResetIdsPref, true);
  SeedIdentifiers();
  ResetBoth();

  EXPECT_TRUE(local_state_.GetString(metrics::prefs::kMetricsClientID).empty());
  EXPECT_TRUE(
      local_state_.GetString(metrics::prefs::kMetricsProvisionalClientID)
          .empty());
  EXPECT_EQ(0, local_state_.GetInteger(metrics::prefs::kMetricsLowEntropySource));
  EXPECT_EQ(
      0, local_state_.GetInteger(metrics::prefs::kMetricsOldLowEntropySource));
  EXPECT_EQ(0, local_state_.GetInteger(
                   metrics::prefs::kMetricsPseudoLowEntropySource));
  EXPECT_TRUE(
      local_state_
          .GetString(metrics::prefs::kMetricsLimitedEntropyRandomizationSource)
          .empty());
  EXPECT_TRUE(local_state_.GetString(metrics::prefs::kMetricsMachineId).empty());
  EXPECT_TRUE(local_state_.GetString(ukm::prefs::kUkmClientId).empty());
  EXPECT_TRUE(profile_prefs_.GetString(enterprise::kProfileGUIDPref).empty());
  EXPECT_TRUE(
      local_state_.GetString(autofill::prefs::kAutofillAblationSeedPref)
          .empty());
  EXPECT_TRUE(
      profile_prefs_.GetString(autofill::prefs::kAutofillAblationSeedPref)
          .empty())
      << "the profile copy of the ablation seed survived; autofill registers "
         "this pref in both services and clearing one leaves the other as a "
         "stable identifier";
}

// The feature is a toggle, and a toggle that does the same thing in both
// positions is not a toggle. Asserted rather than assumed.
TEST_F(ZephyrusIdResetTest, LeavesIdentifiersAloneWhenDisabled) {
  local_state_.SetBoolean(kAutoResetIdsPref, false);
  SeedIdentifiers();
  ResetBoth();

  EXPECT_EQ("client-guid",
            local_state_.GetString(metrics::prefs::kMetricsClientID));
  EXPECT_EQ(1234,
            local_state_.GetInteger(metrics::prefs::kMetricsLowEntropySource));
  EXPECT_EQ("machine-hash",
            local_state_.GetString(metrics::prefs::kMetricsMachineId));
  EXPECT_EQ("ukm-id", local_state_.GetString(ukm::prefs::kUkmClientId));
  EXPECT_EQ("profile-guid",
            profile_prefs_.GetString(enterprise::kProfileGUIDPref));
  EXPECT_EQ("ablation-seed-local",
            local_state_.GetString(autofill::prefs::kAutofillAblationSeedPref));
  EXPECT_EQ(
      "ablation-seed-profile",
      profile_prefs_.GetString(autofill::prefs::kAutofillAblationSeedPref));
}

// THE SAFETY TEST.
//
// The lookup key is a per-profile secret, so it looks exactly like something
// that belongs on the reset list. It must never be added: privacy_crypto_impl.cc
// treats a key that no longer unseals as "rows exist that can no longer be
// read", so rotating it every launch would destroy the user's whole tracker
// history on every launch, silently, while the dashboard reported success.
//
// This fails the moment anyone adds it, which is the only warning that failure
// mode would ever produce.
TEST_F(ZephyrusIdResetTest, NeverTouchesThePrivacyIntelligenceLookupKey) {
  local_state_.SetBoolean(kAutoResetIdsPref, true);
  SeedIdentifiers();
  ResetBoth();

  EXPECT_EQ("sealed-hmac-key", profile_prefs_.GetString(kLookupKeyPref))
      << "the Privacy Intelligence HMAC key was cleared; every row already in "
         "the database is now unreadable and the user's tracker history is "
         "gone. Remove it from the reset list -- see the header comment.";
}

// A missing pref must be skipped, not CHECK. An upstream rebase that renames or
// deletes one of these would otherwise crash the browser during startup, before
// the user could reach the switch that turns this off.
TEST_F(ZephyrusIdResetTest, SurvivesAnUnregisteredIdentifier) {
  TestingPrefServiceSimple sparse_local_state;
  sparse_local_state.registry()->RegisterBooleanPref(kAutoResetIdsPref, true);
  // Deliberately registers NONE of the identifier prefs.
  MaybeResetLocalStateIdentifiers(&sparse_local_state);
  SUCCEED() << "did not CHECK on unregistered identifier prefs";
}

}  // namespace
}  // namespace zephyrus_privacy

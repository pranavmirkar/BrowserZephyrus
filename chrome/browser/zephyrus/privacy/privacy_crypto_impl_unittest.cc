// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_crypto_impl.h"

#include <memory>
#include <set>
#include <string>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "components/os_crypt/async/browser/test_utils.h"
#include "components/os_crypt/async/common/test_encryptor.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

class PrivacyCryptoImplTest : public testing::Test {
 protected:
  void SetUp() override {
    PrivacyCryptoImpl::RegisterProfilePrefs(prefs_.registry());
  }

  std::unique_ptr<PrivacyCryptoImpl> Make(
      scoped_refptr<os_crypt_async::TestEncryptor> encryptor) {
    return PrivacyCryptoImpl::Create(std::move(encryptor), &prefs_);
  }

  TestingPrefServiceSimple prefs_;
};

// --- Round trip -------------------------------------------------------------

TEST_F(PrivacyCryptoImplTest, EncryptsAndDecryptsRoundTrip) {
  auto crypto = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(crypto);

  const std::string domain = "tracker-example.net";
  const auto sealed = crypto->Encrypt(domain);
  ASSERT_TRUE(sealed.has_value());

  // The ciphertext must not contain the plaintext — this is the whole point.
  const std::string as_text(sealed->begin(), sealed->end());
  EXPECT_EQ(std::string::npos, as_text.find(domain));

  const auto opened = crypto->Decrypt(*sealed);
  ASSERT_TRUE(opened.has_value());
  EXPECT_EQ(domain, *opened);
}

TEST_F(PrivacyCryptoImplTest, DecryptRejectsGarbage) {
  auto crypto = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(crypto);
  const std::vector<uint8_t> nonsense(48, 0xAB);
  // Must return nullopt rather than crashing or returning junk: a profile
  // copied between machines hits this path legitimately.
  EXPECT_FALSE(crypto->Decrypt(nonsense).has_value());
}

// --- The keyed lookup digest ------------------------------------------------

TEST_F(PrivacyCryptoImplTest, KeyedHashIsStableWithinAProfile) {
  auto crypto = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(crypto);
  const auto a = crypto->KeyedHash("doubleclick.net");
  const auto b = crypto->KeyedHash("doubleclick.net");
  ASSERT_TRUE(a && b);
  EXPECT_EQ(*a, *b) << "lookups would never match if this varied";
}

TEST_F(PrivacyCryptoImplTest, KeyedHashSurvivesRestart) {
  const auto encryptor = os_crypt_async::GetTestEncryptorForTesting();
  auto first = Make(encryptor);
  ASSERT_TRUE(first);
  const auto before = first->KeyedHash("doubleclick.net");
  ASSERT_TRUE(before.has_value());
  first.reset();

  // Same prefs, same keystore: the sealed key is reloaded, so previously
  // written rows are still findable. If this breaks, every restart silently
  // orphans the whole database.
  auto second = Make(encryptor);
  ASSERT_TRUE(second);
  EXPECT_EQ(*before, *second->KeyedHash("doubleclick.net"));
}

TEST_F(PrivacyCryptoImplTest, KeyedHashDiffersBetweenProfiles) {
  auto one = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(one);

  TestingPrefServiceSimple other_prefs;
  PrivacyCryptoImpl::RegisterProfilePrefs(other_prefs.registry());
  auto two = PrivacyCryptoImpl::Create(
      os_crypt_async::GetTestEncryptorForTesting(), &other_prefs);
  ASSERT_TRUE(two);

  // Different profile, different key, therefore a different digest for the
  // same domain. This is what stops one precomputed table from working across
  // installs — the failure the unkeyed hash had.
  EXPECT_NE(*one->KeyedHash("doubleclick.net"),
            *two->KeyedHash("doubleclick.net"));
}

TEST_F(PrivacyCryptoImplTest, KeyedHashSeparatesDifferentDomains) {
  auto crypto = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(crypto);
  std::set<int64_t> digests;
  for (const char* domain :
       {"doubleclick.net", "google-analytics.com", "facebook.net",
        "scorecardresearch.com", "a.example", "b.example"}) {
    const auto digest = crypto->KeyedHash(domain);
    ASSERT_TRUE(digest.has_value());
    EXPECT_TRUE(digests.insert(*digest).second) << "collision on " << domain;
  }
}

// SQLite INTEGER is signed, and the column is compared and indexed as one.
TEST_F(PrivacyCryptoImplTest, KeyedHashIsAlwaysPositive) {
  auto crypto = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(crypto);
  for (int i = 0; i < 200; ++i) {
    const auto digest = crypto->KeyedHash("host" + base::NumberToString(i));
    ASSERT_TRUE(digest.has_value());
    EXPECT_GE(*digest, 0);
  }
}

TEST_F(PrivacyCryptoImplTest, EmptyInputIsStillHashed) {
  auto crypto = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(crypto);
  // The database never stores an empty domain, but the primitive must not
  // special-case it into something degenerate.
  const auto empty = crypto->KeyedHash("");
  const auto nonempty = crypto->KeyedHash("a");
  ASSERT_TRUE(empty && nonempty);
  EXPECT_NE(*empty, *nonempty);
}

// --- The refusal paths (§10) ------------------------------------------------

// The keystore has no key at all: Create must fail, so the database never
// opens and nothing is written in the clear.
TEST_F(PrivacyCryptoImplTest, RefusesWhenTheKeystoreHasNoKeys) {
  auto crypto = Make(os_crypt_async::GetTestEncryptorWithoutKeysForTesting());
  EXPECT_FALSE(crypto);
  // And no key was persisted as a side effect of trying.
  EXPECT_TRUE(prefs_.GetString("zephyrus.privacy.lookup_key").empty());
}

TEST_F(PrivacyCryptoImplTest, RefusesWithoutAPrefService) {
  EXPECT_FALSE(PrivacyCryptoImpl::Create(
      os_crypt_async::GetTestEncryptorForTesting(), nullptr));
}

TEST_F(PrivacyCryptoImplTest, RefusesWithoutAnEncryptor) {
  EXPECT_FALSE(PrivacyCryptoImpl::Create(nullptr, &prefs_));
}

// A stored key that cannot be unsealed — a profile copied to another machine,
// or a keystore reset. Rotating is the only way forward; the alternative is a
// database that can never be written to again.
TEST_F(PrivacyCryptoImplTest, RotatesAKeyThatCannotBeUnsealed) {
  auto original = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(original);
  const auto before = original->KeyedHash("doubleclick.net");
  ASSERT_TRUE(before.has_value());
  const std::string sealed_by_first =
      prefs_.GetString("zephyrus.privacy.lookup_key");
  ASSERT_FALSE(sealed_by_first.empty());
  original.reset();

  // A DIFFERENT keystore instance cannot unseal the stored key. Each call to
  // GetTestEncryptorForTesting() vends a fresh random key.
  auto rotated = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(rotated) << "must recover by rotating, not give up";
  EXPECT_NE(*before, *rotated->KeyedHash("doubleclick.net"));
  EXPECT_NE(sealed_by_first, prefs_.GetString("zephyrus.privacy.lookup_key"))
      << "a fresh key should have been sealed and stored";
}

// The key is at rest in a pref, so it must be sealed there, not sitting in the
// clear next to the data it protects.
TEST_F(PrivacyCryptoImplTest, StoredKeyIsSealedNotPlaintext) {
  auto crypto = Make(os_crypt_async::GetTestEncryptorForTesting());
  ASSERT_TRUE(crypto);
  const std::string stored = prefs_.GetString("zephyrus.privacy.lookup_key");
  ASSERT_FALSE(stored.empty());

  std::string decoded;
  ASSERT_TRUE(base::Base64Decode(stored, &decoded));
  // A raw 32-byte key would decode to exactly the key size; sealed data
  // carries a header and authentication tag, so it must be longer.
  EXPECT_GT(decoded.size(), 32u)
      << "the stored value looks like a bare key, not sealed output";
}

}  // namespace
}  // namespace zephyrus_privacy

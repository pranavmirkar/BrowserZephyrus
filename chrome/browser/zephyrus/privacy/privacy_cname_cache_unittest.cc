// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §9.1's cache. The properties worth pinning are the two that decide whether
// uncloaking helps or hurts: a genuinely cloaked host must be recognised, and
// an ordinary same-company alias must NOT be — §16 budgets zero false
// positives, and "invented a tracker" is the failure mode that costs trust.

#include "chrome/browser/zephyrus/privacy/privacy_cname_cache.h"

#include <string>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

using Aliases = std::vector<std::string>;

// The case the whole feature exists for: a first-party-looking subdomain that
// really points at a tracker.
TEST(PrivacyCnameCacheTest, RecognisesACloakedHost) {
  EXPECT_EQ("tracker-co.net",
            PrivacyCnameCache::CanonicalFromAliases(
                "metrics.example.com",
                Aliases{"example.tracker-co.net", "metrics.example.com"}));
}

// The false positive that must never happen. A company pointing its own
// subdomain at its own CDN is infrastructure, not cloaking.
TEST(PrivacyCnameCacheTest, SameCompanyAliasIsNotCloaking) {
  EXPECT_EQ("", PrivacyCnameCache::CanonicalFromAliases(
                    "images.example.com",
                    Aliases{"cdn.example.com", "images.example.com"}));
  // Also true when the canonical IS the registrable domain itself.
  EXPECT_EQ("", PrivacyCnameCache::CanonicalFromAliases(
                    "www.example.com", Aliases{"example.com"}));
}

TEST(PrivacyCnameCacheTest, NoAliasesMeansNotCloaked) {
  EXPECT_EQ("", PrivacyCnameCache::CanonicalFromAliases("example.com",
                                                        Aliases{}));
}

// Normalisation has to match what the artifact is keyed by, or a cloaked host
// resolves to a name the dataset cannot look up — which reads as a clean page.
TEST(PrivacyCnameCacheTest, NormalisesTheCanonicalName) {
  EXPECT_EQ("tracker-co.net",
            PrivacyCnameCache::CanonicalFromAliases(
                "metrics.example.com", Aliases{"Example.TRACKER-CO.net."}));
}

// A canonical name with no registrable domain — an internal name, an IP-like
// alias — cannot be attributed to anyone (§9.7).
TEST(PrivacyCnameCacheTest, CanonicalWithNoOwnerIsIgnored) {
  EXPECT_EQ("", PrivacyCnameCache::CanonicalFromAliases("metrics.example.com",
                                                        Aliases{"localhost"}));
  EXPECT_EQ("", PrivacyCnameCache::CanonicalFromAliases(
                    "metrics.example.com", Aliases{"internal-host"}));
}

// -- Cache behaviour ---------------------------------------------------------

TEST(PrivacyCnameCacheTest, MissThenHit) {
  auto cache = base::MakeRefCounted<PrivacyCnameCache>();
  EXPECT_TRUE(cache->NeedsResolution("metrics.example.com"));
  EXPECT_FALSE(cache->CanonicalEtld1("metrics.example.com").has_value());

  cache->Record("metrics.example.com", "tracker-co.net");

  EXPECT_FALSE(cache->NeedsResolution("metrics.example.com"))
      << "a resolved host must not be intercepted again — that is the entire "
         "point of the cache";
  ASSERT_TRUE(cache->CanonicalEtld1("metrics.example.com").has_value());
  EXPECT_EQ("tracker-co.net", *cache->CanonicalEtld1("metrics.example.com"));
}

// Caching the negative answer is what keeps the interceptor off the ordinary
// web: almost every host is not cloaked, and re-checking them forever would
// make this a permanent cost instead of a one-off.
TEST(PrivacyCnameCacheTest, NotCloakedIsRemembered) {
  auto cache = base::MakeRefCounted<PrivacyCnameCache>();
  cache->Record("plain.example.com", "");
  EXPECT_FALSE(cache->NeedsResolution("plain.example.com"));
  EXPECT_FALSE(cache->CanonicalEtld1("plain.example.com").has_value());
}

TEST(PrivacyCnameCacheTest, BoundedAndEvictsLeastRecentlyUsed) {
  auto cache = base::MakeRefCounted<PrivacyCnameCache>();
  for (int i = 0; i < 800; ++i) {
    cache->Record("h" + base::NumberToString(i) + ".example", "t.net");
  }
  EXPECT_LE(cache->size_for_testing(), 512u)
      << "a session touching endless hosts must not grow this forever";
  // The most recent survive.
  EXPECT_FALSE(cache->NeedsResolution("h799.example"));
}

TEST(PrivacyCnameCacheTest, EmptyHostIsIgnored) {
  auto cache = base::MakeRefCounted<PrivacyCnameCache>();
  EXPECT_FALSE(cache->NeedsResolution(""));
  cache->Record("", "t.net");
  EXPECT_EQ(0u, cache->size_for_testing());
}

}  // namespace
}  // namespace zephyrus_privacy

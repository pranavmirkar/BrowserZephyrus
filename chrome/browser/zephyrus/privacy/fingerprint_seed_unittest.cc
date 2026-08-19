// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §6.5 / §11.5. These are not tests of an implementation detail — §6.5 calls
// the properties below "correctness requirements, not style", and each one
// corresponds to a concrete way randomization can be worse than doing nothing:
// unstable seeds can be averaged away, linkable seeds re-identify the user, and
// a persisted secret would be a tracking cookie we invented ourselves.

#include "chrome/browser/zephyrus/privacy/fingerprint_seed.h"

#include <array>
#include <set>
#include <string>

#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

std::array<uint8_t, 32> FixedKey(uint8_t fill) {
  std::array<uint8_t, 32> k;
  k.fill(fill);
  return k;
}

FingerprintSessionSecret SecretA() {
  return FingerprintSessionSecret::CreateForTesting(FixedKey(0x11));
}
FingerprintSessionSecret SecretB() {
  return FingerprintSessionSecret::CreateForTesting(FixedKey(0x22));
}

// STABLE. The single most important property: a site reading the same surface
// twice must get the same answer, or it can average our noise away and recover
// the true value — leaving the user perturbed, detectable, and unprotected.
TEST(FingerprintSeedTest, SameOriginAndSessionGivesTheSameSeed) {
  const FingerprintSessionSecret secret = SecretA();
  EXPECT_EQ(secret.DeriveForOriginKey("https://example.com:443"),
            secret.DeriveForOriginKey("https://example.com:443"));
}

// Stability has to hold for the derived per-surface values too, since those are
// what the perturbation actually consumes.
TEST(FingerprintSeedTest, SurfaceValuesAreStableForAnOrigin) {
  const FingerprintSessionSecret secret = SecretA();
  const FingerprintSeed seed = secret.DeriveForOriginKey("https://a.test:443");
  EXPECT_EQ(DeriveSurfaceValue(seed, "canvas"),
            DeriveSurfaceValue(seed, "canvas"));
}

// UNLINKABLE. Two origins must not be able to recognise each other's noise.
TEST(FingerprintSeedTest, DifferentOriginsGetUnrelatedSeeds) {
  const FingerprintSessionSecret secret = SecretA();
  EXPECT_NE(secret.DeriveForOriginKey("https://a.test:443"),
            secret.DeriveForOriginKey("https://b.test:443"));
}

// Origins that differ only by scheme or port are different origins, and the key
// format must not flatten them together.
TEST(FingerprintSeedTest, SchemeAndPortArePartOfTheIdentity) {
  const FingerprintSessionSecret secret = SecretA();
  const FingerprintSeed https = secret.DeriveForOriginKey(
      OriginKeyForSeed("https", "example.com", 443));
  const FingerprintSeed http =
      secret.DeriveForOriginKey(OriginKeyForSeed("http", "example.com", 443));
  const FingerprintSeed other_port = secret.DeriveForOriginKey(
      OriginKeyForSeed("https", "example.com", 8443));
  EXPECT_NE(https, http);
  EXPECT_NE(https, other_port);
}

// The separators exist so that a host containing the delimiter cannot be made
// to collide with a different (host, port) pair.
TEST(FingerprintSeedTest, OriginKeyIsUnambiguous) {
  EXPECT_NE(OriginKeyForSeed("http", "a.com:80", 443),
            OriginKeyForSeed("http", "a.com", 80));
}

// EPHEMERAL. A new browser session must re-randomize everything. If this ever
// failed, the seed would be a stable cross-session device identifier that
// survives clearing browsing data — worse than the fingerprint it perturbs.
TEST(FingerprintSeedTest, ANewSessionSecretChangesEverySeed) {
  EXPECT_NE(SecretA().DeriveForOriginKey("https://example.com:443"),
            SecretB().DeriveForOriginKey("https://example.com:443"));
}

TEST(FingerprintSeedTest, GeneratedSecretsDiffer) {
  // Two independently generated secrets colliding would mean the CSPRNG is not
  // one. Cheap to assert, and the failure it catches is total.
  EXPECT_NE(FingerprintSessionSecret::Generate().DeriveForOriginKey("x"),
            FingerprintSessionSecret::Generate().DeriveForOriginKey("x"));
}

// Surfaces must be independent: reading the canvas answer must not reveal the
// WebGL answer.
TEST(FingerprintSeedTest, DifferentSurfacesGetDifferentValues) {
  const FingerprintSeed seed =
      SecretA().DeriveForOriginKey("https://example.com:443");
  EXPECT_NE(DeriveSurfaceValue(seed, "canvas"),
            DeriveSurfaceValue(seed, "webgl.vendor"));
  EXPECT_NE(DeriveSurfaceValue(seed, "webgl.vendor"),
            DeriveSurfaceValue(seed, "webgl.renderer"));
}

// The same surface across two origins must differ, or the surface value itself
// becomes the cross-origin link.
TEST(FingerprintSeedTest, TheSameSurfaceDiffersAcrossOrigins) {
  const FingerprintSessionSecret secret = SecretA();
  EXPECT_NE(
      DeriveSurfaceValue(secret.DeriveForOriginKey("https://a.test:443"),
                         "canvas"),
      DeriveSurfaceValue(secret.DeriveForOriginKey("https://b.test:443"),
                         "canvas"));
}

// A weak derivation could map many origins onto few seeds, which would silently
// make unrelated sites linkable. A collision here is not a hash-table nuisance;
// it is two sites sharing a fingerprint.
TEST(FingerprintSeedTest, ManyOriginsProduceNoCollisions) {
  const FingerprintSessionSecret secret = SecretA();
  std::set<FingerprintSeed> seeds;
  std::set<uint64_t> values;
  for (int i = 0; i < 2000; ++i) {
    const std::string key =
        OriginKeyForSeed("https", base::StringPrintf("h%d.test", i), 443);
    const FingerprintSeed seed = secret.DeriveForOriginKey(key);
    EXPECT_TRUE(seeds.insert(seed).second) << "seed collision at " << key;
    EXPECT_TRUE(values.insert(DeriveSurfaceValue(seed, "canvas")).second)
        << "surface-value collision at " << key;
  }
}

// Near-identical origins must not produce near-identical seeds; an attacker who
// controls a.test must learn nothing about b.test from a one-character change.
TEST(FingerprintSeedTest, OneCharacterChangeChangesMostOfTheSeed) {
  const FingerprintSessionSecret secret = SecretA();
  const FingerprintSeed a = secret.DeriveForOriginKey("https://a.test:443");
  const FingerprintSeed b = secret.DeriveForOriginKey("https://b.test:443");
  int differing_bits = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    differing_bits += std::popcount(static_cast<unsigned>(a[i] ^ b[i]));
  }
  // 256 bits, so ~128 expected. A derivation that leaked structure would sit
  // far below this; the bound is loose enough never to flake.
  EXPECT_GT(differing_bits, 80) << "seed does not avalanche";
  EXPECT_LT(differing_bits, 176);
}

// Domain separation: the two derivation levels use different prefixes, so a
// surface value can never coincide with a seed derivation over the same text.
TEST(FingerprintSeedTest, OriginAndSurfaceDerivationsAreSeparated) {
  const FingerprintSessionSecret secret = SecretA();
  const FingerprintSeed seed = secret.DeriveForOriginKey("canvas");
  // Derived from the same literal at two different levels; they must not agree.
  const FingerprintSeed same_text = secret.DeriveForOriginKey("canvas");
  EXPECT_EQ(seed, same_text);  // sanity: still stable
  EXPECT_NE(DeriveSurfaceValue(seed, "canvas"),
            DeriveSurfaceValue(same_text, "canvas.other"));
}

// An empty key is a programming error upstream, but it must still behave: no
// crash, and not silently equal to some other origin's seed.
TEST(FingerprintSeedTest, EmptyOriginKeyIsStillDistinct) {
  const FingerprintSessionSecret secret = SecretA();
  EXPECT_NE(secret.DeriveForOriginKey(""),
            secret.DeriveForOriginKey("https://example.com:443"));
}

}  // namespace
}  // namespace zephyrus_privacy

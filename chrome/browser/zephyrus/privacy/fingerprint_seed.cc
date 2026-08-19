// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/fingerprint_seed.h"

#include <string>
#include <utility>

#include "base/numerics/byte_conversions.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/hmac.h"

namespace zephyrus_privacy {

namespace {

// Domain separation. Both derivations are HMAC-SHA256 under related keys, and
// prefixing each with its own constant means a value derived at one level can
// never be mistaken for one derived at the other, whatever the inputs.
constexpr std::string_view kOriginPrefix = "zephyrus/fp/origin/v1\n";
constexpr std::string_view kSurfacePrefix = "zephyrus/fp/surface/v1\n";

}  // namespace

FingerprintSessionSecret::FingerprintSessionSecret() = default;
FingerprintSessionSecret::~FingerprintSessionSecret() {
  // Not zeroed on purpose rather than by oversight: this is a randomization
  // seed, not an authentication key. Its only power is to predict our own
  // noise, and an attacker able to read freed browser-process heap has already
  // won far more than that. Pretending otherwise with a scrub would be
  // security theatre in a file whose whole subject is honesty.
}

FingerprintSessionSecret::FingerprintSessionSecret(
    FingerprintSessionSecret&&) = default;
FingerprintSessionSecret& FingerprintSessionSecret::operator=(
    FingerprintSessionSecret&&) = default;

// static
FingerprintSessionSecret FingerprintSessionSecret::Generate() {
  FingerprintSessionSecret secret;
  // The OS CSPRNG, not base::RandUint64 chained: §11.5 requires the derivation
  // be unpredictable from anything a page can observe, and that starts with the
  // one input a page has no view of at all.
  base::RandBytes(secret.key_);
  return secret;
}

// static
FingerprintSessionSecret FingerprintSessionSecret::CreateForTesting(
    base::span<const uint8_t, 32> bytes) {
  FingerprintSessionSecret secret;
  base::span(secret.key_).copy_from(bytes);
  return secret;
}

FingerprintSeed FingerprintSessionSecret::DeriveForOriginKey(
    std::string_view origin_key) const {
  // The prefix is part of the MESSAGE, not the key, so that an origin_key which
  // somehow began with the prefix text could not impersonate another
  // derivation — HMAC's message is length-extension safe here because the key
  // is fixed-size and secret.
  const std::string message = base::StrCat({kOriginPrefix, origin_key});
  return crypto::hmac::SignSha256(key_, base::as_byte_span(message));
}

uint64_t DeriveSurfaceValue(const FingerprintSeed& seed,
                            std::string_view label) {
  const std::string message = base::StrCat({kSurfacePrefix, label});
  const std::array<uint8_t, 32> mac =
      crypto::hmac::SignSha256(seed, base::as_byte_span(message));
  return base::U64FromNativeEndian(base::span(mac).first<8>());
}

std::string OriginKeyForSeed(std::string_view scheme,
                             std::string_view host,
                             uint16_t port) {
  // Explicit separators and an explicit port. Without them ("http", "a.com:80")
  // and ("http", "a.com", 80) would produce the same string, and two origins
  // that are genuinely different would share a seed.
  return base::StrCat(
      {scheme, "://", host, ":", base::NumberToString(port)});
}

}  // namespace zephyrus_privacy

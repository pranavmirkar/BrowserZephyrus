// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_crypto_impl.h"

#include <utility>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/memory/ptr_util.h"
#include "base/logging.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "crypto/hmac.h"
#include "crypto/random.h"

namespace zephyrus_privacy {

namespace {

// The OSCrypt-sealed HMAC key, base64 because prefs are JSON.
constexpr char kHmacKeyPref[] = "zephyrus.privacy.lookup_key";

constexpr size_t kHmacKeySize = 32;

}  // namespace

// static
void PrivacyCryptoImpl::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kHmacKeyPref, std::string());
}

// static
std::unique_ptr<PrivacyCryptoImpl> PrivacyCryptoImpl::Create(
    scoped_refptr<os_crypt_async::Encryptor> encryptor,
    PrefService* prefs) {
  if (!prefs || !encryptor) {
    return nullptr;
  }
  // No usable key material means no encryption, which by §10 means no
  // database rather than a plaintext one.
  if (!encryptor->IsEncryptionAvailable()) {
    LOG(ERROR) << "Zephyrus privacy: OSCrypt has no key available; the "
                  "database will run memory-only for this session.";
    return nullptr;
  }

  std::array<uint8_t, kHmacKeySize> key = {};

  const std::string stored = prefs->GetString(kHmacKeyPref);
  bool have_key = false;
  // Distinct from "there was never a key": a first run has nothing to discard,
  // whereas a failed unseal means rows exist that can no longer be read.
  bool rotated_over_existing_key = false;
  if (!stored.empty()) {
    std::string sealed;
    if (base::Base64Decode(stored, &sealed)) {
      if (std::optional<std::string> plain = encryptor->DecryptData(
              base::as_byte_span(sealed))) {
        if (plain->size() == kHmacKeySize) {
          base::span(key).copy_from(base::as_byte_span(*plain));
          have_key = true;
        }
      }
    }
    if (!have_key) {
      // A key exists but cannot be unsealed — a profile copied to another
      // machine, or a keystore reset. The old rows are unreadable either way.
      // Rotating is the only way forward; the database's own version/raze path
      // discards the stale rows.
      rotated_over_existing_key = true;
      LOG(ERROR) << "Zephyrus privacy: lookup key could not be unsealed; "
                    "rotating, and discarding the rows it can no longer read.";
    }
  }

  if (!have_key) {
    key = crypto::RandBytesAsArray<kHmacKeySize>();
    std::optional<std::vector<uint8_t>> sealed =
        encryptor->EncryptString(std::string(key.begin(), key.end()));
    if (!sealed) {
      // No keystore: refuse rather than fall back to something unkeyed.
      LOG(ERROR) << "Zephyrus privacy: cannot seal the lookup key; refusing to "
                    "provide crypto, so the database will not open.";
      return nullptr;
    }
    prefs->SetString(kHmacKeyPref, base::Base64Encode(*sealed));
  }

  return base::WrapUnique(new PrivacyCryptoImpl(std::move(encryptor), key,
                                                rotated_over_existing_key));
}

PrivacyCryptoImpl::PrivacyCryptoImpl(
    scoped_refptr<os_crypt_async::Encryptor> encryptor,
    std::array<uint8_t, 32> hmac_key,
    bool key_was_rotated)
    : encryptor_(std::move(encryptor)),
      hmac_key_(hmac_key),
      key_was_rotated_(key_was_rotated) {}

bool PrivacyCryptoImpl::key_was_rotated() const {
  return key_was_rotated_;
}

PrivacyCryptoImpl::~PrivacyCryptoImpl() = default;

std::optional<std::vector<uint8_t>> PrivacyCryptoImpl::Encrypt(
    std::string_view plaintext) {
  return encryptor_->EncryptString(std::string(plaintext));
}

std::optional<std::string> PrivacyCryptoImpl::Decrypt(
    base::span<const uint8_t> ciphertext) {
  return encryptor_->DecryptData(ciphertext);
}

std::optional<int64_t> PrivacyCryptoImpl::KeyedHash(std::string_view value) {
  const std::array<uint8_t, 32> mac =
      crypto::hmac::SignSha256(hmac_key_, base::as_byte_span(value));

  // Fold the first 8 bytes into an integer column. Truncation is fine: 64 bits
  // over a few thousand domains makes collisions vanishingly unlikely, and
  // truncating an HMAC does not weaken its keyed property.
  uint64_t folded = 0;
  for (size_t i = 0; i < 8; ++i) {
    folded = (folded << 8) | mac[i];
  }
  // SQLite INTEGER is signed; drop the top bit so the value is always positive
  // and comparisons in SQL behave as expected.
  return static_cast<int64_t>(folded >> 1);
}

}  // namespace zephyrus_privacy

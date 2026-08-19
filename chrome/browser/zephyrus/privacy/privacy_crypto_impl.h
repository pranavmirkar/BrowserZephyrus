// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CRYPTO_IMPL_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CRYPTO_IMPL_H_

#include <stdint.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "chrome/browser/zephyrus/privacy/privacy_crypto.h"
#include "components/os_crypt/async/common/encryptor.h"

class PrefService;
class PrefRegistrySimple;

namespace zephyrus_privacy {

// The production PrivacyCrypto: OSCrypt for the encrypted columns, and an
// HMAC-SHA256 keyed by a secret that is itself protected by OSCrypt.
//
// **Why the HMAC key is stored rather than derived from the OSCrypt key.**
// OSCrypt exposes encrypt/decrypt, not the key material, so there is nothing to
// derive from. Instead a 32-byte key is generated once, sealed with OSCrypt,
// and kept in a pref. Recovering a domain from the lookup column then requires
// the keystore — the same bar as decrypting the column beside it — which is the
// whole point of §5.3.
//
// **Bounded threat model (§5.3), repeated here because it is easy to overclaim
// in a UI:** this protects against another local user and against offline
// access to the disk. It does NOT protect against malware running as the user,
// which can simply ask the keystore to decrypt. Nothing in the product may
// claim more.
class PrivacyCryptoImpl : public PrivacyCrypto {
 public:
  // Returns null if the HMAC key can neither be loaded nor created — which is
  // the "no key, no database" case. The caller must not substitute a fallback.
  // The Encryptor is refcounted and immutable; a single instance is shared
  // across consumers rather than copied.
  static std::unique_ptr<PrivacyCryptoImpl> Create(
      scoped_refptr<os_crypt_async::Encryptor> encryptor,
      PrefService* prefs);

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  PrivacyCryptoImpl(const PrivacyCryptoImpl&) = delete;
  PrivacyCryptoImpl& operator=(const PrivacyCryptoImpl&) = delete;
  ~PrivacyCryptoImpl() override;

  // PrivacyCrypto:
  std::optional<std::vector<uint8_t>> Encrypt(
      std::string_view plaintext) override;
  std::optional<std::string> Decrypt(
      base::span<const uint8_t> ciphertext) override;
  std::optional<int64_t> KeyedHash(std::string_view value) override;
  bool key_was_rotated() const override;

 private:
  PrivacyCryptoImpl(scoped_refptr<os_crypt_async::Encryptor> encryptor,
                    std::array<uint8_t, 32> hmac_key,
                    bool key_was_rotated);

  const scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  const std::array<uint8_t, 32> hmac_key_;
  // See PrivacyCrypto::key_was_rotated().
  const bool key_was_rotated_;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CRYPTO_IMPL_H_

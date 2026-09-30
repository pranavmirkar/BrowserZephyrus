// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_voice_access.h"

#include <memory>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/zephyrus/agent/voice_library.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"

namespace zephyrus::agent {

namespace {
// Only its address is used.
const char kVoiceLibraryKey[] = "zephyrus.agent.voice_library";

// The library as profile user data: it goes when the profile does.
class Holder : public base::SupportsUserData::Data {
 public:
  explicit Holder(base::FilePath path) : library(std::move(path)) {}
  VoiceLibrary library;
};
}  // namespace

VoiceLibrary* GetVoiceLibrary(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  if (!profile->GetUserData(kVoiceLibraryKey)) {
    const base::FilePath path =
        profile->IsOffTheRecord()
            ? base::FilePath()
            : profile->GetPath().Append(FILE_PATH_LITERAL("Zephyrus Voices"));
    auto holder = std::make_unique<Holder>(path);
    VoiceLibrary* library = &holder->library;
    profile->SetUserData(kVoiceLibraryKey, std::move(holder));
    if (!path.empty() && g_browser_process &&
        g_browser_process->os_crypt_async()) {
      // The stored voices are decrypted with the OS keystore; until it answers
      // the library has none and refuses to save.
      g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
          [](base::WeakPtr<VoiceLibrary> weak,
             scoped_refptr<os_crypt_async::Encryptor> encryptor) {
            if (weak) {
              weak->SetEncryptor(std::move(encryptor));
            }
          },
          library->GetWeakPtr()));
    }
  }
  return &static_cast<Holder*>(profile->GetUserData(kVoiceLibraryKey))->library;
}

}  // namespace zephyrus::agent

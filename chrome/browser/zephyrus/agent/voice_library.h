// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_LIBRARY_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_LIBRARY_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/zephyrus/agent/voice_lock.h"
#include "chrome/browser/zephyrus/agent/voice_profile_store.h"

namespace os_crypt_async {
class Encryptor;
}

namespace zephyrus::agent {

// The voices the user has recorded, for one profile, shared by everything that
// needs them: the hands-free listener, the settings screen and the enrolment
// dialog. A change made anywhere reaches all of them.
class VoiceLibrary {
 public:
  enum class Result {
    kOk,
    kFull,        // kMaxProfiles reached
    kBadName,     // empty, too long, or not printable
    kDuplicate,   // another voice already has that name
    kNoKeystore,  // could not be stored safely, so it was not stored
    kNotFound,
  };

  // An empty path keeps the voices in memory only (Private Workspace).
  explicit VoiceLibrary(base::FilePath path);
  ~VoiceLibrary();

  VoiceLibrary(const VoiceLibrary&) = delete;
  VoiceLibrary& operator=(const VoiceLibrary&) = delete;

  // Hands over the OS keystore; the voices are read and decrypted now. Until
  // then there are none and nothing can be saved.
  void SetEncryptor(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  // Runs `ready` once the voices are loaded (at once if they already are).
  void WhenLoaded(base::OnceClosure ready);
  bool loaded() const { return loaded_; }

  const std::vector<voice::VoiceProfile>& profiles() const {
    return profiles_;
  }

  Result Add(voice::VoiceProfile profile);
  Result Rename(const std::string& id, const std::string& name);
  Result Remove(const std::string& id);
  void RemoveAll();

  base::WeakPtr<VoiceLibrary> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Called whenever the set of voices changes.
  base::CallbackListSubscription Subscribe(base::RepeatingClosure changed);

  // Trimmed, at most kMaxNameLength characters, no control characters. Empty
  // when there is nothing usable.
  static std::string CleanName(const std::string& name);
  // A new random id.
  static std::string NewId();

 private:
  Result Commit();
  void OnLoaded(std::vector<voice::VoiceProfile> profiles);
  bool NameTaken(const std::string& name, const std::string& except_id) const;

  VoiceProfileStore store_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  std::vector<voice::VoiceProfile> profiles_;
  bool loaded_ = false;
  std::vector<base::OnceClosure> waiting_;
  base::RepeatingClosureList observers_;
  base::WeakPtrFactory<VoiceLibrary> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_LIBRARY_H_

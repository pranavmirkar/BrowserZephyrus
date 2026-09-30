// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_PROFILE_STORE_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_PROFILE_STORE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/important_file_writer.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/zephyrus/agent/voice_lock.h"

namespace os_crypt_async {
class Encryptor;
}

namespace zephyrus::agent {

// The user's voices, on disk, encrypted with the OS keystore (DPAPI) like the
// API keys are. What is in the file is features of a short phrase, never audio,
// and it is never uploaded.
//
// One file for all the voices in a profile. An empty path (Private Workspace)
// keeps them in memory only: nothing is written and nothing survives.
class VoiceProfileStore {
 public:
  using LoadCallback =
      base::OnceCallback<void(std::vector<voice::VoiceProfile> profiles)>;

  explicit VoiceProfileStore(base::FilePath path);
  ~VoiceProfileStore();

  VoiceProfileStore(const VoiceProfileStore&) = delete;
  VoiceProfileStore& operator=(const VoiceProfileStore&) = delete;

  // Reads and decrypts. `done` gets the voices, or none if there is no file, it
  // cannot be decrypted (another Windows user, a reinstall) or it is damaged:
  // the user records their voice again rather than the file being trusted.
  void Load(scoped_refptr<os_crypt_async::Encryptor> encryptor,
            LoadCallback done);

  // Replaces what is stored. False when it could not be encrypted, in which case
  // NOTHING is written: a voice is never saved in the clear.
  bool Save(const std::vector<voice::VoiceProfile>& profiles,
            const os_crypt_async::Encryptor& encryptor);

  // Forgets every voice, on disk too.
  void DeleteAll();

  bool persistent() const { return !path_.empty(); }

  static constexpr size_t kMaxFileBytes = 2 * 1024 * 1024;

 private:
  void OnRead(scoped_refptr<os_crypt_async::Encryptor> encryptor,
              LoadCallback done,
              std::optional<std::string> contents);

  const base::FilePath path_;
  // Every read, write and delete of the file goes through this one sequence, so
  // "delete the last voice, then record a new one" cannot be undone by the
  // delete arriving after the write.
  scoped_refptr<base::SequencedTaskRunner> file_runner_;
  std::unique_ptr<base::ImportantFileWriter> writer_;
  base::WeakPtrFactory<VoiceProfileStore> weak_factory_{this};
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_VOICE_PROFILE_STORE_H_

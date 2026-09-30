// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/voice_profile_store.h"

#include <optional>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "components/os_crypt/async/common/encryptor.h"

namespace zephyrus::agent {

VoiceProfileStore::VoiceProfileStore(base::FilePath path)
    : path_(std::move(path)) {
  if (!path_.empty()) {
    file_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
         base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
    writer_ = std::make_unique<base::ImportantFileWriter>(
        path_, file_runner_, "ZephyrusVoiceProfiles");
  }
}

VoiceProfileStore::~VoiceProfileStore() {
  if (writer_ && writer_->HasPendingWrite()) {
    writer_->DoScheduledWrite();
  }
}

void VoiceProfileStore::Load(scoped_refptr<os_crypt_async::Encryptor> encryptor,
                             LoadCallback done) {
  if (path_.empty() || !encryptor) {
    std::move(done).Run({});
    return;
  }
  file_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath path) -> std::optional<std::string> {
            std::string contents;
            if (!base::ReadFileToStringWithMaxSize(path, &contents,
                                                   kMaxFileBytes)) {
              return std::nullopt;
            }
            return contents;
          },
          path_),
      base::BindOnce(&VoiceProfileStore::OnRead, weak_factory_.GetWeakPtr(),
                     std::move(encryptor), std::move(done)));
}

void VoiceProfileStore::OnRead(
    scoped_refptr<os_crypt_async::Encryptor> encryptor,
    LoadCallback done,
    std::optional<std::string> contents) {
  if (!contents || contents->empty()) {
    std::move(done).Run({});
    return;
  }
  const std::optional<std::string> plain = encryptor->DecryptData(
      base::as_byte_span(*contents));
  if (!plain) {
    std::move(done).Run({});
    return;
  }
  std::optional<std::vector<voice::VoiceProfile>> profiles =
      voice::ParseProfiles(*plain);
  std::move(done).Run(profiles ? std::move(*profiles)
                               : std::vector<voice::VoiceProfile>());
}

bool VoiceProfileStore::Save(const std::vector<voice::VoiceProfile>& profiles,
                             const os_crypt_async::Encryptor& encryptor) {
  if (!writer_) {
    return true;  // in memory only: there is nothing to write
  }
  const std::string plain = voice::SerialiseProfiles(profiles);
  std::optional<std::vector<uint8_t>> cipher = encryptor.EncryptString(plain);
  if (!cipher) {
    return false;
  }
  writer_->WriteNow(std::string(cipher->begin(), cipher->end()));
  return true;
}

void VoiceProfileStore::DeleteAll() {
  if (path_.empty()) {
    return;
  }
  // Queued behind any write already handed to the file sequence (Save uses
  // WriteNow, so nothing is held back in a timer).
  file_runner_->PostTask(
      FROM_HERE,
      base::BindOnce([](base::FilePath path) { base::DeleteFile(path); },
                     path_));
}

}  // namespace zephyrus::agent

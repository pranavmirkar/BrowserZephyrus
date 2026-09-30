// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/voice_library.h"

#include <algorithm>
#include <utility>

#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/os_crypt/async/common/encryptor.h"

namespace zephyrus::agent {

VoiceLibrary::VoiceLibrary(base::FilePath path) : store_(std::move(path)) {
  // A library with nowhere to keep anything has nothing to wait for.
  if (!store_.persistent()) {
    loaded_ = true;
  }
}

VoiceLibrary::~VoiceLibrary() = default;

void VoiceLibrary::SetEncryptor(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
  if (loaded_ && !store_.persistent()) {
    return;
  }
  store_.Load(encryptor_, base::BindOnce(&VoiceLibrary::OnLoaded,
                                         weak_factory_.GetWeakPtr()));
}

void VoiceLibrary::OnLoaded(std::vector<voice::VoiceProfile> profiles) {
  // A voice recorded before the file arrived is kept, after the stored ones.
  for (voice::VoiceProfile& p : profiles_) {
    profiles.push_back(std::move(p));
  }
  profiles_ = std::move(profiles);
  loaded_ = true;
  std::vector<base::OnceClosure> waiting = std::move(waiting_);
  waiting_.clear();
  for (base::OnceClosure& c : waiting) {
    std::move(c).Run();
  }
  observers_.Notify();
}

void VoiceLibrary::WhenLoaded(base::OnceClosure ready) {
  if (loaded_) {
    std::move(ready).Run();
    return;
  }
  waiting_.push_back(std::move(ready));
}

std::string VoiceLibrary::CleanName(const std::string& name) {
  std::u16string wide = base::UTF8ToUTF16(name);
  std::u16string cleaned;
  for (char16_t c : wide) {
    if (c >= 0x20 && c != 0x7f) {
      cleaned.push_back(c);
    }
  }
  base::TrimWhitespace(cleaned, base::TRIM_ALL, &cleaned);
  if (cleaned.empty() || cleaned.size() > voice::kMaxNameLength) {
    return std::string();
  }
  return base::UTF16ToUTF8(cleaned);
}

std::string VoiceLibrary::NewId() {
  return base::HexEncodeLower(base::RandBytesAsVector(8));
}

bool VoiceLibrary::NameTaken(const std::string& name,
                             const std::string& except_id) const {
  for (const voice::VoiceProfile& p : profiles_) {
    if (p.id != except_id && base::EqualsCaseInsensitiveASCII(p.name, name)) {
      return true;
    }
  }
  return false;
}

VoiceLibrary::Result VoiceLibrary::Commit() {
  if (store_.persistent()) {
    if (!encryptor_ || !store_.Save(profiles_, *encryptor_)) {
      return Result::kNoKeystore;
    }
  }
  observers_.Notify();
  return Result::kOk;
}

VoiceLibrary::Result VoiceLibrary::Add(voice::VoiceProfile profile) {
  if (profiles_.size() >= voice::kMaxProfiles) {
    return Result::kFull;
  }
  profile.name = CleanName(profile.name);
  if (profile.name.empty()) {
    return Result::kBadName;
  }
  if (NameTaken(profile.name, std::string())) {
    return Result::kDuplicate;
  }
  if (profile.id.empty()) {
    profile.id = NewId();
  }
  if (profile.prepared.empty() && !profile.Prepare()) {
    return Result::kBadName;  // too few recordings to be a voice at all
  }
  profiles_.push_back(std::move(profile));
  const Result r = Commit();
  if (r != Result::kOk) {
    profiles_.pop_back();  // not stored, so not kept
  }
  return r;
}

VoiceLibrary::Result VoiceLibrary::Rename(const std::string& id,
                                          const std::string& name) {
  const std::string cleaned = CleanName(name);
  if (cleaned.empty()) {
    return Result::kBadName;
  }
  auto it = std::find_if(profiles_.begin(), profiles_.end(),
                         [&](const voice::VoiceProfile& p) { return p.id == id; });
  if (it == profiles_.end()) {
    return Result::kNotFound;
  }
  if (NameTaken(cleaned, id)) {
    return Result::kDuplicate;
  }
  const std::string before = it->name;
  it->name = cleaned;
  const Result r = Commit();
  if (r != Result::kOk) {
    it->name = before;
  }
  return r;
}

VoiceLibrary::Result VoiceLibrary::Remove(const std::string& id) {
  auto it = std::find_if(profiles_.begin(), profiles_.end(),
                         [&](const voice::VoiceProfile& p) { return p.id == id; });
  if (it == profiles_.end()) {
    return Result::kNotFound;
  }
  profiles_.erase(it);
  if (profiles_.empty()) {
    store_.DeleteAll();  // no voices, no file
    observers_.Notify();
    return Result::kOk;
  }
  // Removing must always work, even if the keystore does not: the voice is gone
  // from memory either way and the next successful save drops it from disk.
  Commit();
  return Result::kOk;
}

void VoiceLibrary::RemoveAll() {
  profiles_.clear();
  store_.DeleteAll();
  observers_.Notify();
}

base::CallbackListSubscription VoiceLibrary::Subscribe(
    base::RepeatingClosure changed) {
  return observers_.Add(std::move(changed));
}

}  // namespace zephyrus::agent

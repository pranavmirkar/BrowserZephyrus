// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_memory_access.h"

#include <memory>

#include "base/files/file_path.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/zephyrus/agent/agent_memory.h"

namespace zephyrus::agent {

namespace {
// Only its address is used.
const char kMemoryKey[] = "zephyrus.agent.long_term_memory";
}  // namespace

LongTermMemory* GetLongTermMemory(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  if (!profile->GetUserData(kMemoryKey)) {
    const base::FilePath path =
        profile->IsOffTheRecord()
            ? base::FilePath()
            : profile->GetPath().Append(
                  FILE_PATH_LITERAL("Zephyrus Agent Memory"));
    profile->SetUserData(kMemoryKey, std::make_unique<LongTermMemory>(path));
  }
  return static_cast<LongTermMemory*>(profile->GetUserData(kMemoryKey));
}

}  // namespace zephyrus::agent

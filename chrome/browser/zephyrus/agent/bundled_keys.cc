// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/bundled_keys.h"

#include <cstddef>

#include "chrome/browser/zephyrus/agent/bundled_keys_data.h"

namespace zephyrus::agent {

bool HasBundledKey(std::string_view kind) {
  for (const bundled::Entry& entry : bundled::kEntries) {
    if (entry.kind == kind) {
      return true;
    }
  }
  return false;
}

std::string BundledProxyUrl() {
  return std::string(bundled::kProxyUrl);
}

std::optional<std::string> BundledKey(std::string_view kind) {
  for (const bundled::Entry& entry : bundled::kEntries) {
    if (entry.kind != kind || entry.pad.size() != entry.data.size()) {
      continue;
    }
    std::string key(entry.data.size(), '\0');
    for (size_t i = 0; i < key.size(); ++i) {
      key[i] = static_cast<char>(entry.data[i] ^ entry.pad[i]);
    }
    return key;
  }
  return std::nullopt;
}

}  // namespace zephyrus::agent

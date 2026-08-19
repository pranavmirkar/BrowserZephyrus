// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_cname_cache.h"

#include <utility>
#include <vector>

#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace zephyrus_privacy {

PrivacyCnameCache::PrivacyCnameCache() = default;
PrivacyCnameCache::~PrivacyCnameCache() = default;

// static
std::string PrivacyCnameCache::CanonicalFromAliases(
    std::string_view host,
    const std::vector<std::string>& aliases) {
  if (aliases.empty()) {
    return std::string();
  }
  // The first entry is the canonical name (see address_list.mojom). Lowercased
  // and trailing-dot-stripped through the same helper the artifact is keyed
  // by, so a cloaked host resolves against the dataset rather than missing on
  // a formatting difference.
  const std::string canonical =
      base::ToLowerASCII(CanonicalHostForHash(aliases.front()));
  if (canonical.empty()) {
    return std::string();
  }

  const std::string canonical_etld1 =
      net::registry_controlled_domains::GetDomainAndRegistry(
          canonical, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (canonical_etld1.empty()) {
    return std::string();
  }

  const std::string host_etld1 =
      net::registry_controlled_domains::GetDomainAndRegistry(
          std::string(CanonicalHostForHash(host)),
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  // Same registrable domain is not cloaking. A company pointing its own
  // subdomain at its own CDN is ordinary infrastructure, and calling it a
  // hidden third party would invent a tracker that does not exist — the false
  // positive §16 sets a zero budget for.
  if (canonical_etld1 == host_etld1) {
    return std::string();
  }
  return canonical_etld1;
}

bool PrivacyCnameCache::NeedsResolution(std::string_view host) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (host.empty()) {
    return false;
  }
  const auto it = entries_.find(host);
  if (it == entries_.end()) {
    return true;
  }
  return base::TimeTicks::Now() - it->second.recorded_at >= kEntryLifetime;
}

std::optional<std::string> PrivacyCnameCache::CanonicalEtld1(
    std::string_view host) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto it = entries_.find(host);
  if (it == entries_.end() || it->second.canonical_etld1.empty()) {
    return std::nullopt;
  }
  if (base::TimeTicks::Now() - it->second.recorded_at >= kEntryLifetime) {
    // Stale. Report unknown rather than a possibly-moved answer: over-reporting
    // a tracker that has since moved is the false positive, and the next
    // request re-resolves it anyway.
    return std::nullopt;
  }
  // Promote on use.
  lru_.splice(lru_.begin(), lru_, it->second.lru_position);
  return it->second.canonical_etld1;
}

void PrivacyCnameCache::Record(std::string_view host,
                               std::string canonical_etld1) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (host.empty()) {
    return;
  }
  const auto it = entries_.find(host);
  if (it != entries_.end()) {
    it->second.canonical_etld1 = std::move(canonical_etld1);
    it->second.recorded_at = base::TimeTicks::Now();
    lru_.splice(lru_.begin(), lru_, it->second.lru_position);
    return;
  }

  Evict();
  const std::string key(host);
  lru_.push_front(key);
  Entry entry;
  entry.canonical_etld1 = std::move(canonical_etld1);
  entry.recorded_at = base::TimeTicks::Now();
  entry.lru_position = lru_.begin();
  entries_.emplace(key, std::move(entry));
}

void PrivacyCnameCache::Evict() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (entries_.size() >= kMaxEntries && !lru_.empty()) {
    const std::string& oldest = lru_.back();
    entries_.erase(oldest);
    lru_.pop_back();
  }
}

}  // namespace zephyrus_privacy

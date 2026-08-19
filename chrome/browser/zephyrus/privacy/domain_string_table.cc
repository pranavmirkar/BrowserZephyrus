// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/domain_string_table.h"

#include <utility>

namespace zephyrus_privacy {

DomainStringTable::DomainStringTable() = default;
DomainStringTable::~DomainStringTable() = default;

void DomainStringTable::RecordInto(EntryMap& map,
                                   size_t cap,
                                   uint32_t hash,
                                   std::string_view name) {
  const auto it = map.find(hash);
  if (it != map.end()) {
    // Known already: refresh the epoch so the next sweep keeps it. This is the
    // common case, and it costs no allocation.
    it->second.epoch = epoch_;
    return;
  }
  if (map.size() >= cap) {
    // Drop rather than grow. The event still aggregates by hash; only the
    // label is lost (§11.3).
    ++dropped_;
    return;
  }
  map.emplace(hash, Entry{std::string(name), epoch_});
}

void DomainStringTable::Record(uint32_t hash, std::string_view domain) {
  if (domain.empty()) {
    return;
  }
  base::AutoLock guard(lock_);
  RecordInto(entries_, kMaxEntries, hash, domain);
}

void DomainStringTable::RecordSite(uint32_t hash, std::string_view etld1) {
  if (etld1.empty()) {
    return;
  }
  base::AutoLock guard(lock_);
  RecordInto(sites_, kMaxSiteEntries, hash, etld1);
}

// static
std::string DomainStringTable::LookupIn(const EntryMap& map, uint32_t hash) {
  const auto it = map.find(hash);
  return it == map.end() ? std::string() : it->second.name;
}

std::string DomainStringTable::Lookup(uint32_t hash) const {
  base::AutoLock guard(lock_);
  return LookupIn(entries_, hash);
}

std::string DomainStringTable::LookupSite(uint32_t hash) const {
  base::AutoLock guard(lock_);
  return LookupIn(sites_, hash);
}

size_t DomainStringTable::SweepAndAdvance() {
  base::AutoLock guard(lock_);
  size_t removed = 0;
  for (EntryMap* map : {&entries_, &sites_}) {
    for (auto it = map->begin(); it != map->end();) {
      // Strictly older than the current epoch: recorded before the previous
      // sweep and not touched since, so every event that could have wanted it
      // has already been aggregated and flushed.
      if (it->second.epoch < epoch_) {
        it = map->erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
  }
  ++epoch_;
  return removed;
}

size_t DomainStringTable::size() const {
  base::AutoLock guard(lock_);
  return entries_.size() + sites_.size();
}

uint64_t DomainStringTable::dropped_count() const {
  base::AutoLock guard(lock_);
  return dropped_;
}

void DomainStringTable::Clear() {
  base::AutoLock guard(lock_);
  entries_.clear();
  sites_.clear();
  epoch_ = 0;
  dropped_ = 0;
}

}  // namespace zephyrus_privacy

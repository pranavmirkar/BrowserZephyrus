// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_DOMAIN_STRING_TABLE_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_DOMAIN_STRING_TABLE_H_

#include <stdint.h>

#include <map>
#include <string>
#include <string_view>

#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"

namespace zephyrus_privacy {

// Carries domain and site STRINGS from the emission point to the privacy
// sequence, keyed by the hash that RawEvent carries.
//
// **Why this exists.** RawEvent is 16 bytes of integers (§8.5) so the request
// path stays allocation-free, but `tracker_daily.request_domain` stores the
// text for display. §8.6 says "everything downstream stores integers" and §5's
// schema stores TEXT; neither says how the string gets across. This is that
// missing channel.
//
// **It is a channel, not a store.** A name is needed only from the moment its
// request is seen until the next flush folds it into a row — about five
// seconds. Holding it any longer would leave a plaintext list of everywhere the
// user has been sitting in browser memory, outliving the encrypted rows it
// describes, and would need invalidating from every deletion path (Prune's site
// GC, DeleteRange, Clear Browsing Data) to stay honest. Instead every entry is
// stamped with an epoch and `SweepAndAdvance()` drops anything not re-recorded
// since the previous flush. Nothing to invalidate, because nothing lingers.
//
// **Sites and domains are separate namespaces.** They were one map, which cost
// two things: a burst of unique subdomains could exhaust the shared cap and
// starve out site names (turning real sites into "(unknown site)"), and a hash
// collision between a site and a domain would hand one the other's name. The
// site side is also tiny and bounded by real browsing, so it gets its own small
// cap and cannot be crowded out.
//
// **Bounded, and it drops rather than grows.** A page issuing requests to
// thousands of unique subdomains (§11.3) must not be able to grow this without
// limit. Past a cap new names are dropped and counted; their events still
// aggregate by hash, they just render as "unknown" rather than a name. Losing a
// label is acceptable; unbounded memory is not.
//
// **Why a lock is acceptable here.** The ring buffer is lock-free because it is
// touched on every single request. This is touched on every request too, but
// only ever does a map find plus an epoch stamp — no allocation after the first
// sighting within an epoch — and the consumer only reads in batches during a
// flush, so there is nothing to contend with.
class DomainStringTable : public base::RefCountedThreadSafe<DomainStringTable> {
 public:
  // ~4096 short strings. Comfortably inside §8.13's ~30 KB interning budget,
  // and with the sweep in place a single flush interval never comes close.
  static constexpr size_t kMaxEntries = 4096;

  // One entry per site the user is actively loading. Real browsing produces a
  // handful; the cap only exists so a bug cannot make this unbounded.
  static constexpr size_t kMaxSiteEntries = 256;

  DomainStringTable();
  DomainStringTable(const DomainStringTable&) = delete;
  DomainStringTable& operator=(const DomainStringTable&) = delete;

  // Emission side. Cheap and idempotent; re-recording a known name refreshes
  // its epoch, which is what keeps an actively-requested domain from being
  // swept out from under a pending event.
  void Record(uint32_t hash, std::string_view domain);
  void RecordSite(uint32_t hash, std::string_view etld1);

  // Privacy sequence. Empty when the name was never recorded, was dropped at a
  // cap, or was swept — callers must handle that rather than assuming a name
  // exists.
  std::string Lookup(uint32_t hash) const;
  std::string LookupSite(uint32_t hash) const;

  // Privacy sequence, at the end of every flush. Drops every name that has not
  // been recorded since the PREVIOUS sweep, then opens a new epoch. Two epochs
  // of grace, not one: an event whose name was recorded just after a flush's
  // drain is not aggregated until the following flush, and its name has to
  // still be there when it is. Returns how many entries were dropped.
  size_t SweepAndAdvance();

  size_t size() const;
  uint64_t dropped_count() const;

  // Called when the rows referencing these strings are gone, so the table does
  // not outlive them (the §5.2.2 site-GC argument applies here too).
  void Clear();

 private:
  friend class base::RefCountedThreadSafe<DomainStringTable>;
  ~DomainStringTable();

  struct Entry {
    std::string name;
    uint64_t epoch = 0;
  };
  using EntryMap = std::map<uint32_t, Entry>;

  void RecordInto(EntryMap& map,
                  size_t cap,
                  uint32_t hash,
                  std::string_view name) EXCLUSIVE_LOCKS_REQUIRED(lock_);
  static std::string LookupIn(const EntryMap& map, uint32_t hash);

  mutable base::Lock lock_;
  EntryMap entries_ GUARDED_BY(lock_);
  EntryMap sites_ GUARDED_BY(lock_);
  uint64_t epoch_ GUARDED_BY(lock_) = 0;
  uint64_t dropped_ GUARDED_BY(lock_) = 0;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_DOMAIN_STRING_TABLE_H_

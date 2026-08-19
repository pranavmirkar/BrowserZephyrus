// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_AGGREGATOR_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_AGGREGATOR_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "chrome/browser/zephyrus/privacy/domain_string_table.h"
#include "chrome/browser/zephyrus/privacy/entity_resolver.h"
#include "chrome/browser/zephyrus/privacy/privacy_database.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"

namespace zephyrus_privacy {

// The WARM path (§8.2): folds a stream of RawEvents into per-(site, domain)
// counters, resolves entities, and hands out DailyRows for the cold path to
// persist.
//
// Not thread-safe by design — it lives on the privacy sequence, reached only
// through the SequenceBound consumer.
//
// **Counting happens whether or not anything can be persisted.** §5.3 requires
// in-memory current-session statistics to keep working when the keystore is
// unavailable: they never touch disk, so they carry none of that risk. The
// aggregator therefore knows nothing about the database — it just accumulates
// and yields rows. Whether those rows reach SQLite is the caller's problem.
class PrivacyAggregator {
 public:
  // §8.6. Beyond this many distinct domains for one site, further domains fold
  // into a single "other" bucket instead of growing the map. Real pages rarely
  // pass ~100; the cap is also the §11.3 flooding defence.
  static constexpr size_t kMaxDomainsPerSite = 256;

  // Total sites tracked in memory before the oldest are dropped. A session
  // spanning hundreds of sites should not accumulate all of them.
  static constexpr size_t kMaxSites = 512;

  PrivacyAggregator(scoped_refptr<DomainStringTable> strings,
                    EntityResolver* resolver);
  PrivacyAggregator(const PrivacyAggregator&) = delete;
  PrivacyAggregator& operator=(const PrivacyAggregator&) = delete;
  ~PrivacyAggregator();

  void Add(const RawEvent& event);
  void AddBatch(base::span<const RawEvent> events);

  // Yields everything accumulated so far and resets. `day` is stamped on every
  // row; the caller supplies it so day bucketing stays testable and uses UTC
  // (§9.6).
  //
  // Domains whose string was never recorded, or was dropped at the string
  // table's cap, come back with an empty `request_domain`. They are still
  // counted — losing a label must not lose the event.
  std::vector<DailyRow> TakeRows(int64_t day);

  // §5.2.1's lifetime totals, as the amount to ADD since the last call. Taken
  // from the pre-cap session counters, not by summing the rows: an event that a
  // memory cap folded or dropped still happened, and the "since March" headline
  // must not drift below the truth because a page flooded us.
  struct LifetimeDelta {
    uint64_t detected = 0;
    uint64_t blocked = 0;
    uint64_t randomized = 0;
    bool empty() const { return !detected && !blocked && !randomized; }
  };
  LifetimeDelta TakeLifetimeDelta();

  // Live totals for the popup, which never queries the database (§8.9).
  struct SessionTotals {
    uint64_t detected = 0;
    uint64_t blocked = 0;
    uint64_t allowed = 0;
    uint64_t randomized = 0;
  };
  const SessionTotals& session_totals() const { return session_totals_; }

  size_t tracked_sites() const { return sites_.size(); }
  uint64_t folded_into_other() const { return folded_into_other_; }
  uint64_t dropped_sites() const { return dropped_sites_; }

  // §4.4 atomic swap. Safe because the aggregator, the consumer that owns the
  // resolver, and every Lookup all live on the privacy sequence: there is no
  // such thing as an in-flight reader here, so the old table can be freed as
  // soon as the pointer moves.
  void SetResolver(EntityResolver* resolver) { resolver_ = resolver; }

  void Clear();

 private:
  struct Counters {
    uint32_t detected = 0;
    uint32_t blocked = 0;
    uint32_t allowed = 0;
    uint32_t randomized = 0;
  };
  // domain_hash 0 is reserved for the "other" bucket: a real hash of 0 is
  // remapped on insert so the two can never be confused.
  using DomainMap = std::map<uint32_t, Counters>;

  scoped_refptr<DomainStringTable> strings_;
  raw_ptr<EntityResolver> resolver_;

  std::map<uint32_t, DomainMap> sites_;
  SessionTotals session_totals_;
  // What session_totals_ read at the last TakeLifetimeDelta. session_totals_
  // itself must keep growing for the popup, so the delta is a watermark rather
  // than a reset counter.
  SessionTotals lifetime_watermark_;
  uint64_t folded_into_other_ = 0;
  uint64_t dropped_sites_ = 0;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_AGGREGATOR_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_aggregator.h"
#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"

#include <utility>

#include "base/hash/hash.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace zephyrus_privacy {

namespace {

// The bucket every domain past the per-site cap folds into. A real domain that
// hashes to 0 is remapped to 1 on insert so it cannot collide with this.
constexpr uint32_t kOtherBucket = 0;

// Site id 0 is the synthetic site the emission point uses when there is no
// top-level document (about:, an IP literal, a service worker). It is a real,
// meaningful category — not a lookup failure — so it gets its own name.
constexpr uint32_t kNoSiteId = 0;

}  // namespace

PrivacyAggregator::PrivacyAggregator(scoped_refptr<DomainStringTable> strings,
                                     EntityResolver* resolver)
    : strings_(std::move(strings)), resolver_(resolver) {}

PrivacyAggregator::~PrivacyAggregator() = default;

void PrivacyAggregator::Add(const RawEvent& event) {
  // Session totals are counted first and unconditionally. Even an event whose
  // site or domain gets dropped by a cap really happened, and the headline
  // number must not quietly under-report because of a memory bound.
  switch (static_cast<TrackerStatus>(event.status)) {
    case TrackerStatus::kDetected:
      ++session_totals_.detected;
      break;
    case TrackerStatus::kBlocked:
      ++session_totals_.blocked;
      break;
    case TrackerStatus::kAllowed:
      ++session_totals_.allowed;
      break;
    case TrackerStatus::kRandomized:
      ++session_totals_.randomized;
      break;
    case TrackerStatus::kPotential:
      // Excluded from headline numbers by §6.5: a heuristic match is not a
      // fact, and counting it would overstate what we observed.
      break;
  }

  auto site_it = sites_.find(event.site_id);
  if (site_it == sites_.end()) {
    if (sites_.size() >= kMaxSites) {
      ++dropped_sites_;
      return;
    }
    site_it = sites_.emplace(event.site_id, DomainMap()).first;
  }
  DomainMap& domains = site_it->second;

  uint32_t key = event.domain_hash ? event.domain_hash : 1u;
  if (domains.find(key) == domains.end() &&
      domains.size() >= kMaxDomainsPerSite) {
    // §8.6: fold rather than grow. The counts survive; only the per-domain
    // breakdown is lost, which is the right thing to give up under a flood.
    key = kOtherBucket;
    ++folded_into_other_;
  }

  Counters& counters = domains[key];
  switch (static_cast<TrackerStatus>(event.status)) {
    case TrackerStatus::kDetected:
      ++counters.detected;
      break;
    case TrackerStatus::kBlocked:
      ++counters.blocked;
      break;
    case TrackerStatus::kAllowed:
      ++counters.allowed;
      break;
    case TrackerStatus::kRandomized:
      ++counters.randomized;
      break;
    case TrackerStatus::kPotential:
      break;
  }
}

void PrivacyAggregator::AddBatch(base::span<const RawEvent> events) {
  for (const RawEvent& event : events) {
    Add(event);
  }
}

std::vector<DailyRow> PrivacyAggregator::TakeRows(int64_t day) {
  std::vector<DailyRow> rows;
  for (const auto& [site_id, domains] : sites_) {
    for (const auto& [domain_hash, counters] : domains) {
      DailyRow row;
      row.day = day;
      if (site_id == kNoSiteId) {
        row.site_etld1 = kNoSiteName;
      } else {
        // Empty when the name was never recorded or was dropped at the string
        // table's cap; the database substitutes "(unknown site)" rather than
        // discarding the counts.
        row.site_etld1 = strings_->LookupSite(site_id);
      }
      row.detected = counters.detected;
      row.blocked = counters.blocked;
      row.allowed = counters.allowed;
      row.randomized = counters.randomized;

      if (domain_hash == kOtherBucket) {
        // Named, not blank: a blank row is indistinguishable from a lookup
        // failure, and the user is entitled to know the tail was collapsed.
        row.request_domain = "(other)";
      } else {
        // Empty when the string was never recorded or was dropped at the
        // table's cap. Still emitted — losing a label must not lose the event.
        row.request_domain = strings_->Lookup(domain_hash);
      }

      // Exact hash, then a fold to the registrable domain. Shared with the
      // popup's per-page analysis, which has to resolve the same domains the
      // same way — see LookupEntityWithFallback for why the fold is load
      // bearing. Cold path only: once per (site, domain) per flush, never per
      // request.
      const EntityInfo info =
          LookupEntityWithFallback(resolver_, domain_hash, row.request_domain);
      row.entity_id = info.entity_id;
      row.category = info.category;
      rows.push_back(std::move(row));
    }
  }
  sites_.clear();
  // Every name these rows needed has now been read, so retire the ones nobody
  // has re-recorded. Keeps the channel from becoming a standing plaintext log
  // of the session's browsing (see DomainStringTable).
  strings_->SweepAndAdvance();
  return rows;
}

PrivacyAggregator::LifetimeDelta PrivacyAggregator::TakeLifetimeDelta() {
  LifetimeDelta delta;
  // DETECTED here is the SUPERSET, not the leftover bucket.
  //
  // §2.1 defines DETECTED as "request made or script seen" and ALLOWED as
  // "request completed; data left the device" — so every allowed request was
  // also detected. §5.2.1's lifetime row has no `allowed` column, and its
  // "since March" headline means "trackers we saw", which has to include the
  // ones that succeeded.
  //
  // This matters because it silently broke once. While the emission point
  // guessed at request time, every unblocked request was recorded kDetected and
  // this line happened to count them all. Once the request observer started
  // reporting real outcomes, the same line began counting only the requests
  // that FAILED — a real page went from 137 to 6 — and nothing failed, the
  // headline just quietly shrank.
  const uint64_t observed = session_totals_.detected + session_totals_.allowed;
  const uint64_t observed_watermark =
      lifetime_watermark_.detected + lifetime_watermark_.allowed;
  delta.detected = observed - observed_watermark;
  delta.blocked = session_totals_.blocked - lifetime_watermark_.blocked;
  delta.randomized = session_totals_.randomized - lifetime_watermark_.randomized;
  lifetime_watermark_ = session_totals_;
  return delta;
}

void PrivacyAggregator::Clear() {
  sites_.clear();
  session_totals_ = SessionTotals();
  // Must reset with the totals: a stale watermark above a zeroed total would
  // underflow the unsigned subtraction into billions on the next flush.
  lifetime_watermark_ = SessionTotals();
  folded_into_other_ = 0;
  dropped_sites_ = 0;
}

}  // namespace zephyrus_privacy

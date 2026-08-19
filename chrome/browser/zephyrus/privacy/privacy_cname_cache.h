// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CNAME_CACHE_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CNAME_CACHE_H_

#include <stddef.h>

#include <list>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "base/memory/ref_counted.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"

namespace zephyrus_privacy {

// §9.1: what a hostname really resolves to.
//
// **The problem.** Trackers hide behind a first-party subdomain —
// `metrics.example.com` CNAMEd to a tracker-owned host. Domain-based
// classification sees a same-site request, skips it as first-party, and reports
// the page as clean. The spec calls this "the single largest correctness gap in
// a domain-matching design", and it is worse than a miss: the page looks clean
// *because* the tracker hid well.
//
// **Why a cache is the whole design.** The canonical name is only visible on a
// response, which means intercepting the loader — and intercepting every
// request to learn something that changes about once per host per session is
// how a privacy feature becomes a performance problem. §9.1 is explicit: cache
// `hostname -> canonical eTLD+1` "so classification is one lookup after the
// first request". So the interceptor is attached ONLY on a miss here, and the
// steady state adds nothing to the request path.
//
// UI thread only. Both users — the factory deciding whether to intercept, and
// the tab helper classifying a completed load — already live there.
class PrivacyCnameCache : public base::RefCounted<PrivacyCnameCache> {
 public:
  PrivacyCnameCache();
  PrivacyCnameCache(const PrivacyCnameCache&) = delete;
  PrivacyCnameCache& operator=(const PrivacyCnameCache&) = delete;

  // Whether `host` still needs its canonical name harvested. True on a miss and
  // on an expired entry; the caller attaches an interceptor only then.
  bool NeedsResolution(std::string_view host) const;

  // The registrable domain `host` really resolves to, when that differs from
  // the host's own. nullopt means either "not known yet" or "not cloaked" —
  // both of which the caller treats identically, by classifying the host as
  // itself. Distinguishing them would only matter to a diagnostic.
  std::optional<std::string> CanonicalEtld1(std::string_view host) const;

  // Records what a response revealed. `canonical_etld1` empty means the host is
  // not cloaked, which is worth remembering: it is the answer for the vast
  // majority of hosts and caching it is what keeps the interceptor off them.
  void Record(std::string_view host, std::string canonical_etld1);

  // Derives the canonical registrable domain from a response's DNS alias list,
  // or empty when the host is not cloaked.
  //
  // `aliases` comes straight off URLResponseHead::dns_aliases, whose first
  // entry is the canonical name. Returns empty when the canonical name folds to
  // the same registrable domain as `host` — a CDN alias inside one company is
  // not cloaking, and reporting it as a hidden third party would invent a
  // tracker where none exists.
  static std::string CanonicalFromAliases(
      std::string_view host,
      const std::vector<std::string>& aliases);

  size_t size_for_testing() const { return entries_.size(); }

 private:
  friend class base::RefCounted<PrivacyCnameCache>;
  ~PrivacyCnameCache();

  // Bounded so a session that touches endless hosts cannot grow it forever.
  // 512 is far above the distinct hosts of any real page and matches the
  // aggregator's site cap.
  static constexpr size_t kMaxEntries = 512;

  // §9.1 asks for the DNS TTL to be respected. URLResponseHead::dns_aliases
  // does not carry one, and asking the resolver for it would be the second
  // lookup §9.1 forbids — so this is a fixed ceiling instead, chosen short
  // enough that a tracker moving hosts is picked up within the hour, and long
  // enough that the interceptor stays off the hot path. Deliberately an
  // approximation, and named as one.
  static constexpr base::TimeDelta kEntryLifetime = base::Hours(1);

  struct Entry {
    // Empty means "resolved, and not cloaked".
    std::string canonical_etld1;
    base::TimeTicks recorded_at;
    // Position in `lru_`, so a hit is O(log n) for the map plus O(1) to
    // promote.
    std::list<std::string>::iterator lru_position;
  };

  void Evict();

  SEQUENCE_CHECKER(sequence_checker_);
  std::map<std::string, Entry, std::less<>> entries_;
  // Front is most recently used.
  mutable std::list<std::string> lru_;
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CNAME_CACHE_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The §8.1 budget guard for the request path.
//
// §8.1 makes "< 20 µs added latency per intercepted request (p99)" a release
// blocker, and §12.4 says the budget has to be measured in CI rather than
// asserted in a design doc: "a perf budget nobody measures is a wish."
//
// **Scope.** This measures the whole per-request cost the feature adds at the
// emission point: hashing, publishing the names, and pushing the event. It does
// NOT replay the §8.11 corpus of 50 heavy real-world pages — that needs a
// captured corpus and a browser test, and is tracked separately. What it does
// cover is the part that runs 200-400 times per page load, which is where the
// budget can actually be blown.
//
// **Why the margin is enormous.** §8.3 puts the real cost at tens of
// nanoseconds and calls 20 µs "a ceiling with a wide margin, not a target". A
// ~400x margin is also what keeps this from flaking on a loaded CI machine: a
// failure here means someone put real work on the hot path, not that the bot
// was busy.

#include <stdint.h>

#include <algorithm>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/hash/hash.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/privacy/domain_string_table.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_event_sink.h"
#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

// §8.1.
constexpr base::TimeDelta kP99Budget = base::Microseconds(20);

// A content-heavy page fires 200-400 requests against a few dozen distinct
// domains (§5.1). Enough iterations that a p99 means something.
constexpr int kRequests = 200000;
constexpr int kDistinctDomains = 64;

// Exactly what ZephyrusAdblockProxyingURLLoaderFactory::RecordPrivacyEvent does
// per request, minus the RenderFrameHost walk that is memoized per document.
// Kept in step with that function by hand; if the two drift, this measures the
// wrong thing.
void EmitOne(PrivacyEventSink* sink,
             DomainStringTable* strings,
             const std::string& host,
             uint32_t site_id,
             const std::string& site_name) {
  RawEvent event{};
  event.site_id = site_id;
  // Through the canonicaliser, exactly as RecordPrivacyEvent does. Calling
  // PersistentHash directly here would quietly stop measuring the real path.
  event.domain_hash = base::PersistentHash(CanonicalHostForHash(host));
  strings->RecordSite(site_id, site_name);
  strings->Record(event.domain_hash ? event.domain_hash : 1u, host);
  event.ticks_delta_ms = 1234;
  event.entity_id = kNoEntity;
  event.category = static_cast<uint8_t>(Category::kUnknown);
  event.status = static_cast<uint8_t>(TrackerStatus::kDetected);
  sink->Record(event);
}

TEST(PrivacyHotPathPerfTest, AddedLatencyPerRequestStaysUnderBudget) {
  auto sink = base::MakeRefCounted<PrivacyEventSink>();
  auto strings = base::MakeRefCounted<DomainStringTable>();

  std::vector<std::string> hosts;
  hosts.reserve(kDistinctDomains);
  for (int i = 0; i < kDistinctDomains; ++i) {
    hosts.push_back("tracker" + base::NumberToString(i) + ".example.com");
  }
  const std::string site_name = "news.example";
  const uint32_t site_id = base::PersistentHash(site_name);

  // Warm up: first sighting of each name allocates once, by design. The budget
  // is about the steady state, and an actively-requested name is never swept
  // out of the table, so the steady state really is allocation-free.
  for (const std::string& host : hosts) {
    EmitOne(sink.get(), strings.get(), host, site_id, site_name);
  }

  std::vector<int64_t> samples;
  samples.reserve(kRequests);
  const base::TimeTicks loop_start = base::TimeTicks::Now();
  for (int i = 0; i < kRequests; ++i) {
    // The ring is 4096 deep and nothing is draining it here; let it wrap by
    // draining periodically, or every sample after the first 4096 would be
    // measuring the "full, drop it" path instead of the real one.
    if (i % 2048 == 0) {
      std::vector<RawEvent> scratch(4096);
      sink->Drain(base::span<RawEvent>(scratch));
    }
    const std::string& host = hosts[i % kDistinctDomains];
    const base::TimeTicks start = base::TimeTicks::Now();
    EmitOne(sink.get(), strings.get(), host, site_id, site_name);
    samples.push_back((base::TimeTicks::Now() - start).InNanoseconds());
  }

  // Per-request timing on Windows sits at base::TimeTicks resolution (~1 µs),
  // so the percentiles below quantise to 0 or 1000 ns. That is still a valid
  // and conservative assertion — p99 within one tick IS under the 20 µs budget
  // — but it cannot report an actual cost. The amortized figure can: it times
  // the whole loop and divides, which sees straight through the resolution
  // floor. Both are printed so a regression is legible either way.
  const base::TimeTicks loop_end = base::TimeTicks::Now();
  const int64_t total_ns = (loop_end - loop_start).InNanoseconds();
  const double amortized_ns = static_cast<double>(total_ns) / kRequests;

  std::sort(samples.begin(), samples.end());
  const int64_t p50 = samples[samples.size() / 2];
  const int64_t p99 = samples[samples.size() * 99 / 100];
  const int64_t p999 = samples[samples.size() * 999 / 1000];

  LOG(INFO) << "Zephyrus privacy hot path over " << kRequests
            << " requests: p50=" << p50 << "ns p99=" << p99
            << "ns p99.9=" << p999 << "ns; amortized=" << amortized_ns
            << "ns/request (budget " << kP99Budget << " at p99)";

  EXPECT_LT(amortized_ns, static_cast<double>(kP99Budget.InNanoseconds()))
      << "§8.3 budgets the steady-state cost at tens of nanoseconds. An "
         "amortized cost anywhere near the 20 µs ceiling means the hot path is "
         "doing real work.";

  EXPECT_LT(p99, kP99Budget.InNanoseconds())
      << "§8.1: added latency per intercepted request must stay under 20 µs at "
         "p99. Something expensive landed on the request path — §8.3 allows a "
         "lookup and a push, and nothing else: no allocation, no lock held "
         "across work, no string comparison, no logging.";

  // §8.1 also forbids database writes on the request path. Nothing here can
  // reach the database — the sink only owns a ring buffer — so the guard that
  // matters is structural: if this ever needs a Profile or a service to
  // compile, the layering that keeps SQLite off this path has been broken.
  EXPECT_EQ(0u, sink->dropped_count())
      << "the drain cadence in this test is wrong, so the numbers above are "
         "measuring the ring-full path rather than the real one";
}

}  // namespace
}  // namespace zephyrus_privacy

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Standalone ThreadSanitizer harness for PrivacyRingBuffer.
//
// Spec §12.4: "hand-written lock-free code without TSAN coverage is not
// production code." TSAN is Linux-only in Chromium
// (build/config/sanitizers/BUILD.gn asserts `is_linux || is_chromeos`), and
// Zephyrus is developed on Windows — so this harness exists to run the real
// implementation under TSAN outside the Chromium build.
//
// It compiles the ACTUAL privacy_ring_buffer.{h,cc}, not a copy, so it cannot
// drift from what ships. The only substitution is a minimal base::span shim in
// tools/shim/.
//
// Build and run (WSL / any Linux box with clang or gcc):
//
//   cd chrome/browser/zephyrus/privacy/tools
//   clang++ -std=c++20 -fsanitize=thread -g -O1 \
//       -I shim -I ../../../../.. \
//       ring_buffer_tsan_harness.cc ../privacy_ring_buffer.cc \
//       -o /tmp/ring_tsan && /tmp/ring_tsan
//
// (g++ works too; substitute g++ for clang++.)
//
// A clean run prints "PASS" and TSAN prints nothing. ANY "WARNING:
// ThreadSanitizer: data race" output is a failure, even if the assertions pass
// — a race that happens to produce correct output today is still a race.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "chrome/browser/zephyrus/privacy/privacy_ring_buffer.h"

namespace {

using zephyrus_privacy::PrivacyRingBuffer;
using zephyrus_privacy::RawEvent;

// Enough iterations to cycle the 4096-slot ring hundreds of times, so the
// producer and consumer repeatedly meet at the full and empty boundaries —
// which is where the acquire/release pairing actually matters.
constexpr uint32_t kTotalEvents = 2'000'000;

RawEvent MakeEvent(uint32_t seq) {
  RawEvent e{};
  e.site_id = seq;
  e.domain_hash = ~seq;  // Complement: detects a torn or interleaved write.
  e.ticks_delta_ms = seq * 3;
  e.entity_id = static_cast<uint16_t>(seq & 0xFFFF);
  e.category = 1;
  e.status = 1;
  return e;
}

int g_failures = 0;

void Fail(const char* what, uint32_t at) {
  std::fprintf(stderr, "FAIL: %s at seq %u\n", what, at);
  if (++g_failures > 20) {
    std::fprintf(stderr, "too many failures, aborting\n");
    std::exit(1);
  }
}

}  // namespace

int main() {
  PrivacyRingBuffer ring;
  std::atomic<uint64_t> dropped_by_producer{0};

  // Producer: exactly one thread, as the contract requires. Unlike the
  // in-tree unit test it does NOT spin on a full buffer — it drops, which is
  // production behaviour (§8.5) and keeps the two threads genuinely racing at
  // the boundary instead of lock-stepping.
  std::thread producer([&] {
    for (uint32_t i = 0; i < kTotalEvents; ++i) {
      if (!ring.Push(MakeEvent(i))) {
        dropped_by_producer.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  // Consumer: exactly one thread. Verifies that whatever arrives is intact and
  // strictly increasing. Drops are expected and legal; going backwards,
  // repeating, or arriving half-written is not.
  uint64_t received = 0;
  std::thread consumer([&] {
    std::vector<RawEvent> out(256);
    int64_t last_seq = -1;
    uint64_t seen = 0;
    while (seen + dropped_by_producer.load(std::memory_order_relaxed) <
           kTotalEvents) {
      const size_t got = ring.Drain(base::span<RawEvent>(out));
      for (size_t i = 0; i < got; ++i) {
        const RawEvent& e = out[i];
        if (e.domain_hash != ~e.site_id) {
          Fail("torn write: domain_hash does not complement site_id",
               e.site_id);
        }
        if (e.ticks_delta_ms != e.site_id * 3) {
          Fail("torn write: ticks_delta_ms inconsistent", e.site_id);
        }
        if (static_cast<int64_t>(e.site_id) <= last_seq) {
          Fail("out of order or duplicated", e.site_id);
        }
        last_seq = e.site_id;
        ++seen;
      }
      if (got == 0) {
        std::this_thread::yield();
      }
    }
    received = seen;
  });

  producer.join();
  consumer.join();

  const uint64_t dropped = ring.dropped_count();
  std::printf("received=%llu dropped=%llu total=%u peak_depth=%u\n",
              static_cast<unsigned long long>(received),
              static_cast<unsigned long long>(dropped), kTotalEvents,
              ring.peak_depth());

  // Nothing may be invented or lost without being counted.
  if (received + dropped != kTotalEvents) {
    std::fprintf(stderr,
                 "FAIL: received + dropped (%llu) != total (%u) — events "
                 "vanished without being counted as drops\n",
                 static_cast<unsigned long long>(received + dropped),
                 kTotalEvents);
    ++g_failures;
  }
  if (ring.peak_depth() > PrivacyRingBuffer::kCapacity) {
    std::fprintf(stderr, "FAIL: peak depth %u exceeded capacity %zu\n",
                 ring.peak_depth(), PrivacyRingBuffer::kCapacity);
    ++g_failures;
  }

  if (g_failures) {
    std::fprintf(stderr, "FAILED with %d error(s)\n", g_failures);
    return 1;
  }
  std::printf("PASS (assertions only — check for TSAN warnings above)\n");
  return 0;
}

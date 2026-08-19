// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_RING_BUFFER_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_RING_BUFFER_H_

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <atomic>
#include <new>

#include "base/dcheck_is_on.h"
#include "base/sequence_checker.h"

#if DCHECK_IS_ON()
#include <functional>
#include <thread>
#endif

#include "base/containers/span.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"

namespace zephyrus_privacy {

// Spec §8.5. The handoff from the network thread to the privacy sequence.
//
// Single-producer / single-consumer, lock-free, fixed capacity, allocated once
// and never grown. Exactly one thread may call Push() and exactly one other
// may call Drain(); this is not a general-purpose MPMC queue and will corrupt
// if used as one.
//
// **A full buffer drops the event and increments a counter.** It never blocks,
// never allocates an overflow, and never grows. Losing statistics is
// tolerable; stalling a page load is not — backpressure on the network thread
// is the failure mode this design exists to prevent. Sustained drops mean the
// consumer is too slow, which is a bug to fix rather than a reason to enlarge
// the buffer (§8.12).
//
// Hand-written lock-free code is not production code without TSAN coverage
// (§12.4); see privacy_ring_buffer_unittest.cc.
class PrivacyRingBuffer {
 public:
  // 4096 × 16 bytes = 64 KB. Power of two so the index masks.
  static constexpr size_t kCapacity = 4096;
  static_assert((kCapacity & (kCapacity - 1)) == 0,
                "kCapacity must be a power of two for the index mask");

  PrivacyRingBuffer();
  PrivacyRingBuffer(const PrivacyRingBuffer&) = delete;
  PrivacyRingBuffer& operator=(const PrivacyRingBuffer&) = delete;
  ~PrivacyRingBuffer();

  // Producer side. Must always be called from the SAME thread. Returns false
  // if the event was dropped because the buffer was full. Callers on the network thread must ignore the return value and
  // carry on — it exists for tests and for the internals page.
  //
  // Wait-free: two atomic loads, one store, one release.
  bool Push(const RawEvent& event);

  // Consumer side. Copies up to `out.size()` events into `out` and returns how
  // many were written.
  size_t Drain(base::span<RawEvent> out);

  // Consumer side. Approximate by nature — the producer may push concurrently.
  size_t ApproximateSize() const;

  // Total events dropped for a full buffer since construction. Surfaced by
  // chrome://privacy-internals (§8.11) and drives the §13.3 kill switch.
  uint64_t dropped_count() const {
    return dropped_.load(std::memory_order_relaxed);
  }

  // High-water mark of occupancy, for the internals page. Written by the
  // producer only.
  // Total events ever pushed. `tail_` only ever moves forward and only the
  // producer writes it, so it IS the count — no separate counter needed on the
  // hot path. Wraps after 2^32 events, same as the depth arithmetic already
  // does; a diagnostic counter is the right place to accept that.
  uint32_t recorded_count() const {
    return tail_.load(std::memory_order_relaxed);
  }

  uint32_t peak_depth() const {
    return peak_depth_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr uint32_t kMask = static_cast<uint32_t>(kCapacity - 1);

  std::array<RawEvent, kCapacity> buffer_;

  // Monotonically increasing sequence numbers, masked into the array. Unsigned
  // wraparound is well defined and the difference stays correct across it, so
  // no special handling is needed at 2^32 pushes.
  //
  // `head_` is written only by the consumer, `tail_` only by the producer.
  //
  // Each sits on its own cache line. Adjacent, they would share one, and every
  // producer store would invalidate the consumer's copy and vice versa —
  // cache-line ping-pong on the busiest path in the browser. The padding costs
  // ~128 bytes once.
  alignas(64) std::atomic<uint32_t> head_{0};
  alignas(64) std::atomic<uint32_t> tail_{0};

  // Producer-owned diagnostics, kept off both hot lines.
  alignas(64) std::atomic<uint64_t> dropped_{0};
  std::atomic<uint32_t> peak_depth_{0};

#if DCHECK_IS_ON()
  // The SPSC contract is a correctness requirement, not a convention: a second
  // producer silently corrupts the buffer rather than failing loudly.
  //
  // **SEQUENCE, not thread.** An earlier version of this pinned the physical
  // thread id of the first caller, which crashed every DCHECK-enabled build the
  // moment the feature was switched on: the consumer runs through
  // base::SequenceBound on a thread-pool sequence, and a sequence guarantees
  // that its tasks never overlap — not that they run on the same thread. The
  // second task legitimately lands on a different worker.
  //
  // Mutual exclusion is what this buffer actually needs, and a sequence gives
  // exactly that; the memory ordering comes from the acquire/release pairs
  // below plus the happens-before that task posting establishes. SequenceChecker
  // is the right granularity, and it still catches a genuine second consumer.
  // It also degrades correctly for the producer, which is a bare thread rather
  // than a sequence.
  SEQUENCE_CHECKER(producer_sequence_);
  SEQUENCE_CHECKER(consumer_sequence_);
#endif
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_RING_BUFFER_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_EVENT_SINK_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_EVENT_SINK_H_

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "base/containers/span.h"
#include "base/memory/ref_counted.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_ring_buffer.h"

namespace zephyrus_privacy {

// The refcounted boundary between the network thread and the privacy sequence.
//
// The ring buffer must outlive both sides, and the two sides have unrelated
// lifetimes: the emission point lives with a URLLoaderFactory on the network
// thread, while the consumer lives with a KeyedService destroyed at profile
// shutdown. Owning the ring directly in the service would leave the network
// thread writing into freed memory during teardown — a use-after-free on a
// path a hostile page can drive at will.
//
// So neither side owns it. Both hold a reference, the ring dies with the last
// one, and a late Push() after service shutdown lands in a buffer nobody will
// ever drain. That is deliberate: dropping events during teardown is correct,
// crashing is not.
class PrivacyEventSink : public base::RefCountedThreadSafe<PrivacyEventSink> {
 public:
  PrivacyEventSink();
  PrivacyEventSink(const PrivacyEventSink&) = delete;
  PrivacyEventSink& operator=(const PrivacyEventSink&) = delete;

  // Network thread (the single producer). Never blocks, never allocates. The
  // return value is for tests and internals; production callers ignore it.
  //
  // Returns false immediately once the §13.3 kill switch has tripped.
  bool Record(const RawEvent& event);

  // §13.3 kill switch, producer side. Called once from the service sequence
  // when the pipeline is judged to be degrading browsing; never reset, because
  // "for the session" is the whole point — a switch that flapped back on would
  // reintroduce the very cost it tripped over.
  //
  // Cutting collection HERE rather than at the drain is deliberate. Stopping
  // the consumer would leave the producer still pushing into a ring nobody
  // empties, so the network thread would keep paying for an event that is
  // guaranteed to be discarded. §13.1's DISABLED state means "no interception
  // hook, zero cost", and this is as close to it as a running session gets.
  //
  // The cost of the check itself is one relaxed atomic load on a line that is
  // never written after startup, so it stays in every core's cache.
  void DisableForSession();
  bool disabled_for_session() const {
    return disabled_.load(std::memory_order_relaxed);
  }

  // Privacy sequence (the single consumer).
  size_t Drain(base::span<RawEvent> out);

  size_t ApproximateDepth() const { return ring_.ApproximateSize(); }
  uint64_t dropped_count() const { return ring_.dropped_count(); }
  uint32_t recorded_count() const { return ring_.recorded_count(); }
  uint32_t peak_depth() const { return ring_.peak_depth(); }

 private:
  friend class base::RefCountedThreadSafe<PrivacyEventSink>;
  ~PrivacyEventSink();

  PrivacyRingBuffer ring_;

  // Written once by the service sequence, read by the producer. Relaxed on
  // both sides: there is nothing to synchronise WITH — no other memory is
  // published by the flip, and the exact event on which the producer first
  // observes it does not matter.
  std::atomic<bool> disabled_{false};
};

// Milliseconds since a PROCESS-WIDE epoch, for RawEvent::ticks_delta_ms.
//
// Monotonic, never wall clock: an NTP correction or a user changing the system
// clock would otherwise produce negative or absurd values (§9.6). Saturating,
// so a session running past ~49 days stops climbing rather than restarting at
// zero.
//
// Process-wide rather than per-emitter, and shared rather than duplicated: two
// emitters computing deltas from two different epochs produce values the
// consumer cannot order against each other, which is the only thing it ever
// does with them.
uint32_t PrivacyTicksDeltaMs();

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_EVENT_SINK_H_

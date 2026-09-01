// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_ring_buffer.h"
#include "base/check_op.h"

#include <algorithm>

#include "base/check.h"

namespace zephyrus_privacy {

PrivacyRingBuffer::PrivacyRingBuffer() : buffer_{} {
#if DCHECK_IS_ON()
  // Both sides are detached at construction so each binds to whichever
  // sequence FIRST uses it, rather than to the one that happened to build the
  // buffer. The ring is constructed on the UI thread but produced on the
  // network thread and consumed on the privacy sequence, so binding at
  // construction would fail on the very first real use of either side.
  DETACH_FROM_SEQUENCE(producer_sequence_);
  DETACH_FROM_SEQUENCE(consumer_sequence_);
#endif
}

PrivacyRingBuffer::~PrivacyRingBuffer() = default;

bool PrivacyRingBuffer::Push(const RawEvent& event) {
  // Single PRODUCER. See the sequence-vs-thread note in the header.
  // A second producer would corrupt the buffer silently rather than failing.
  DCHECK_CALLED_ON_VALID_SEQUENCE(producer_sequence_);
  // Only this thread writes tail_, so a relaxed load of our own value is
  // enough. head_ must be acquired: it tells us which slots the consumer has
  // released, and we must not see that release before the reads it published.
  const uint32_t tail = tail_.load(std::memory_order_relaxed);
  const uint32_t head = head_.load(std::memory_order_acquire);
  const uint32_t depth = tail - head;  // Correct across unsigned wraparound.

  if (depth >= kCapacity) {
    // Full. Drop, count, return — never block, never allocate, never grow.
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  buffer_[tail & kMask] = event;

  // Release: the slot write above must be visible to the consumer before it
  // can observe the new tail. This store is what publishes the event.
  tail_.store(tail + 1, std::memory_order_release);

  const uint32_t new_depth = depth + 1;
  if (new_depth > peak_depth_.load(std::memory_order_relaxed)) {
    peak_depth_.store(new_depth, std::memory_order_relaxed);
  }
  return true;
}

size_t PrivacyRingBuffer::Drain(base::span<RawEvent> out) {
  // Single CONSUMER. A sequence, not a thread: SequenceBound legitimately
  // runs successive tasks on different pool workers.
  // A second consumer would corrupt the buffer silently rather than failing.
  DCHECK_CALLED_ON_VALID_SEQUENCE(consumer_sequence_);
  const uint32_t head = head_.load(std::memory_order_relaxed);
  // Acquire pairs with the producer's release store, so every slot below the
  // observed tail is fully written before we read it.
  const uint32_t tail = tail_.load(std::memory_order_acquire);

  const size_t available = static_cast<size_t>(tail - head);
  const size_t count = std::min(available, out.size());
  for (size_t i = 0; i < count; ++i) {
    out[i] = buffer_[(head + static_cast<uint32_t>(i)) & kMask];
  }

  // Release: the copies above must complete before the producer can observe
  // these slots as free and overwrite them.
  head_.store(head + static_cast<uint32_t>(count), std::memory_order_release);
  return count;
}

size_t PrivacyRingBuffer::ApproximateSize() const {
  const uint32_t tail = tail_.load(std::memory_order_acquire);
  const uint32_t head = head_.load(std::memory_order_relaxed);
  return static_cast<size_t>(tail - head);
}

}  // namespace zephyrus_privacy

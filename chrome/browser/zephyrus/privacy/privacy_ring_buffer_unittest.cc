// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_ring_buffer.h"

#include <atomic>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/threading/simple_thread.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

RawEvent MakeEvent(uint32_t seq) {
  // site_id doubles as a sequence number so the consumer can prove ordering,
  // and domain_hash as its complement so a torn write is detectable: any
  // half-written slot fails the invariant below.
  RawEvent e{};
  e.site_id = seq;
  e.domain_hash = ~seq;
  e.ticks_delta_ms = seq * 3;
  e.entity_id = static_cast<uint16_t>(seq & 0xFFFF);
  e.category = static_cast<uint8_t>(Category::kAdvertising);
  e.status = static_cast<uint8_t>(TrackerStatus::kBlocked);
  return e;
}

// Every field survived the trip, and the two derived fields still agree.
void ExpectIntact(const RawEvent& e, uint32_t expected_seq) {
  EXPECT_EQ(expected_seq, e.site_id);
  EXPECT_EQ(~expected_seq, e.domain_hash) << "torn or interleaved write";
  EXPECT_EQ(expected_seq * 3, e.ticks_delta_ms);
  EXPECT_EQ(static_cast<uint16_t>(expected_seq & 0xFFFF), e.entity_id);
}

TEST(PrivacyRingBufferTest, EmptyDrainYieldsNothing) {
  PrivacyRingBuffer ring;
  std::vector<RawEvent> out(16);
  EXPECT_EQ(0u, ring.Drain(out));
  EXPECT_EQ(0u, ring.ApproximateSize());
  EXPECT_EQ(0u, ring.dropped_count());
}

TEST(PrivacyRingBufferTest, PushThenDrainPreservesOrderAndContent) {
  PrivacyRingBuffer ring;
  for (uint32_t i = 0; i < 100; ++i) {
    EXPECT_TRUE(ring.Push(MakeEvent(i)));
  }
  EXPECT_EQ(100u, ring.ApproximateSize());

  std::vector<RawEvent> out(100);
  ASSERT_EQ(100u, ring.Drain(out));
  for (uint32_t i = 0; i < 100; ++i) {
    ExpectIntact(out[i], i);
  }
  EXPECT_EQ(0u, ring.ApproximateSize());
}

TEST(PrivacyRingBufferTest, PartialDrainLeavesTheRemainder) {
  PrivacyRingBuffer ring;
  for (uint32_t i = 0; i < 50; ++i) {
    ring.Push(MakeEvent(i));
  }
  std::vector<RawEvent> out(20);
  ASSERT_EQ(20u, ring.Drain(out));
  for (uint32_t i = 0; i < 20; ++i) {
    ExpectIntact(out[i], i);
  }
  EXPECT_EQ(30u, ring.ApproximateSize());

  std::vector<RawEvent> rest(30);
  ASSERT_EQ(30u, ring.Drain(rest));
  ExpectIntact(rest[0], 20);
  ExpectIntact(rest[29], 49);
}

// §8.5: a full buffer drops and counts. It must never block, grow, or
// overwrite an event the consumer has not read.
TEST(PrivacyRingBufferTest, FullBufferDropsInsteadOfOverwriting) {
  PrivacyRingBuffer ring;
  for (size_t i = 0; i < PrivacyRingBuffer::kCapacity; ++i) {
    ASSERT_TRUE(ring.Push(MakeEvent(static_cast<uint32_t>(i))));
  }
  EXPECT_EQ(PrivacyRingBuffer::kCapacity, ring.ApproximateSize());

  for (int i = 0; i < 500; ++i) {
    EXPECT_FALSE(ring.Push(MakeEvent(999999)));
  }
  EXPECT_EQ(500u, ring.dropped_count());
  EXPECT_EQ(PrivacyRingBuffer::kCapacity, ring.ApproximateSize());

  // The retained events are the FIRST kCapacity, not the most recent: dropping
  // must not corrupt what is already queued.
  std::vector<RawEvent> out(PrivacyRingBuffer::kCapacity);
  ASSERT_EQ(PrivacyRingBuffer::kCapacity, ring.Drain(out));
  for (size_t i = 0; i < PrivacyRingBuffer::kCapacity; ++i) {
    ExpectIntact(out[i], static_cast<uint32_t>(i));
  }
}

TEST(PrivacyRingBufferTest, RecoversAfterDrainingAFullBuffer) {
  PrivacyRingBuffer ring;
  for (size_t i = 0; i < PrivacyRingBuffer::kCapacity; ++i) {
    ring.Push(MakeEvent(static_cast<uint32_t>(i)));
  }
  EXPECT_FALSE(ring.Push(MakeEvent(0)));

  std::vector<RawEvent> out(PrivacyRingBuffer::kCapacity);
  ring.Drain(out);
  EXPECT_TRUE(ring.Push(MakeEvent(4242)));

  std::vector<RawEvent> one(1);
  ASSERT_EQ(1u, ring.Drain(one));
  ExpectIntact(one[0], 4242);
}

// The index mask must keep working as the sequence numbers run far past the
// array bounds.
TEST(PrivacyRingBufferTest, WrapsAroundTheIndexMaskRepeatedly) {
  PrivacyRingBuffer ring;
  uint32_t seq = 0;
  std::vector<RawEvent> out(64);
  for (int cycle = 0; cycle < 200; ++cycle) {
    for (int i = 0; i < 64; ++i) {
      ASSERT_TRUE(ring.Push(MakeEvent(seq + i)));
    }
    ASSERT_EQ(64u, ring.Drain(out));
    for (int i = 0; i < 64; ++i) {
      ExpectIntact(out[i], seq + i);
    }
    seq += 64;
  }
  EXPECT_EQ(0u, ring.dropped_count());
}

TEST(PrivacyRingBufferTest, PeakDepthTracksHighWaterMark) {
  PrivacyRingBuffer ring;
  for (uint32_t i = 0; i < 300; ++i) {
    ring.Push(MakeEvent(i));
  }
  std::vector<RawEvent> out(300);
  ring.Drain(out);
  for (uint32_t i = 0; i < 10; ++i) {
    ring.Push(MakeEvent(i));
  }
  // The mark is a high-water line, so draining does not lower it.
  EXPECT_EQ(300u, ring.peak_depth());
}

// The concurrency test that matters. One producer, one consumer, exactly as
// production uses it (network thread → privacy sequence). Asserts that no
// event is lost, duplicated, reordered, or torn, and that every event either
// arrives intact or is accounted for as a drop.
//
// On Windows this is a stress test: it catches ordering mistakes empirically
// but proves nothing about the memory model. TSAN, which does, is Linux-only
// in Chromium — see privacy_ring_buffer_tsan.cc for the standalone harness.
constexpr uint32_t kStressTotal = 200000;

TEST(PrivacyRingBufferTest, SingleProducerSingleConsumerStress) {
  PrivacyRingBuffer ring;
  std::atomic<uint32_t> produced{0};

  class Producer : public base::DelegateSimpleThread::Delegate {
   public:
    Producer(PrivacyRingBuffer* ring, std::atomic<uint32_t>* produced)
        : ring_(ring), produced_(produced) {}
    void Run() override {
      for (uint32_t i = 0; i < kStressTotal; ++i) {
        // Spin until accepted so the test asserts on a known total; production
        // drops instead, which FullBufferDropsInsteadOfOverwriting covers.
        while (!ring_->Push(MakeEvent(i))) {
        }
        produced_->fetch_add(1, std::memory_order_relaxed);
      }
    }

   private:
    raw_ptr<PrivacyRingBuffer> ring_;
    raw_ptr<std::atomic<uint32_t>> produced_;
  };

  Producer producer(&ring, &produced);
  base::DelegateSimpleThread thread(&producer, "ring-producer");
  thread.Start();

  uint32_t next_expected = 0;
  std::vector<RawEvent> out(128);
  while (next_expected < kStressTotal) {
    const size_t got = ring.Drain(out);
    for (size_t i = 0; i < got; ++i) {
      ASSERT_EQ(next_expected, out[i].site_id)
          << "event lost, duplicated or reordered at " << next_expected;
      ASSERT_EQ(~next_expected, out[i].domain_hash)
          << "torn write at " << next_expected;
      ++next_expected;
    }
  }
  thread.Join();

  EXPECT_EQ(kStressTotal, next_expected);
  EXPECT_EQ(kStressTotal, produced.load());
  EXPECT_EQ(0u, ring.ApproximateSize());
}

}  // namespace
}  // namespace zephyrus_privacy

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_event_sink.h"

#include <algorithm>
#include <limits>

#include "base/time/time.h"

namespace zephyrus_privacy {

PrivacyEventSink::PrivacyEventSink() = default;
PrivacyEventSink::~PrivacyEventSink() = default;

bool PrivacyEventSink::Record(const RawEvent& event) {
  if (disabled_.load(std::memory_order_relaxed)) {
    return false;
  }
  return ring_.Push(event);
}

void PrivacyEventSink::DisableForSession() {
  disabled_.store(true, std::memory_order_relaxed);
}

size_t PrivacyEventSink::Drain(base::span<RawEvent> out) {
  return ring_.Drain(out);
}


uint32_t PrivacyTicksDeltaMs() {
  static const base::TimeTicks epoch = base::TimeTicks::Now();
  const int64_t delta_ms = (base::TimeTicks::Now() - epoch).InMilliseconds();
  return static_cast<uint32_t>(
      std::clamp<int64_t>(delta_ms, 0, std::numeric_limits<uint32_t>::max()));
}

}  // namespace zephyrus_privacy

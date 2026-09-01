// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_EVENT_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_EVENT_H_

#include <stdint.h>

#include <type_traits>

namespace zephyrus_privacy {

// Spec §2.1. Exactly five values. There is no sixth, and in particular no
// CONFIRMED_DATA_TRANSFER: we can observe that a request completed, never what
// the server did with it, and the UI must never imply otherwise.
//
// Every user-facing string binds to one of these (§2.4, enforced in CI).
enum class TrackerStatus : uint8_t {
  // Request made or script seen; no action taken.
  kDetected = 0,
  // Request cancelled before leaving the device.
  kBlocked = 1,
  // Request completed; data left the device.
  kAllowed = 2,
  // An API returned perturbed data instead of true values.
  kRandomized = 3,
  // Heuristic match, not certain. Excluded from headline numbers.
  kPotential = 4,
  kMaxValue = kPotential,
};

// Spec §5. Timeline events are sparse — only notable things reach it.
enum class EventType : uint8_t {
  kFirstSeenOnSite = 0,
  kCrossSiteDetected = 1,
  kFingerprintAttempt = 2,
  // The highest-quality breakage signal available: a user allowing trackers on
  // a site almost always means a filter rule broke the page. Local only.
  kUserAllowedSite = 3,
  // Phase 2, §9.2: RTCPeerConnection construction / ICE gathering observed.
  kWebrtcAddressRequest = 4,
  kMaxValue = kWebrtcAddressRequest,
};

// Fingerprinting-relevant surfaces observed in the renderer (§6.5, §9.2.1).
//
// The browser-process mirror of zephyrus_privacy.mojom.FingerprintSurface. The
// two are translated at the mojo boundary rather than shared, so that
// privacy_core stays free of generated bindings and so that a value arriving
// from an untrusted renderer has to pass through an explicit switch before it
// becomes anything this code acts on. Keep them in sync; the translation is a
// switch with no default, so adding a value to the mojom fails the build here
// rather than silently mapping to the wrong surface.
enum class FingerprintSurface : uint8_t {
  kMediaDeviceEnumeration = 0,
  // Phase 4 (§6.5). The mask in PageSignals is uint32_t, so this enum may hold
  // at most 32 values; a static_assert on kMaxValue guards that below.
  kCanvasRead = 1,
  kCanvasExport = 2,
  kWebglRenderer = 3,
  kAudioBuffer = 4,
  kHardwareConcurrency = 5,
  kDeviceMemory = 6,
  kScreenDepth = 7,
  kMaxValue = kScreenDepth,
};

// PageSignals::fingerprint_surface_mask is a uint32_t bitmask indexed by this
// enum. Exceeding 32 values would shift past the end of it and silently stop
// counting surfaces, which would quietly disable the §6.5 attempt heuristic
// rather than fail.
static_assert(static_cast<int>(FingerprintSurface::kMaxValue) < 32,
              "fingerprint_surface_mask is uint32_t");

// §13.3. Why collection stopped itself, or kNone while it is running.
//
// A reason rather than a bool because the three triggers call for different
// responses and the user-visible consequence is identical: the history simply
// stops growing. §5.3 already insists a degraded state be VISIBLE rather than
// look like a clean web, and a switch that fired without saying which condition
// fired would be the same mistake one layer down.
enum class KillSwitchReason : uint8_t {
  kNone = 0,
  // Sustained ring overflow: the consumer cannot keep up with the network
  // thread, so events are being lost anyway. Continuing costs the hot path
  // real time to produce data that is already incomplete.
  kRingOverflow = 1,
  // Drain round trips are taking far longer than budget, which means the
  // privacy sequence is starved or blocked. Whatever it is competing with
  // matters more than this feature does.
  kFlushLatency = 2,
  // Repeated database write failures. Aggregation continues in memory
  // regardless (§5.3), but persisting is failing and retrying it forever helps
  // nobody.
  kDatabaseFailures = 3,
  kMaxValue = kDatabaseFailures,
};

// Tracker Radar categories, collapsed to the set we display. kUnknown is not a
// failure state — it is the honest answer for a domain no dataset covers, and
// such domains render as the bare domain with no invented owner (§4.1).
enum class Category : uint8_t {
  kUnknown = 0,
  kAdvertising = 1,
  kAnalytics = 2,
  kSocial = 3,
  kContent = 4,
  kCdn = 5,
  kFingerprinting = 6,
  kMaxValue = kFingerprinting,
};

// Sentinel for "no entity in the dataset". Distinct from entity 0.
inline constexpr uint16_t kNoEntity = 0xFFFF;

// Spec §8.5. Exactly 16 bytes, POD, no pointers — this is what the network
// thread writes, and the hot path budget (§8.1: <20µs p99, zero allocations)
// depends on it staying that way.
//
// `ticks_delta_ms` is a delta from a monotonic base (base::TimeTicks), never
// wall clock: §9.6. An NTP correction or a user changing the clock would
// otherwise produce negative or absurd deltas.
struct RawEvent {
  uint32_t site_id;
  uint32_t domain_hash;
  uint32_t ticks_delta_ms;
  uint16_t entity_id;
  uint8_t category;  // Category
  uint8_t status;    // TrackerStatus
};

// The size is a contract, not an implementation detail. 4096 of these are the
// fixed 64 KB ring buffer, and growing the struct silently grows that.
static_assert(sizeof(RawEvent) == 16, "RawEvent must stay 16 bytes (§8.5)");
static_assert(std::is_trivially_copyable_v<RawEvent>,
              "RawEvent is memcpy'd through a lock-free ring buffer");
static_assert(std::is_trivially_destructible_v<RawEvent>,
              "RawEvent must not require destruction on drop");

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_EVENT_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZEPHYRUS_FINGERPRINT_SEED_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZEPHYRUS_FINGERPRINT_SEED_H_

#include <array>
#include <cstdint>
#include <optional>

#include "third_party/blink/public/mojom/zephyrus/zephyrus_privacy_reporter.mojom-blink.h"
#include "third_party/blink/renderer/core/core_export.h"

namespace blink {

class ExecutionContext;

// Zephyrus §6.5: the fingerprint-randomization seed for `context`, or nullopt
// when randomization is off for it.
//
// One accessor for every instrumented surface. Canvas readback lives in
// modules/, image export in core/, and more surfaces are coming; each resolving
// the seed its own way is how two of them would end up disagreeing about
// whether a document is being perturbed — and a document perturbed on one
// surface but not another is measurably stranger than one perturbed on neither.
CORE_EXPORT std::optional<std::array<uint8_t, 32>> ZephyrusFingerprintSeedFor(
    ExecutionContext* context);

// COVERAGE, MEASURED — not inferred. Same 64x64 drawing hashed in four
// contexts on one origin, out/Release, both features on, all within a single
// browser session (the session secret is per launch, so hashes from two
// launches are not comparable and a harness that restarts the browser per
// context produces meaningless disagreement):
//
//   document          perturbed, and differs per origin
//   dedicated worker  perturbed, byte-identical to its own document
//   shared worker     UNPERTURBED — exactly the flag-off baseline
//   service worker    UNPERTURBED — exactly the flag-off baseline
//
// Service workers are now covered, by fetching their own seed through their own
// broker (see ZephyrusWorkerSeed in the .cc). Their origin is well defined and
// the seed is keyed on origin alone, so they land on the same seed their
// documents hold. Note they DO have a WebContentSettingsClient — a
// ServiceWorkerContentSettingsProxy — it simply carries no seed, so the
// accessors must check the self-fetch path first or that proxy answers nullopt
// and shadows it.
//
// SHARED WORKERS REMAIN AN OPEN EVASION, recorded rather than fixed: a script
// moves its canvas work into a SharedWorker and reads true pixels. They get a
// SharedWorkerContentSettingsProxy that lives entirely in //content and carries
// no seed, and //content exposes no embedder binder hook for them either, so
// unlike service workers there is no seam to reach them through without
// patching //content.
//
// Whatever the UI says must stay true while this holds: it may say specific
// surfaces were randomized, never that the device cannot be identified.

// A 64-bit value unique to (seed, surface). Every instrumented surface derives
// its own, so reading one teaches a page nothing about the others: a site that
// only touches audio must not thereby learn what our canvas answer will be.
//
// `tag` is a compile-time constant naming the surface, never page input.
CORE_EXPORT uint64_t ZephyrusSurfaceValue(const std::array<uint8_t, 32>& seed,
                                          uint64_t tag);

// §6.5 per-surface kill switches. Bits must match
// chrome/browser/zephyrus/privacy/privacy_features.h — they cross the mojom.
inline constexpr uint32_t kZephyrusFpCanvas = 1u << 0;
inline constexpr uint32_t kZephyrusFpAudio = 1u << 1;
inline constexpr uint32_t kZephyrusFpWebgl = 1u << 2;
inline constexpr uint32_t kZephyrusFpNavigator = 1u << 3;
inline constexpr uint32_t kZephyrusFpScreen = 1u << 4;
inline constexpr uint32_t kZephyrusFpFonts = 1u << 5;

// The seed for `context`, but only if `surface_bit` is currently enabled.
// One call so no site can check the seed and forget the mask.
CORE_EXPORT std::optional<std::array<uint8_t, 32>> ZephyrusSeedForSurface(
    ExecutionContext* context,
    uint32_t surface_bit);

// Surface tags. Kept together so two surfaces cannot silently share one.
inline constexpr uint64_t kZephyrusSurfaceAudio = 0x617564696f2d3031ULL;
inline constexpr uint64_t kZephyrusSurfaceWebglVendor = 0x77676c2d76656e64ULL;
inline constexpr uint64_t kZephyrusSurfaceWebglRenderer = 0x77676c2d72656e64ULL;
inline constexpr uint64_t kZephyrusSurfaceHardwareConcurrency =
    0x68772d636f6e6331ULL;
inline constexpr uint64_t kZephyrusSurfaceDeviceMemory = 0x6d656d2d64657631ULL;

// Reports that this document touched `surface` (§6.5 / §9.2.1).
//
// Deduplicated per document, which matters for cost as much as for accuracy:
// getImageData can be called in a loop, and an IPC per call would put privacy
// bookkeeping on a hot path. The browser dedupes again per site; both layers
// exist because the renderer is not trusted to be honest about repeats.
//
// Fire-and-forget. If the browser side is absent — feature off, or an
// off-the-record profile where the service is deliberately never created — the
// message is dropped, which is intended rather than an error.
CORE_EXPORT void ZephyrusReportFingerprintSurface(
    ExecutionContext* context,
    zephyrus_privacy::mojom::blink::FingerprintSurface surface);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_FRAME_ZEPHYRUS_FINGERPRINT_SEED_H_

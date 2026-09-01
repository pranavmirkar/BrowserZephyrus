// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/execution_context/navigator_base.h"
#include "third_party/blink/renderer/core/frame/zephyrus_fingerprint_seed.h"
#include "third_party/blink/renderer/core/frame/navigator_device_memory.h"
#include <optional>
#include <array>
#include <algorithm>

#include "base/feature_list.h"
#include "build/build_config.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/frame/navigator_concurrent_hardware.h"
#include "third_party/blink/renderer/core/probe/core_probes.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"
#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

#if !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_WIN)
#include <sys/utsname.h>
#include "third_party/blink/renderer/platform/wtf/thread_specific.h"
#include "third_party/blink/renderer/platform/wtf/threading.h"
#endif

namespace blink {

namespace {

String GetReducedNavigatorPlatform() {
#if BUILDFLAG(IS_ANDROID)
  return "Linux armv81";
#elif BUILDFLAG(IS_MAC)
  return "MacIntel";
#elif BUILDFLAG(IS_WIN)
  return "Win32";
#elif BUILDFLAG(IS_FUCHSIA)
  return "";
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  return "Linux x86_64";
#elif BUILDFLAG(IS_IOS)
  return "iPhone";
#else
#error Unsupported platform
#endif
}

}  // namespace

NavigatorBase::NavigatorBase(ExecutionContext* context)
    : NavigatorLanguage(context), ExecutionContextClient(context) {}

String NavigatorBase::userAgent() const {
  ExecutionContext* execution_context = GetExecutionContext();
  return execution_context ? execution_context->UserAgent() : String();
}

String NavigatorBase::platform() const {
#if BUILDFLAG(IS_ANDROID)
  // We need to check the ReduceUserAgentMinorVersion feature flag for
  // Android WebView, which does not currently ship a reduced User-Agent.
  if (!RuntimeEnabledFeatures::ReduceUserAgentMinorVersionEnabled()) {
    return NavigatorID::platform();
  }
#endif
  return GetReducedNavigatorPlatform();
}

void NavigatorBase::Trace(Visitor* visitor) const {
  ScriptWrappable::Trace(visitor);
  NavigatorLanguage::Trace(visitor);
  ExecutionContextClient::Trace(visitor);
  Supplementable<NavigatorBase>::Trace(visitor);
}

unsigned int NavigatorBase::hardwareConcurrency() const {
  unsigned int hardware_concurrency =
      NavigatorConcurrentHardware::hardwareConcurrency();

  probe::ApplyHardwareConcurrencyOverride(
      probe::ToCoreProbeSink(GetExecutionContext()), hardware_concurrency);

  // Zephyrus §6.5. Reported DOWNWARD only, never up.
  //
  // This is the one perturbation here with a functional consequence: sites size
  // their worker pools from this number, so over-reporting would make a page
  // spawn threads the machine does not have — turning a privacy feature into a
  // performance bug. Under-reporting costs at most some parallelism, and a
  // floor of 2 keeps code that branches on ">1" on its normal path.
  ZephyrusReportFingerprintSurface(GetExecutionContext(),
                                   zephyrus_privacy::mojom::blink::FingerprintSurface::kHardwareConcurrency);
  if (std::optional<std::array<uint8_t, 32>> seed =
          ZephyrusSeedForSurface(GetExecutionContext(), kZephyrusFpNavigator)) {
    // Candidates are the values real machines report, so the answer stays
    // inside the population rather than standing out as an odd number.
    static constexpr auto kPlausible =
        std::to_array<unsigned>({2, 4, 6, 8, 12, 16});
    // Only the NEAREST few rungs at or below the truth.
    //
    // Picking freely from everything below it is what the first version did,
    // and on a 16-core machine it happily answered 2. That is still "downward
    // only", but §6.5 asks for perturbation below the threshold of FUNCTIONAL
    // significance, and an eightfold cut in a site's worker pool is well above
    // it. Staying within a few rungs keeps the answer both plausible and
    // harmless while still splitting the population.
    static constexpr size_t kSpread = 3;
    std::array<unsigned, kSpread> candidates = {};
    size_t n = 0;
    for (size_t i = kPlausible.size(); i-- > 0 && n < kSpread;) {
      if (kPlausible[i] <= hardware_concurrency) {
        candidates[n++] = kPlausible[i];
      }
    }
    if (n == 0) {
      return hardware_concurrency;  // Below the lowest rung: leave it alone.
    }
    const uint64_t v =
        ZephyrusSurfaceValue(*seed, kZephyrusSurfaceHardwareConcurrency);
    return candidates[v % n];
  }
  return hardware_concurrency;
}

float NavigatorBase::deviceMemory() const {
  const float real = NavigatorDeviceMemory::deviceMemory();

  // §6.5, same downward-only rule. The web-exposed value is already quantised
  // to this ladder, so staying on it means the perturbed answer is
  // indistinguishable from a real machine's.
  ZephyrusReportFingerprintSurface(GetExecutionContext(),
                                   zephyrus_privacy::mojom::blink::FingerprintSurface::kDeviceMemory);
  if (std::optional<std::array<uint8_t, 32>> seed =
          ZephyrusSeedForSurface(GetExecutionContext(), kZephyrusFpNavigator)) {
    static constexpr auto kLadder =
        std::to_array<float>({0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f});
    // Same nearest-rungs rule, and for a sharper reason: the first version
    // could answer 0.25 on a 16 GB machine, and sites use this value to decide
    // whether to enable features at all. A privacy tweak that silently puts the
    // user in the low-memory tier of every site is breakage, not protection.
    static constexpr size_t kSpread = 3;
    std::array<float, kSpread> candidates = {};
    size_t n = 0;
    for (size_t i = kLadder.size(); i-- > 0 && n < kSpread;) {
      if (kLadder[i] <= real) {
        candidates[n++] = kLadder[i];
      }
    }
    if (n > 0) {
      const uint64_t v =
          ZephyrusSurfaceValue(*seed, kZephyrusSurfaceDeviceMemory);
      return candidates[v % n];
    }
  }
  return real;
}

ExecutionContext* NavigatorBase::GetUAExecutionContext() const {
  return GetExecutionContext();
}

UserAgentMetadata NavigatorBase::GetUserAgentMetadata() const {
  ExecutionContext* execution_context = GetExecutionContext();
  return execution_context ? execution_context->GetUserAgentMetadata()
                           : blink::UserAgentMetadata();
}

}  // namespace blink

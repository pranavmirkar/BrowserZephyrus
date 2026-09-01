// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/domain_reliability/service_factory.h"

namespace domain_reliability {

const char kUploadReporterString[] = "chrome";

// Zephyrus: never create the service.
//
// Domain Reliability watches for failed connections to Google-owned hosts and
// uploads beacons describing them to beacons*.gvt2.com. It is telemetry about
// Google's own service quality; there is no user-facing feature to lose.
//
// The whole decision is replaced rather than adding an early return, for two
// reasons. First, -Wunreachable-code-aggressive is on, so a return above live
// code does not compile. Second, and more important, the conditions removed
// here were the ONLY thing keeping this off: upstream's default is enabled,
// and the two checks that made it inert in practice -- the policy pref and
// IsMetricsAndCrashReportingEnabled() -- are runtime state. Anything that
// flips metrics consent on restores the uploads silently. Returning false
// unconditionally does not depend on a pref nobody is watching, and it also
// means --enable-domain-reliability can no longer turn it back on.
bool ShouldCreateService(const DomainReliabilityServiceDelegate* delegate) {
  return false;
}

}  // namespace domain_reliability

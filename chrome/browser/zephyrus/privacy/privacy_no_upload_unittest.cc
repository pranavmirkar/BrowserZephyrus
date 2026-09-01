// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §13.4, decided 2026-08-20: **Zephyrus captures crash dumps LOCALLY and
// uploads nothing. Not metrics, not crashes, not UKM.**
//
// §7 already forbids sending privacy events anywhere, and nothing in the
// feature emits a histogram of its own — there is not one UmaHistogram call in
// chrome/browser/zephyrus/privacy. But the privacy database opens with
// `sql::Database::Tag("ZephyrusPrivacy")`, and that tag makes //sql emit
// histograms for database size and error codes. They carry no domains, sites or
// entities, yet on a build that uploaded UMA they would leave the device.
//
// Today they cannot, and that is the problem this file exists to fix: it is
// true by ACCIDENT OF BUILD FLAGS rather than by decision. §13.4 says exactly
// that is not good enough --
//
//   "if the build uploads UMA they leave the device, which needs to be a
//    deliberate decision rather than an inherited default"
//
// Two independent things currently make upload impossible:
//
//   1. is_chrome_branded = false, so CrashReporterClient::GetUploadUrl()
//      returns "" (it is non-empty only under GOOGLE_CHROME_BRANDING AND
//      OFFICIAL_BUILD). Crashpad still writes dumps to disk, which is what
//      Zephyrus uses for DCHECK triage, and they stay there.
//   2. enable_src_internal = false, so components/metrics/server_urls.grd
//      carries "-" for every endpoint, which the loader maps to an empty GURL.
//      Upstream keeps the real URLs internal precisely "to prevent Chromium
//      forks from accidentally sending metrics to Google servers".
//
// Either could be flipped by an unrelated build-config change, by someone
// chasing branded PGO or an official-build feature, with no reason to think
// they were touching privacy. This file makes that flip fail loudly here
// instead of silently starting an upload.
//
// IF YOU ARE HERE BECAUSE THIS TEST FAILED: do not delete it. Decide, and write
// the decision down. If Zephyrus is genuinely gaining upload, the §13.4
// requirement is that the privacy feature be EXCLUDED from it — the sql tag
// above is the thing to exclude — and this test should then be rewritten to
// assert that exclusion rather than removed.

#include "build/branding_buildflags.h"
#include "build/buildflag.h"
#include "components/autofill/core/common/autofill_debug_features.h"
#include "components/metrics/server_urls.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_privacy {

// Compile-time half. Cheapest possible guard: flipping is_chrome_branded stops
// the build rather than quietly enabling a crash upload URL.
static_assert(!BUILDFLAG(GOOGLE_CHROME_BRANDING),
              "Zephyrus builds unbranded ON PURPOSE (§13.4). Google Chrome "
              "branding turns on the crash upload endpoint, which would start "
              "sending the ZephyrusPrivacy sql histograms off the device. If "
              "branding is genuinely wanted, exclude the privacy feature from "
              "reporting first and update privacy_no_upload_unittest.cc.");

// Runtime half. The metrics endpoints are data, not buildflags, so a static
// assert cannot see them: they arrive through GRIT and would become real the
// moment an internal src checkout is used.
TEST(PrivacyNoUploadTest, MetricsEndpointsAreEmpty) {
  EXPECT_TRUE(metrics::GetMetricsServerUrl().is_empty())
      << "§13.4: this build has a real UMA endpoint, so the ZephyrusPrivacy "
         "sql histograms (database size, error codes) would now leave the "
         "device. That may be fine, but it must be a decision — see the file "
         "comment.";
  EXPECT_TRUE(metrics::GetInsecureMetricsServerUrl().is_empty());
  EXPECT_TRUE(metrics::GetUkmServerUrl().is_empty())
      << "UKM is per-URL by construction and is the last thing this feature "
         "should ever feed";
}

// Autofill crowdsourcing, found by the 2026-08-24 netlog audit and verified in
// code: upstream ships kAutofillServerCommunication ENABLED, ungated by
// sign-in, consent or a valid API key, POSTing per-site form signatures to
// content-autofill.googleapis.com.
//
// This lives beside the metrics guard because it is the same failure: an
// upstream default that quietly sends something off the device, in a browser
// that promises it does not. It is a separate test so the reason is legible in
// the failure line rather than buried in a list.
TEST(PrivacyNoUploadTest, AutofillCrowdsourcingIsDisabled) {
  EXPECT_FALSE(base::FeatureList::IsEnabled(
      autofill::features::debug::kAutofillServerCommunication))
      << "Autofill would upload this profile's form signatures to "
         "content-autofill.googleapis.com on ordinary navigation, with no "
         "sign-in and no consent. Note an empty API key does NOT prevent this: "
         "it makes Google reject the reply, long after the request has already "
         "revealed which site the user is on.";
}

}  // namespace zephyrus_privacy

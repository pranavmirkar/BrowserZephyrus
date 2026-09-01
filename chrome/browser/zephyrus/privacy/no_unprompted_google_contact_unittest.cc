// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Pins the "no unprompted Google contact" defaults.
//
// An external audit (2026-08-24) listed several subsystems as live phone-home
// surfaces. Verification found most of them already inert -- but inert for
// reasons that live in a DIFFERENT file from the flag that names them, which is
// exactly the configuration that rots silently. Two were genuinely live and are
// fixed by the changes these tests guard.
//
// The distinction that organises this file, learned the expensive way from the
// autofill leak: an unconfigured API key only protects you when it is checked
// BEFORE the request is built. Where the key is merely appended to a URL, the
// request still goes out and the empty key just makes it fail server-side --
// after the connection, the SNI and the locale have already been sent. So each
// test below asserts the thing that actually stops the packet, not the flag
// that sounds like it should.
//
// These are DEFAULTS tests: no ScopedFeatureList anywhere. A test that enables
// a flag and then reads the flag back proves nothing about what a user gets.

#include "base/feature_list.h"
#include "chrome/browser/domain_reliability/service_factory.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/translate/core/browser/translate_ranker_impl.h"
#include "components/translate/core/common/translate_features.h"
#include "components/variations/net/variations_http_headers.h"
#include "components/variations/scoped_variations_ids_provider.h"
#include "services/network/public/cpp/resource_request.h"
#include "google_apis/google_api_keys.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

// --- Translate: the two contacts, only one of which the user asked for. ---

// GetSupportedLanguages() fetches the roster as a side effect of being asked
// which languages exist -- language-settings reads and pref changes, no intent
// to translate. The API key is appended to that URL rather than checked, so an
// empty key does not stop it.
TEST(NoUnpromptedGoogleContactTest, TranslateLanguageListFetchIsOff) {
  EXPECT_FALSE(base::FeatureList::IsEnabled(
      translate::kTranslateLanguageListFetch))
      << "translate.googleapis.com/translate_a/l would be fetched without the "
         "user asking to translate anything; the compiled-in "
         "kDefaultSupportedLanguages is the intended source";
}

// The ranker builds a model loader in its constructor and downloads from
// gstatic.com on any profile without a cached model.
TEST(NoUnpromptedGoogleContactTest, TranslateRankerIsOff) {
  EXPECT_FALSE(base::FeatureList::IsEnabled(translate::kTranslateRankerQuery));
  EXPECT_FALSE(
      base::FeatureList::IsEnabled(translate::kTranslateRankerEnforcement));
}

// The behavioural assertion. The test above reads the two inputs; this one
// reads the OUTPUT, and that distinction is the point: either flag alone is
// enough to construct the loader, so the condition that matters is a composition
// of the two, not each in isolation. Asserting the composed result stays correct
// if upstream adds a third flag, reorders the OR, or moves the decision
// elsewhere -- at which point the input tests would still pass and this one
// would not. Translation still works; the prompt is simply no longer
// ML-suppressed.
TEST(NoUnpromptedGoogleContactTest, TranslateRankerHasNoModelUrl) {
  EXPECT_TRUE(translate::TranslateRankerImpl::GetModelURL().is_empty())
      << "a non-empty ranker model URL means the gstatic.com download is live "
         "again; check whether only one of the two ranker flags got flipped";
}

// --- The API key, which is what actually makes several subsystems inert. ---

// This single fact is load-bearing for more than it looks. The optimization
// guide checks it inside IsUserPermittedToFetchFromRemoteOptimizationGuide()
// before fetching hints, and PredictionModelDownloadManager::ShouldFetchModels()
// checks it before downloading models -- which is also what makes the
// segmentation platform inert, since that is where its models come from.
//
// All of that is a PRECONDITION check, so it genuinely prevents the request.
// But it lives in google_apis, three components away from any of the flags an
// auditor would read, and a build-config change would silently reactivate every
// one of those fetches at once. Hence this test, here, next to the others.
TEST(NoUnpromptedGoogleContactTest, NoGoogleApiKeyIsConfigured) {
  EXPECT_FALSE(google_apis::HasAPIKeyConfigured())
      << "a configured API key re-enables optimization-guide hint fetching, "
         "optimization-guide model downloads, and with them the segmentation "
         "platform's model source -- none of which have a flag of their own "
         "that would show the change";
}

// The optimization guide's own flags are deliberately left at their upstream
// ENABLED defaults, because the gate above is what stops the traffic and
// diverging from upstream here would cost a rebase conflict for no behaviour
// change. Asserted rather than assumed, so that if someone later "fixes" this
// by flipping the flags, the reason the flags do not matter is written down.
TEST(NoUnpromptedGoogleContactTest, OptimizationHintsFlagIsUpstreamDefault) {
  EXPECT_TRUE(
      base::FeatureList::IsEnabled(optimization_guide::features::kOptimizationHints))
      << "this flag being on is fine and expected; NoGoogleApiKeyIsConfigured "
         "is the test that keeps the fetches from happening";
}

// --- Local-network broadcast, rather than a Google contact. ---

// Cast/DIAL discovery sprays _googlecast._tcp mDNS and SSDP M-SEARCH onto the
// LAN, which fingerprints the browser to every device on the network.
//
// On Windows -- the only platform we ship -- cast_media_sink_service.cc
// hardcodes the delay with an #if, so this feature is not even consulted and
// this test cannot fail for the reason it exists. That is precisely why it is
// worth keeping: it is the defence that survives upstream deleting the #if,
// and the DIAL service consults the feature on every platform including ours.
TEST(NoUnpromptedGoogleContactTest, MediaSinkDiscoveryIsDelayedUntilUserAction) {
  EXPECT_TRUE(
      base::FeatureList::IsEnabled(media_router::kDelayMediaSinkDiscovery))
      << "without this, Cast and DIAL discovery broadcast on the local network "
         "at startup instead of waiting for the user to open a Cast surface";
}

// --- Telemetry to Google with no user-facing feature attached. ---

// A delegate that says yes to everything. The point is to prove the decision no
// longer depends on it: upstream, Domain Reliability was held off only by the
// policy pref and the metrics-consent check, both of which are runtime state
// that something else can flip. If this test passes with a permissive delegate,
// the beacons*.gvt2.com uploads cannot come back by way of a pref change.
class PermissiveDomainReliabilityDelegate
    : public domain_reliability::DomainReliabilityServiceDelegate {
 public:
  bool IsDomainReliabilityAllowed() const override { return true; }
  bool IsMetricsAndCrashReportingEnabled() const override { return true; }
};

TEST(NoUnpromptedGoogleContactTest, DomainReliabilityServiceIsNeverCreated) {
  PermissiveDomainReliabilityDelegate permissive;
  EXPECT_FALSE(domain_reliability::ShouldCreateService(&permissive))
      << "Domain Reliability would upload connection-failure beacons about "
         "Google-owned hosts to beacons*.gvt2.com; it has no user-facing "
         "feature, so there is nothing this should ever be traded against";
}

// --- A quiet identifier on every Google request. ---

// Asserts the OUTPUT again rather than the producer: what matters is that no
// header lands on the request. Uses a plain google.com URL in a non-incognito,
// signed-out context -- the case most favourable to the header being attached,
// so a pass here is not an artefact of picking a URL that was exempt anyway.
TEST(NoUnpromptedGoogleContactTest, NoXClientDataHeaderOnGoogleRequests) {
  // The provider must exist before the header code runs; this scoped helper is
  // the supported way to stand one up and tear it down again.
  variations::test::ScopedVariationsIdsProvider scoped_provider(
      variations::VariationsIdsProvider::Mode::kUseSignedInState);

  network::ResourceRequest request;
  request.url = GURL("https://www.google.com/search?q=test");

  // Guard against a vacuous pass. If this URL were rejected earlier -- not a
  // Google property, wrong scheme, incognito -- the header would be absent for
  // a reason that has nothing to do with our change, and the assertions below
  // would still pass while silently testing nothing. ASSERT, so the test stops
  // here rather than reporting a green result it did not earn.
  ASSERT_TRUE(variations::ShouldAppendVariationsHeaderForTesting(
      request.url, variations::InIncognito::kNo))
      << "this URL never reaches the header code, so the rest of this test "
         "would prove nothing about whether X-Client-Data was removed";

  const bool appended = variations::AppendVariationsHeaderUnknownSignedIn(
      request.url, variations::InIncognito::kNo, &request);

  EXPECT_FALSE(appended);
  EXPECT_FALSE(variations::HasVariationsHeader(request))
      << "X-Client-Data names this browser's field-trial groups on every "
         "request to a Google property; the combination is close to unique";
}

}  // namespace
}  // namespace zephyrus_privacy

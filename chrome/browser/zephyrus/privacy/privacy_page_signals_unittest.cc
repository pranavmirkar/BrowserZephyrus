// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 2, §9.2.1 and §6.5: the page signals that arrive from outside the ring
// buffer — a renderer reporting a fingerprinting surface, and the browser
// noticing an RTCPeerConnection.
//
// These are the paths where a compromised renderer touches the user's privacy
// record, so what is tested here is mostly not "the count went up" but the
// properties that bound the damage: attribution comes from the frame and not
// the message, repeats are dropped, the set of tracked sites is capped, and the
// reassuring "local addresses were withheld" string cannot be reached by a
// request that was not in fact protected.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "chrome/browser/zephyrus/privacy/privacy_scores.h"
#include "chrome/browser/zephyrus/privacy/privacy_tab_helper.h"
#include "chrome/browser/zephyrus/privacy/privacy_crypto_impl.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/test/base/testing_profile.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

using PageSignals = PrivacyIntelligenceService::PageSignals;

class PrivacyPageSignalsTest : public testing::Test {
 public:
  PrivacyPageSignalsTest() {
    features_.InitAndEnableFeature(kZephyrusPrivacyIntelligence);
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    PrivacyCryptoImpl::RegisterProfilePrefs(prefs_.registry());
    // Null os_crypt: the service runs memory-only, which is a supported steady
    // state (§5.3) and exactly what these tests want — no database, no disk,
    // and the in-memory signal map is the whole subject.
    service_ = std::make_unique<PrivacyIntelligenceService>(
        temp_dir_.GetPath(), &prefs_, /*os_crypt=*/nullptr);
  }

  void TearDown() override {
    service_->Shutdown();
    service_.reset();
  }

 protected:
  content::BrowserTaskEnvironment task_environment_;
  base::test::ScopedFeatureList features_;
  base::ScopedTempDir temp_dir_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  std::unique_ptr<PrivacyIntelligenceService> service_;
};

// -- SiteKeyFor: what may and may not be attributed -------------------------

TEST_F(PrivacyPageSignalsTest, SiteKeyFoldsToRegistrableDomain) {
  EXPECT_EQ("example.com", PrivacyIntelligenceService::SiteKeyFor(
                               GURL("https://news.sub.example.com/a?b=c")));
  EXPECT_EQ("example.co.uk", PrivacyIntelligenceService::SiteKeyFor(
                                 GURL("http://www.example.co.uk/")));
}

// §9.7: a host with no registrable domain has no site to attribute to, and
// inventing one from its last two labels is the fabrication §4.2 forbids.
TEST_F(PrivacyPageSignalsTest, SiteKeyRefusesHostsThatCannotHaveAnOwner) {
  const char* const kNoSite[] = {
      "http://127.0.0.1/",       "https://192.168.1.1/",
      "http://[::1]/",           "http://localhost/",
      "http://intranet/",        "https://server/page",
  };
  for (const char* url : kNoSite) {
    EXPECT_EQ("", PrivacyIntelligenceService::SiteKeyFor(GURL(url)))
        << "attributed something to " << url;
  }
}

// Non-web schemes carry no site the popup could describe, and a WebUI or
// extension page must never appear in the user's tracking record.
TEST_F(PrivacyPageSignalsTest, SiteKeyRefusesNonWebSchemes) {
  const char* const kNoSite[] = {
      "chrome://settings",  "about:blank",
      "data:text/html,hi",  "file:///C:/tmp/a.html",
      "",                   "not a url",
  };
  for (const char* url : kNoSite) {
    EXPECT_EQ("", PrivacyIntelligenceService::SiteKeyFor(GURL(url)))
        << "attributed something to '" << url << "'";
  }
}

// -- Fingerprint surfaces ---------------------------------------------------

TEST_F(PrivacyPageSignalsTest, UnknownSiteHasNothingRecorded) {
  const PageSignals signals =
      service_->GetPageSignals(GURL("https://example.com/"));
  EXPECT_EQ(0u, signals.fingerprint_surface_count());
  EXPECT_EQ(0u, signals.webrtc_address_requests);
}

TEST_F(PrivacyPageSignalsTest, FingerprintSurfaceIsRecordedOncePerSite) {
  const GURL page("https://example.com/");
  service_->RecordFingerprintSurface(
      page, FingerprintSurface::kMediaDeviceEnumeration,
      /*randomized=*/false);
  EXPECT_EQ(1u, service_->GetPageSignals(page).fingerprint_surface_count());

  // The bound on a hostile renderer: a page calling the API in a loop cannot
  // inflate the user's numbers, because distinct surfaces are what is counted.
  for (int i = 0; i < 10000; ++i) {
    service_->RecordFingerprintSurface(
        page, FingerprintSurface::kMediaDeviceEnumeration,
        /*randomized=*/false);
  }
  EXPECT_EQ(1u, service_->GetPageSignals(page).fingerprint_surface_count());
}

TEST_F(PrivacyPageSignalsTest, SubdomainsShareTheSiteRecord) {
  service_->RecordFingerprintSurface(
      GURL("https://a.example.com/"),
      FingerprintSurface::kMediaDeviceEnumeration, /*randomized=*/false);
  // The popup describes a SITE, so a report from one subdomain has to be
  // visible when the user is looking at another.
  EXPECT_EQ(1u, service_->GetPageSignals(GURL("https://b.example.com/"))
                    .fingerprint_surface_count());
}

TEST_F(PrivacyPageSignalsTest, SitesDoNotBleedIntoEachOther) {
  service_->RecordFingerprintSurface(
      GURL("https://tracker.example/"),
      FingerprintSurface::kMediaDeviceEnumeration, /*randomized=*/false);
  EXPECT_EQ(0u, service_->GetPageSignals(GURL("https://innocent.example/"))
                    .fingerprint_surface_count());
}

TEST_F(PrivacyPageSignalsTest, HostsWithNoOwnerRecordNothing) {
  const GURL page("http://127.0.0.1/");
  service_->RecordFingerprintSurface(
      page, FingerprintSurface::kMediaDeviceEnumeration,
      /*randomized=*/false);
  EXPECT_EQ(0u, service_->GetPageSignals(page).fingerprint_surface_count());
}

// A surface is reported whether or not it was perturbed, so the two masks must
// not track each other. If they ever do, "Device fingerprint" appears under
// "What was protected" for surfaces that were only DETECTED — the §2 false
// claim that IDS_ZEPHYRUS_PRIVACY_PROTECTED_FINGERPRINT's own description
// forbids, and the single most likely way to reintroduce it.
TEST_F(PrivacyPageSignalsTest, DetectedSurfacesDoNotCountAsProtected) {
  const GURL page("https://example.com/");
  service_->RecordFingerprintSurface(page, FingerprintSurface::kCanvasRead,
                                     /*randomized=*/false);
  service_->RecordFingerprintSurface(page, FingerprintSurface::kAudioBuffer,
                                     /*randomized=*/false);

  const auto signals = service_->GetPageSignals(page);
  EXPECT_EQ(2u, signals.fingerprint_surface_count());
  EXPECT_EQ(0u, signals.fingerprint_randomized_count())
      << "nothing was perturbed, so nothing may be claimed as protected";
}

TEST_F(PrivacyPageSignalsTest, OnlyPerturbedSurfacesCountAsProtected) {
  const GURL page("https://example.com/");
  service_->RecordFingerprintSurface(page, FingerprintSurface::kCanvasRead,
                                     /*randomized=*/true);
  service_->RecordFingerprintSurface(page, FingerprintSurface::kAudioBuffer,
                                     /*randomized=*/false);

  const auto signals = service_->GetPageSignals(page);
  EXPECT_EQ(2u, signals.fingerprint_surface_count());
  EXPECT_EQ(1u, signals.fingerprint_randomized_count())
      << "the randomized count is a strict subset of the touched count";
}

// The same surface can be touched before a seed is available and again after.
// The honest answer to "was this perturbed on this site" is yes if it ever was,
// so the repeat must still be able to set the bit even though the repeat is
// otherwise dropped.
TEST_F(PrivacyPageSignalsTest, LaterPerturbationOfASeenSurfaceStillCounts) {
  const GURL page("https://example.com/");
  service_->RecordFingerprintSurface(page, FingerprintSurface::kCanvasRead,
                                     /*randomized=*/false);
  ASSERT_EQ(0u, service_->GetPageSignals(page).fingerprint_randomized_count());

  service_->RecordFingerprintSurface(page, FingerprintSurface::kCanvasRead,
                                     /*randomized=*/true);

  const auto signals = service_->GetPageSignals(page);
  EXPECT_EQ(1u, signals.fingerprint_surface_count())
      << "still one distinct surface; the repeat must not inflate the count";
  EXPECT_EQ(1u, signals.fingerprint_randomized_count());
}

// -- WebRTC -----------------------------------------------------------------

TEST_F(PrivacyPageSignalsTest, WebrtcRequestsAccumulate) {
  const GURL page("https://meet.example/");
  service_->RecordWebrtcAddressRequest(page, /*local_addresses_withheld=*/true);
  service_->RecordWebrtcAddressRequest(page, /*local_addresses_withheld=*/true);
  const PageSignals signals = service_->GetPageSignals(page);
  EXPECT_EQ(2u, signals.webrtc_address_requests);
  EXPECT_TRUE(signals.local_addresses_withheld);
}

// The claim "local addresses were withheld" describes every request on the
// page. One request under a laxer policy — an enterprise override, an extension
// changing the pref mid-session — must falsify it, or the popup reassures the
// user on the strength of a different connection that happened to be protected.
TEST_F(PrivacyPageSignalsTest, OneUnprotectedRequestFalsifiesTheClaim) {
  const GURL page("https://meet.example/");
  service_->RecordWebrtcAddressRequest(page, /*local_addresses_withheld=*/true);
  service_->RecordWebrtcAddressRequest(page,
                                       /*local_addresses_withheld=*/false);
  service_->RecordWebrtcAddressRequest(page, /*local_addresses_withheld=*/true);

  const PageSignals signals = service_->GetPageSignals(page);
  EXPECT_EQ(3u, signals.webrtc_address_requests);
  EXPECT_FALSE(signals.local_addresses_withheld)
      << "a page where one request leaked local addresses must not report "
         "them as withheld";
}

// -- Bounds and clearing ----------------------------------------------------

// The key derives from page content, so a long session must not grow the map
// without bound. Past the cap new sites are not recorded — which shows nothing,
// honestly — rather than evicting, which would make an already-recorded site
// silently revert to "nothing observed".
TEST_F(PrivacyPageSignalsTest, TrackedSitesAreCapped) {
  for (int i = 0; i < 2000; ++i) {
    service_->RecordFingerprintSurface(GURL(base::StringPrintf("https://site%d.example/", i)),
        FingerprintSurface::kMediaDeviceEnumeration,
                                     /*randomized=*/false);
  }
  // An early site is inside the cap and still recorded.
  EXPECT_EQ(1u, service_->GetPageSignals(GURL("https://site0.example/"))
                    .fingerprint_surface_count());
  // A late one was refused rather than displacing it.
  EXPECT_EQ(0u, service_->GetPageSignals(GURL("https://site1999.example/"))
                    .fingerprint_surface_count());
}

TEST_F(PrivacyPageSignalsTest, ClearingEverythingWipesPageSignals) {
  const GURL page("https://example.com/");
  service_->RecordFingerprintSurface(
      page, FingerprintSurface::kMediaDeviceEnumeration,
      /*randomized=*/false);
  service_->RecordWebrtcAddressRequest(page, true);
  ASSERT_EQ(1u, service_->GetPageSignals(page).fingerprint_surface_count());

  service_->DeleteAllBrowsingData();

  const PageSignals signals = service_->GetPageSignals(page);
  EXPECT_EQ(0u, signals.fingerprint_surface_count())
      << "clearing history must not leave this session's signals in the popup";
  EXPECT_EQ(0u, signals.webrtc_address_requests);
}

// A ranged clear covering the session has to take them too: these carry no
// timestamp, so there is no honest way to subtract a window from them.
TEST_F(PrivacyPageSignalsTest, RangedClearOverSessionWipesPageSignals) {
  const GURL page("https://example.com/");
  service_->RecordWebrtcAddressRequest(page, true);
  ASSERT_EQ(1u, service_->GetPageSignals(page).webrtc_address_requests);

  service_->DeleteRange(base::Time::Now() - base::Hours(1),
                        base::Time::Max());

  EXPECT_EQ(0u, service_->GetPageSignals(page).webrtc_address_requests);
}

// -- §6.2 current site analysis ---------------------------------------------

using DomainRow = PrivacyTabHelper::DomainRow;
using PageTotals = PrivacyTabHelper::PageTotals;
using PageAnalysis = PrivacyIntelligenceService::PageAnalysis;

DomainRow Row(std::string domain,
              uint32_t detected,
              uint32_t blocked,
              uint32_t allowed) {
  DomainRow row;
  row.domain = std::move(domain);
  row.detected = detected;
  row.blocked = blocked;
  row.allowed = allowed;
  return row;
}

TEST_F(PrivacyPageSignalsTest, AnalysisOrdersBlockedFirstThenByRequestCount) {
  std::vector<DomainRow> rows = {
      Row("small-blocked.example", 0, 2, 0),
      Row("allowed.example", 0, 0, 50),
      Row("big-blocked.example", 0, 30, 0),
  };
  PageTotals totals;
  totals.blocked = 32;
  totals.allowed = 50;
  totals.distinct_domains = 3;

  base::test::TestFuture<PageAnalysis> future;
  service_->AnalyzeCurrentPage(GURL("https://news.example/"), std::move(rows),
                               totals, future.GetCallback());
  const PageAnalysis analysis = future.Take();

  ASSERT_EQ(3u, analysis.rows.size());
  EXPECT_EQ("big-blocked.example", analysis.rows[0].domain);
  EXPECT_EQ("small-blocked.example", analysis.rows[1].domain)
      << "blocked rows come before allowed ones regardless of request count";
  EXPECT_EQ("allowed.example", analysis.rows[2].domain);
}

// The per-row form of the §2 accuracy rule. A domain where six requests were
// blocked and two were not is not a blocked domain, and a green "Blocked" on
// that row tells the user something untrue about their own page.
TEST_F(PrivacyPageSignalsTest, MixedRowNeverClaimsBlocked) {
  std::vector<DomainRow> rows = {
      Row("mixed.example", /*detected=*/2, /*blocked=*/6, /*allowed=*/0),
      Row("clean-block.example", 0, 6, 0),
  };
  PageTotals totals;
  totals.detected = 2;
  totals.blocked = 12;
  totals.distinct_domains = 2;

  base::test::TestFuture<PageAnalysis> future;
  service_->AnalyzeCurrentPage(GURL("https://news.example/"), std::move(rows),
                               totals, future.GetCallback());
  const PageAnalysis analysis = future.Take();

  ASSERT_EQ(2u, analysis.rows.size());
  const auto mixed = std::ranges::find(analysis.rows, "mixed.example",
                                       &PrivacyIntelligenceService::
                                           TrackerRow::domain);
  ASSERT_NE(analysis.rows.end(), mixed);
  EXPECT_NE(TrackerStatus::kBlocked, mixed->status)
      << "two of eight requests got through";
  EXPECT_EQ(8u, mixed->requests);

  const auto clean = std::ranges::find(analysis.rows, "clean-block.example",
                                       &PrivacyIntelligenceService::
                                           TrackerRow::domain);
  ASSERT_NE(analysis.rows.end(), clean);
  EXPECT_EQ(TrackerStatus::kBlocked, clean->status);
}

// §4.2: no dataset is installed in these tests, so every domain is unknown and
// must render as itself. An invented owner or category is the failure mode.
TEST_F(PrivacyPageSignalsTest, UnknownDomainsCarryNoInventedOwner) {
  std::vector<DomainRow> rows = {Row("cdn.unknown.net", 0, 0, 3)};
  PageTotals totals;
  totals.allowed = 3;
  totals.distinct_domains = 1;

  base::test::TestFuture<PageAnalysis> future;
  service_->AnalyzeCurrentPage(GURL("https://news.example/"), std::move(rows),
                               totals, future.GetCallback());
  const PageAnalysis analysis = future.Take();

  ASSERT_EQ(1u, analysis.rows.size());
  EXPECT_EQ("cdn.unknown.net", analysis.rows[0].domain);
  EXPECT_EQ("", analysis.rows[0].entity_name);
  EXPECT_EQ(Category::kUnknown, analysis.rows[0].category);
}

// A page with no third-party requests has no ratio to report. "0% protected"
// on a clean page is both false and alarming.
TEST_F(PrivacyPageSignalsTest, CleanPageHasNoProtectionPercentage) {
  base::test::TestFuture<PageAnalysis> future;
  service_->AnalyzeCurrentPage(GURL("https://clean.example/"), {},
                               PageTotals(), future.GetCallback());
  const PageAnalysis analysis = future.Take();

  EXPECT_TRUE(analysis.rows.empty());
  EXPECT_FALSE(analysis.protection.has_value());
  EXPECT_EQ(IntensityBand::kMinimal, analysis.intensity.band);
}

// The §9.2.1 signal has to reach the score, or detecting it changes nothing
// the user can see.
TEST_F(PrivacyPageSignalsTest, WebrtcRequestRaisesTrackingIntensity) {
  const GURL page("https://meet.example/");
  PageTotals totals;
  totals.detected = 1;
  totals.distinct_domains = 1;
  auto analyze = [&] {
    base::test::TestFuture<PageAnalysis> future;
    service_->AnalyzeCurrentPage(page, {Row("a.example", 1, 0, 0)}, totals,
                                 future.GetCallback());
    return future.Take();
  };

  const uint8_t before = analyze().intensity.points;
  service_->RecordWebrtcAddressRequest(page, /*local_addresses_withheld=*/true);
  const uint8_t after = analyze().intensity.points;

  EXPECT_GT(after, before)
      << "a page that took the user's network address must score higher";
}

}  // namespace
}  // namespace zephyrus_privacy

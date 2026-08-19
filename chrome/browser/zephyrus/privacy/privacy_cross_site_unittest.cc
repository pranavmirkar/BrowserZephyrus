// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// §6.4 + §9.5. §16 budgets ZERO false positives here, so most of this file is
// about what must NOT be reported: the claim "this company linked your activity
// across these sites" is the strongest thing the product says, and getting it
// wrong once discredits every other number on the screen.

#include "chrome/browser/zephyrus/privacy/privacy_cross_site.h"

#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace zephyrus_privacy {
namespace {

CrossSiteSite Site(std::string etld1,
                   bool owned = false,
                   uint32_t blocked = 1,
                   uint32_t allowed = 0) {
  CrossSiteSite s;
  s.etld1 = std::move(etld1);
  s.owned_by_entity = owned;
  s.blocked = blocked;
  s.allowed = allowed;
  return s;
}

CrossSiteEntity Entity(uint16_t id,
                       std::string name,
                       std::vector<CrossSiteSite> sites) {
  CrossSiteEntity e;
  e.entity_id = id;
  e.entity_name = std::move(name);
  e.sites = std::move(sites);
  return e;
}

TEST(PrivacyCrossSiteTest, ThreeThirdPartySitesQualifies) {
  auto out = SelectCrossSiteEntities({Entity(
      7, "Meta",
      {Site("news-site.com"), Site("shop-site.com"), Site("forum-site.com")})});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(3u, out[0].qualifying_sites);
  EXPECT_TRUE(out[0].qualifies());
}

TEST(PrivacyCrossSiteTest, TwoSitesDoesNotQualify) {
  auto out = SelectCrossSiteEntities(
      {Entity(7, "Meta", {Site("news-site.com"), Site("shop-site.com")})});
  EXPECT_TRUE(out.empty());
}

// §9.5, and the reason this file exists. facebook.com loading fbcdn.net is one
// company talking to itself. Counting the entity's own site toward the
// threshold would report cross-site tracking for a company the user visited
// directly — the headline false positive.
TEST(PrivacyCrossSiteTest, OwnSiteDoesNotCountTowardTheThreshold) {
  auto out = SelectCrossSiteEntities(
      {Entity(7, "Meta",
              {Site("facebook.com", /*owned=*/true), Site("news-site.com"),
               Site("shop-site.com")})});
  EXPECT_TRUE(out.empty())
      << "two third-party sites plus the company's own site is not three sites";
}

// But it is still SHOWN. §6.4's own example lists facebook.com among Meta's
// sites; omitting it would make the list look arbitrary to anyone who knows
// they were just on it.
TEST(PrivacyCrossSiteTest, OwnSiteIsStillDisplayed) {
  auto out = SelectCrossSiteEntities(
      {Entity(7, "Meta",
              {Site("facebook.com", /*owned=*/true), Site("news-site.com"),
               Site("shop-site.com"), Site("forum-site.com")})});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(3u, out[0].qualifying_sites);
  EXPECT_EQ(4u, out[0].sites.size());
  // Owned sites sort last: they are context, not evidence.
  EXPECT_TRUE(out[0].sites.back().owned_by_entity);
  EXPECT_EQ("facebook.com", out[0].sites.back().etld1);
}

// §4.2: an unattributed domain has no owner, so there is nothing to link
// across sites. It can never be cross-site, however many sites it appears on.
TEST(PrivacyCrossSiteTest, UnattributedDomainsNeverQualify) {
  auto out = SelectCrossSiteEntities({Entity(
      kNoEntity, "",
      {Site("a.com"), Site("b.com"), Site("c.com"), Site("d.com")})});
  EXPECT_TRUE(out.empty());
}

// §6.4: "If anything was ALLOWED, say so prominently. Hiding a leak to improve
// the number defeats the premise."
TEST(PrivacyCrossSiteTest, AllowedRequestsAreSurfaced) {
  auto out = SelectCrossSiteEntities({Entity(
      7, "Meta",
      {Site("a.com", false, /*blocked=*/3, /*allowed=*/0),
       Site("b.com", false, 4, 0), Site("c.com", false, 2, /*allowed=*/1)})});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(9u, out[0].blocked);
  EXPECT_EQ(1u, out[0].allowed);
  EXPECT_TRUE(out[0].anything_got_through())
      << "one request through is a leak and must be sayable";
}

TEST(PrivacyCrossSiteTest, FullyBlockedEntityReportsNoLeak) {
  auto out = SelectCrossSiteEntities(
      {Entity(7, "Meta",
              {Site("a.com"), Site("b.com"), Site("c.com")})});
  ASSERT_EQ(1u, out.size());
  EXPECT_FALSE(out[0].anything_got_through());
}

// Totals are recomputed from the sites, so a headline can never contradict the
// list printed directly beneath it.
TEST(PrivacyCrossSiteTest, TotalsAreDerivedFromTheSites) {
  CrossSiteEntity e = Entity(7, "Meta",
                             {Site("a.com", false, 2, 1),
                              Site("b.com", false, 3, 2),
                              Site("c.com", false, 4, 0)});
  // Deliberately wrong; must be overwritten rather than trusted.
  e.blocked = 999;
  e.allowed = 999;
  e.qualifying_sites = 99;

  auto out = SelectCrossSiteEntities({e});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(9u, out[0].blocked);
  EXPECT_EQ(3u, out[0].allowed);
  EXPECT_EQ(3u, out[0].qualifying_sites);
}

TEST(PrivacyCrossSiteTest, MostWidespreadEntityComesFirst) {
  auto out = SelectCrossSiteEntities(
      {Entity(1, "Small", {Site("a.com"), Site("b.com"), Site("c.com")}),
       Entity(2, "Big",
              {Site("a.com"), Site("b.com"), Site("c.com"), Site("d.com"),
               Site("e.com")})});
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ("Big", out[0].entity_name);
  EXPECT_EQ("Small", out[1].entity_name);
}

TEST(PrivacyCrossSiteTest, EmptyInputIsEmptyOutput) {
  EXPECT_TRUE(SelectCrossSiteEntities({}).empty());
}

// An entity seen only on its own sites — a CDN talking to its parent across
// several of that parent's properties — must not be reported at all.
TEST(PrivacyCrossSiteTest, EntitySeenOnlyOnItsOwnSitesIsSilent) {
  auto out = SelectCrossSiteEntities(
      {Entity(7, "Meta",
              {Site("facebook.com", true), Site("instagram.com", true),
               Site("whatsapp.com", true), Site("messenger.com", true)})});
  EXPECT_TRUE(out.empty());
}

// §9.4 / §16. The aggregator files every request from a page with no
// registrable domain (IP literal, localhost, about:, a single-label intranet
// name) under the synthetic site named kNoSiteName. It is a placeholder, and
// counting it as a site would let two real sites plus one unattributable page
// produce "seen on 3 sites you visited" — a false positive on the strongest
// claim the product makes, naming something the user cannot have visited.
//
// Found by the 50-site acceptance harness, which checks every named site
// against the set actually browsed and caught "(no site)" in an Amazon claim.
TEST(PrivacyCrossSiteTest, SyntheticNoSiteDoesNotCountTowardTheThreshold) {
  auto out = SelectCrossSiteEntities(
      {Entity(7, "Amazon.com",
              {Site("news-site.com"), Site("shop-site.com"),
               Site(kNoSiteName)})});
  EXPECT_TRUE(out.empty())
      << "two real sites plus the no-site placeholder is not three sites";
}

TEST(PrivacyCrossSiteTest, SyntheticNoSiteIsNotEvenDisplayed) {
  auto out = SelectCrossSiteEntities(
      {Entity(7, "Amazon.com",
              {Site("a.com"), Site("b.com"), Site("c.com"),
               Site(kNoSiteName)})});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(3u, out[0].qualifying_sites);
  ASSERT_EQ(3u, out[0].sites.size()) << "the placeholder must not be listed";
  for (const CrossSiteSite& s : out[0].sites) {
    EXPECT_NE(kNoSiteName, s.etld1);
  }
}

// An empty site name is the same kind of non-answer and must behave the same.
TEST(PrivacyCrossSiteTest, EmptySiteNameIsDroppedToo) {
  auto out = SelectCrossSiteEntities(
      {Entity(7, "Meta", {Site("a.com"), Site("b.com"), Site("")})});
  EXPECT_TRUE(out.empty());
}

// -- §6.7 exposure -----------------------------------------------------------

// The ordering IS the argument: "what got through matters most, so it goes on
// top". A company that leaked one request outranks one that made a thousand and
// leaked none — sorting by volume would bury the only entry that matters.
TEST(PrivacyCrossSiteTest, ExposureputsLeaksAboveVolume) {
  const auto out = SelectExposure({
      Entity(1, "Loud", {Site("a.com", false, /*blocked=*/1000, /*allowed=*/0)}),
      Entity(2, "Leaky", {Site("b.com", false, /*blocked=*/1, /*allowed=*/1)}),
  });
  ASSERT_EQ(2u, out.size());
  EXPECT_EQ("Leaky", out[0].entity_name);
  EXPECT_EQ("Loud", out[1].entity_name);
}

TEST(PrivacyCrossSiteTest, ExposureCountsRequestsAndSites) {
  const auto out = SelectExposure({Entity(
      1, "Google",
      {Site("a.com", false, 20, 1), Site("b.com", false, 25, 1)})});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(2u, out[0].sites);
  EXPECT_EQ(45u, out[0].blocked);
  EXPECT_EQ(2u, out[0].allowed);
  EXPECT_EQ(47u, out[0].requests);
}

// Unlike cross-site, exposure has NO threshold: one site is still someone who
// reached you, and §6.7 asks who reached you.
TEST(PrivacyCrossSiteTest, ExposureHasNoSiteThreshold) {
  const auto out =
      SelectExposure({Entity(1, "Solo", {Site("only.com", false, 3, 0)})});
  EXPECT_EQ(1u, out.size());
}

// ...and no ownership suppression: reaching you on their own site is still
// reaching you. That exclusion belongs to the cross-site CLAIM, not to a count.
TEST(PrivacyCrossSiteTest, ExposureCountsAnEntitysOwnSite) {
  const auto out = SelectExposure(
      {Entity(1, "Meta", {Site("facebook.com", /*owned=*/true, 5, 0)})});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(1u, out[0].sites);
  EXPECT_EQ(5u, out[0].blocked);
}

// Exposure counts sites too, so it needs the same exclusion — and an entity
// seen ONLY on the placeholder has no site to report at all.
TEST(PrivacyCrossSiteTest, ExposureExcludesTheSyntheticNoSite) {
  const auto out = SelectExposure({Entity(
      1, "Amazon.com",
      {Site("real.com", false, 4, 1), Site(kNoSiteName, false, 9, 9)})});
  ASSERT_EQ(1u, out.size());
  EXPECT_EQ(1u, out[0].sites);
  EXPECT_EQ(4u, out[0].blocked) << "placeholder requests must not be counted";
  EXPECT_EQ(1u, out[0].allowed);
}

TEST(PrivacyCrossSiteTest, ExposureDropsAnEntitySeenOnlyOnTheNoSite) {
  EXPECT_TRUE(
      SelectExposure({Entity(1, "Ghost", {Site(kNoSiteName, false, 5, 5)})})
          .empty());
}

// §6.7 + §9.5. A publisher loading its own asset domain is a different
// registrable domain, so it lands in this list and often at the top of it --
// their own domains are the ones they load most. The row is kept and counted,
// but flagged, because "who tried to reach you" reads as an accusation against
// the site the user deliberately opened otherwise.
TEST(PrivacyCrossSiteTest, ExposureFlagsAnEntitySeenOnlyOnItsOwnSites) {
  const auto out = SelectExposure({Entity(
      1, "BBC",
      {Site("bbci.co.uk", /*owned=*/true, 40, 39),
       Site("bbc.com", /*owned=*/true, 5, 4)})});
  ASSERT_EQ(1u, out.size());
  EXPECT_TRUE(out[0].own_sites_only);
  EXPECT_EQ(2u, out[0].sites) << "still counted, not suppressed";
  EXPECT_EQ(43u, out[0].allowed);
}

// One third-party site is enough to make it a genuine third party, however
// many of its own sites it was also seen on.
TEST(PrivacyCrossSiteTest, ExposureDoesNotFlagAMixedEntity) {
  const auto out = SelectExposure({Entity(
      1, "Google",
      {Site("google.com", /*owned=*/true, 3, 1),
       Site("news-site.com", /*owned=*/false, 2, 1)})});
  ASSERT_EQ(1u, out.size());
  EXPECT_FALSE(out[0].own_sites_only);
}

TEST(PrivacyCrossSiteTest, ExposureDoesNotFlagAPureThirdParty) {
  const auto out = SelectExposure(
      {Entity(1, "Tracker", {Site("a.com", false, 2, 1), Site("b.com", false, 1, 1)})});
  ASSERT_EQ(1u, out.size());
  EXPECT_FALSE(out[0].own_sites_only);
}

TEST(PrivacyCrossSiteTest, ExposureSkipsUnattributed) {
  EXPECT_TRUE(
      SelectExposure({Entity(kNoEntity, "", {Site("a.com")})}).empty());
}

}  // namespace
}  // namespace zephyrus_privacy

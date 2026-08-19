// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CROSS_SITE_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CROSS_SITE_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "chrome/browser/zephyrus/privacy/privacy_event.h"

namespace zephyrus_privacy {

// §6.4 cross-site tracking monitor, and the §9.5 ownership rule that keeps it
// honest.
//
// **The claim being made is strong**, so the bar is high: "this company could
// have linked your activity across these sites." §16 budgets ZERO false
// positives across a 50-site manual set, and the way this goes wrong is not
// subtle — telling someone Facebook tracked them across four sites when three
// of those sites were Facebook's own is the kind of error that discredits every
// other number on the screen.
//
// Pure functions over plain values, in their own translation unit, for the same
// reason as the scores: the rule can then be tested exhaustively without a
// database, and the UI cannot quietly implement a second version of it.

// §6.4: "≥ 3 distinct eTLD+1 sites within the retention window".
inline constexpr uint32_t kCrossSiteThreshold = 3;

// One site an entity was seen on.
struct CrossSiteSite {
  std::string etld1;
  // §9.5: the entity's OWN site. `facebook.com` loading `fbcdn.net` is one
  // company talking to itself, not cross-site tracking.
  //
  // Such a site still appears in the list — §6.4's mock shows facebook.com
  // among Meta's sites, marked "(first-party)", because hiding it would make
  // the list look arbitrary — but it does NOT count toward the threshold.
  bool owned_by_entity = false;
  uint32_t detected = 0;
  uint32_t blocked = 0;
  uint32_t allowed = 0;
};

struct CrossSiteEntity {
  uint16_t entity_id = kNoEntity;
  // Empty is not possible for a qualifying entity: an unattributed domain has
  // no entity to aggregate across sites, so it can never reach the threshold.
  // That is a deliberate consequence of §4.2 — we do not invent owners, and
  // without an owner there is nothing to link.
  std::string entity_name;
  std::vector<CrossSiteSite> sites;
  // Sites NOT owned by the entity. This is what the threshold tests.
  uint32_t qualifying_sites = 0;
  uint32_t detected = 0;
  uint32_t blocked = 0;
  uint32_t allowed = 0;

  bool qualifies() const { return qualifying_sites >= kCrossSiteThreshold; }
  // §6.4: "If anything was ALLOWED, say so prominently. Hiding a leak to
  // improve the number defeats the premise."
  bool anything_got_through() const { return allowed > 0; }
};

// Filters `candidates` to the entities that genuinely qualify, applying §9.5.
//
// Input rows may be in any order and may contain entities that do not qualify;
// output contains only qualifying entities, each with its sites ordered for
// display (owned sites last, then by request count descending, then by name so
// two opens of the same view agree).
std::vector<CrossSiteEntity> SelectCrossSiteEntities(
    std::vector<CrossSiteEntity> candidates);

// -- §6.7 Privacy exposure ---------------------------------------------------
//
// "Who tried to reach you today", as COUNTS.
//
// §6.7 opens by rejecting the obvious design: "Dot ratings imply a magnitude no
// observed event supports." Three dots out of five is a judgement we have no
// evidence for; "47 requests, 45 blocked, 2 allowed" is four facts we watched
// happen. Nothing in this struct is a score.
struct ExposureEntity {
  uint16_t entity_id = kNoEntity;
  std::string entity_name;
  uint32_t requests = 0;
  uint32_t sites = 0;
  uint32_t blocked = 0;
  uint32_t allowed = 0;
  // Every site this entity was seen on is a site it OWNS (§9.5) — so this is
  // the publisher of a page the user chose to open, not a third party that
  // followed them there.
  //
  // It is still counted and still listed: `bbc.com` loading `bbci.co.uk` is a
  // different registrable domain, the requests really happened, and dropping
  // them would understate what left the device. But a heading that reads "who
  // tried to reach you" turns into an accusation against the site the user
  // deliberately visited unless the row says which one it is. In practice
  // publishers dominate the top of this list, because their own asset domains
  // are the ones they load most.
  bool own_sites_only = false;
};

// Every attributed entity, ordered by §6.7's rule: allowed descending, then
// request count. "What got through matters most, so it goes on top" — the
// ordering is the argument, not decoration. An entity that leaked once
// outranks one that made a thousand requests and leaked none.
//
// Unlike SelectCrossSiteEntities there is NO threshold and no ownership
// suppression: this answers "who reached you", and a company reaching you on
// its own site still reached you. Ownership is REPORTED here rather than
// applied — see ExposureEntity::own_sites_only — because the cross-site claim
// is a claim, while this is a count, and a count should not be edited.
std::vector<ExposureEntity> SelectExposure(
    const std::vector<CrossSiteEntity>& candidates);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_CROSS_SITE_H_

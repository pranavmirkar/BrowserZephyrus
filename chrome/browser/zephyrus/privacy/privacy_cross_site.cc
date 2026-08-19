// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_cross_site.h"

#include <algorithm>
#include <utility>

#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"

namespace zephyrus_privacy {

std::vector<CrossSiteEntity> SelectCrossSiteEntities(
    std::vector<CrossSiteEntity> candidates) {
  std::vector<CrossSiteEntity> qualifying;
  qualifying.reserve(candidates.size());

  for (CrossSiteEntity& entity : candidates) {
    // An unattributed domain has no owner to link anything across, so it can
    // never be cross-site. §4.2 forbids guessing one, which makes this a
    // correctness rule rather than a coverage gap.
    if (entity.entity_id == kNoEntity) {
      continue;
    }

    // Recount from the sites rather than trusting whatever the caller put in
    // the totals. The counts are what the user reads, and a caller that
    // populated them from a different query would produce a headline that its
    // own site list contradicts.
    // §9.4: the synthetic "no site" is a placeholder for a page that HAS no
    // registrable domain, not a place the user went. Counting it toward the
    // threshold would let two real sites plus one unattributable page produce
    // the sentence "seen on 3 sites you visited" — a false positive on the
    // strongest claim the product makes, and the claim would name a site the
    // user cannot have visited because it is not a site.
    std::erase_if(entity.sites, [](const CrossSiteSite& s) {
      return s.etld1 == kNoSiteName || s.etld1.empty();
    });

    entity.qualifying_sites = 0;
    entity.detected = 0;
    entity.blocked = 0;
    entity.allowed = 0;
    for (const CrossSiteSite& site : entity.sites) {
      entity.detected += site.detected;
      entity.blocked += site.blocked;
      entity.allowed += site.allowed;
      // §9.5: the entity's own sites are shown but never counted toward the
      // threshold. This is the whole false-positive defence.
      if (!site.owned_by_entity) {
        ++entity.qualifying_sites;
      }
    }

    if (!entity.qualifies()) {
      continue;
    }

    // §6.4's list order: third-party sites first, because they are the reason
    // the entry exists; the entity's own site is context, not evidence. Then by
    // volume, then by name so the same data yields the same order every time.
    std::sort(entity.sites.begin(), entity.sites.end(),
              [](const CrossSiteSite& a, const CrossSiteSite& b) {
                if (a.owned_by_entity != b.owned_by_entity) {
                  return !a.owned_by_entity;
                }
                const uint32_t a_total = a.detected + a.blocked + a.allowed;
                const uint32_t b_total = b.detected + b.blocked + b.allowed;
                if (a_total != b_total) {
                  return a_total > b_total;
                }
                return a.etld1 < b.etld1;
              });
    qualifying.push_back(std::move(entity));
  }

  // Most-spread first: the company on the most sites is the one the user most
  // needs to know about.
  std::sort(qualifying.begin(), qualifying.end(),
            [](const CrossSiteEntity& a, const CrossSiteEntity& b) {
              if (a.qualifying_sites != b.qualifying_sites) {
                return a.qualifying_sites > b.qualifying_sites;
              }
              return a.entity_name < b.entity_name;
            });
  return qualifying;
}

std::vector<ExposureEntity> SelectExposure(
    const std::vector<CrossSiteEntity>& candidates) {
  std::vector<ExposureEntity> out;
  out.reserve(candidates.size());
  for (const CrossSiteEntity& c : candidates) {
    if (c.entity_id == kNoEntity || c.sites.empty()) {
      continue;  // No owner, nothing to name (§4.2).
    }
    ExposureEntity e;
    e.entity_id = c.entity_id;
    e.entity_name = c.entity_name;
    e.own_sites_only = true;  // Until a site it does not own turns up.
    for (const CrossSiteSite& s : c.sites) {
      // Same §9.4 exclusion as above. "Sites" is a count the user reads, and
      // the placeholder is not one of them.
      if (s.etld1 == kNoSiteName || s.etld1.empty()) {
        continue;
      }
      ++e.sites;
      if (!s.owned_by_entity) {
        e.own_sites_only = false;
      }
      e.blocked += s.blocked;
      e.allowed += s.allowed;
      e.requests += s.detected + s.blocked + s.allowed;
    }
    if (e.sites == 0) {
      continue;
    }
    out.push_back(std::move(e));
  }

  std::sort(out.begin(), out.end(),
            [](const ExposureEntity& a, const ExposureEntity& b) {
              // §6.7: what got through goes on top, whatever the volume.
              if (a.allowed != b.allowed) {
                return a.allowed > b.allowed;
              }
              if (a.requests != b.requests) {
                return a.requests > b.requests;
              }
              return a.entity_name < b.entity_name;
            });
  return out;
}

}  // namespace zephyrus_privacy

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/privacy_tab_helper.h"

#include <utility>

#include "base/hash/hash.h"
#include "chrome/browser/zephyrus/privacy/privacy_event_sink.h"
#include "chrome/browser/zephyrus/privacy/privacy_host_canon.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "base/strings/strcat.h"
#include "content/public/browser/cookie_access_details.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "third_party/blink/public/mojom/loader/resource_load_info.mojom.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

// The tail bucket's name. Named rather than blank: a blank row is
// indistinguishable from a lookup failure, and the user is entitled to know the
// list was truncated rather than that the page contacted only 256 domains.
constexpr char kOverflowDomain[] = "(other)";

// The four counters both DomainRow and PageTotals carry, addressed as a group
// so the status switch is written once. A template rather than a cast between
// the two structs: they do not share a layout — DomainRow leads with a
// std::string — so any reinterpretation of one as the other is undefined
// behaviour that happens to look plausible.
template <typename Counted>
void AddStatus(Counted& row, TrackerStatus status) {
  switch (status) {
    case TrackerStatus::kDetected:
      ++row.detected;
      break;
    case TrackerStatus::kBlocked:
      ++row.blocked;
      break;
    case TrackerStatus::kAllowed:
      ++row.allowed;
      break;
    case TrackerStatus::kRandomized:
      ++row.randomized;
      break;
    case TrackerStatus::kPotential:
      // §2.1: a heuristic match is excluded from headline numbers. Counting it
      // anywhere here would put it into the "N req" column and into both
      // scores, which is exactly what "excluded" rules out.
      break;
  }
}

}  // namespace

PrivacyTabHelper::PrivacyTabHelper(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<PrivacyTabHelper>(*contents) {}

PrivacyTabHelper::~PrivacyTabHelper() = default;

void PrivacyTabHelper::RecordRequest(std::string_view domain,
                                     TrackerStatus status) {
  if (domain.empty()) {
    return;
  }

  // Totals first and unconditionally, so the cap below can never make the
  // headline number smaller than what actually happened.
  AddStatus(totals_, status);

  auto it = domains_.find(domain);
  if (it == domains_.end()) {
    if (domains_.size() >= kMaxDomainsPerPage) {
      // Past the cap the domain is never inserted, so the lookup above misses
      // again on its NEXT request, and its one after that. Counting distinct
      // domains here would therefore count REQUESTS, and the arithmetic panel
      // would show "third-party domains: 4,721" for a page that contacted 300.
      // The count saturates at the cap instead: a floor rather than a fiction,
      // and no effect on the score, whose top rung is 20 domains.
      AddStatus(overflow_, status);
      return;
    }
    it = domains_.emplace(std::string(domain), DomainRow()).first;
    it->second.domain = it->first;
    ++totals_.distinct_domains;
  }
  AddStatus(it->second, status);
}

std::vector<PrivacyTabHelper::DomainRow> PrivacyTabHelper::PageRows() const {
  std::vector<DomainRow> rows;
  rows.reserve(domains_.size() + 1);
  for (const auto& [domain, row] : domains_) {
    rows.push_back(row);
  }
  if (overflow_.requests() > 0 || overflow_.randomized > 0) {
    DomainRow tail = overflow_;
    tail.domain = kOverflowDomain;
    rows.push_back(std::move(tail));
  }
  return rows;
}


void PrivacyTabHelper::ResourceLoadComplete(
    content::RenderFrameHost* render_frame_host,
    const content::GlobalRequestID& request_id,
    const blink::mojom::ResourceLoadInfo& info) {
  // The blocker cancels its own requests with ERR_BLOCKED_BY_CLIENT and records
  // kBlocked itself, at the moment it makes the decision — which is the only
  // place that knows the cancellation was OURS rather than someone else's.
  // Counting it again here would double every blocked request.
  if (info.net_error == net::ERR_BLOCKED_BY_CLIENT) {
    return;
  }

  // §2.1: ALLOWED means "request completed; data left the device". Only net::OK
  // supports that. Anything else — aborted by a navigation, DNS failure,
  // connection reset — was seen and did not deliver, which is DETECTED and
  // nothing stronger.
  const TrackerStatus status = info.net_error == net::OK
                                   ? TrackerStatus::kAllowed
                                   : TrackerStatus::kDetected;

  // final_url, not original_url: a redirect chain's hops are separate loads,
  // and this one describes where the request actually ended up.
  const GURL& url = info.final_url;
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return;  // data:, blob:, about: — no eTLD+1 exists (§9.2).
  }
  // host() is a view into `url`'s own storage, which outlives this scope.
  const std::string_view host = CanonicalHostForHash(url.host());
  if (host.empty()) {
    return;
  }

  content::WebContents* contents = web_contents();
  if (!contents) {
    return;
  }
  PrivacyIntelligenceService* service =
      PrivacyIntelligenceServiceFactory::GetForBrowserContext(
          contents->GetBrowserContext());
  if (!service) {
    return;  // Feature off, or an off-the-record profile.
  }

  // §9.1: uncloak BEFORE deciding whether this is a third party. That order is
  // the entire point — `metrics.example.com` on `example.com` looks first-party
  // and would be skipped two lines below, which is exactly how a cloaked
  // tracker makes a page read as clean.
  std::string effective_domain(host);
  bool cloaked = false;
  if (auto cache = service->cname_cache()) {
    if (std::optional<std::string> canonical = cache->CanonicalEtld1(host)) {
      effective_domain = *canonical;
      cloaked = true;
    }
  }

  // Only third parties. The page's own requests are not tracking, and counting
  // them would bury the ones that are. A cloaked host is judged on what it
  // really resolves to, so hiding behind the first party no longer works.
  const GURL top_url =
      contents->GetPrimaryMainFrame()->GetLastCommittedURL();
  const std::string top_etld1 =
      net::registry_controlled_domains::GetDomainAndRegistry(
          top_url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (cloaked) {
    if (effective_domain == top_etld1) {
      return;  // Resolves back into the page's own site; not a third party.
    }
  } else if (net::registry_controlled_domains::SameDomainOrHost(
                 url, top_url,
                 net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
    return;
  }

  RecordRequest(effective_domain, status);

  // The durable record. Same call as the per-page view above, so the popup and
  // the database cannot disagree — two consumers of one event.
  const uint32_t site_id = SiteIdForEtld1(top_etld1);
  // Hashed on the EFFECTIVE domain, so a cloaked request is attributed to the
  // company that really received it rather than to the disguise.
  const uint32_t domain_hash = base::PersistentHash(effective_domain);

  if (auto strings = service->domain_strings()) {
    if (!top_etld1.empty()) {
      strings->RecordSite(site_id, top_etld1);
    }
    // Mirror the aggregator's remap of hash 0, its "(other)" bucket.
    strings->Record(domain_hash ? domain_hash : 1u, effective_domain);
  }
  if (auto sink = service->sink()) {
    RawEvent event{};
    event.site_id = site_id;
    event.domain_hash = domain_hash;
    event.ticks_delta_ms = PrivacyTicksDeltaMs();
    event.entity_id = kNoEntity;
    event.category = static_cast<uint8_t>(Category::kUnknown);
    event.status = static_cast<uint8_t>(status);
    // Return value ignored: a full ring drops and counts (§8.5).
    sink->Record(event);
  }
}

void PrivacyTabHelper::OnCookiesAccessed(
    content::RenderFrameHost* render_frame_host,
    const content::CookieAccessDetails& details) {
  RecordCookieAccess(details);
}

void PrivacyTabHelper::OnCookiesAccessed(
    content::NavigationHandle* navigation_handle,
    const content::CookieAccessDetails& details) {
  RecordCookieAccess(details);
}

void PrivacyTabHelper::RecordCookieAccess(
    const content::CookieAccessDetails& details) {
  // §6.6 says "third-party cookie blocked", and §6.3's input is the tracking
  // kind. A site's own cookies are how it keeps you logged in; counting them
  // would score every working website as hostile and make the number useless.
  //
  // first_party_url is the browser's own record of the top-level context for
  // this access, so it is the right comparison even for a cookie read during a
  // navigation, when there is no committed page to ask.
  if (!details.url.is_valid() || !details.first_party_url.is_valid()) {
    return;
  }
  if (net::registry_controlled_domains::SameDomainOrHost(
          details.url, details.first_party_url,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)) {
    return;
  }

  // One notification can carry many cookies, and the same cookie comes back on
  // every request that sends it. Count each DISTINCT cookie once, keyed by the
  // identity that makes a cookie a cookie -- name, domain and path -- so
  // "a=1; b=2" on one request is two, and "a=1" on forty requests is one.
  for (const net::CookieWithAccessResult& cookie :
       details.cookie_access_result_list) {
    if (cookie_keys_.size() >= kMaxCookiesPerPage) {
      break;  // A floor past the cap, exactly like distinct_domains.
    }
    // Separated by a byte that cannot occur in any of the three parts, so
    // ("ab", "c") and ("a", "bc") cannot collide into one key.
    const std::string key =
        base::StrCat({cookie.cookie.Name(), "\n", cookie.cookie.Domain(),
                      "\n", cookie.cookie.Path()});
    if (cookie_keys_.insert(key).second) {
      ++totals_.cookies_attempted;
    }
    // Blocked is a property of the ACCESS, not of the cookie, so a cookie
    // blocked once and allowed later stays counted as blocked. That is the
    // conservative direction: it can understate Tracking Intensity, never
    // claim a protection that did not happen.
    if (details.blocked_by_policy && blocked_cookie_keys_.insert(key).second) {
      ++totals_.cookies_blocked;
    }
  }
}

void PrivacyTabHelper::PrimaryPageChanged(content::Page& page) {
  // Same reason the domain map is cleared below: these identify cookies from
  // the PREVIOUS page, and keeping them would under-count the next one.
  cookie_keys_.clear();
  blocked_cookie_keys_.clear();
  // §6.2 describes THIS page. Carrying counts across a navigation would
  // attribute one site's trackers to another, which is worse than showing
  // nothing.
  domains_.clear();
  overflow_ = DomainRow();
  totals_ = PageTotals();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(PrivacyTabHelper);

}  // namespace zephyrus_privacy

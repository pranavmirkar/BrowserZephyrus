// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_TAB_HELPER_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_TAB_HELPER_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace content {
class Page;
class RenderFrameHost;
class WebContents;
struct GlobalRequestID;
}  // namespace content

namespace blink::mojom {
class ResourceLoadInfo;
}

namespace content {
class NavigationHandle;
struct CookieAccessDetails;
}  // namespace content

namespace zephyrus_privacy {

// Per-tab, per-page-load tracker counts for §6.2's current-site analysis.
//
// **Why this exists rather than reading the aggregator.** PrivacyAggregator
// looks like the right source — it already holds per-(site, domain) counters —
// but TakeRows() CLEARS them on every flush, once every five seconds. A popup
// opened just after a flush would find almost nothing and report "no trackers"
// on a page that had loaded forty. The database is no better: it holds daily
// aggregates, and §6.2 asks about this page load.
//
// So the popup needs its own accumulator with page-load lifetime, which is
// exactly what a WebContentsUserData reset on PrimaryPageChanged is. This
// mirrors ZephyrusAdblockTabHelper, which solves the same problem for the
// toolbar badge.
//
// **Not a second source of truth.** These counts feed the popup only. The
// durable record still goes through the ring buffer to the aggregator and the
// database; nothing here is persisted, and nothing here can contradict what is
// stored, because both are fed from the same call at the emission point.
class PrivacyTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<PrivacyTabHelper> {
 public:
  // One third-party domain contacted by the current page.
  struct DomainRow {
    std::string domain;
    uint32_t detected = 0;
    uint32_t blocked = 0;
    uint32_t allowed = 0;
    uint32_t randomized = 0;

    // What §6.2 shows in the "N req" column. Excludes kPotential, which §2.1
    // keeps out of headline numbers because it is a heuristic match.
    uint32_t requests() const { return detected + blocked + allowed; }
  };

  ~PrivacyTabHelper() override;
  PrivacyTabHelper(const PrivacyTabHelper&) = delete;
  PrivacyTabHelper& operator=(const PrivacyTabHelper&) = delete;

  // Called from the emission point, on the UI thread, once per classified
  // request. Must stay cheap: this is on the request path (§8.1).
  void RecordRequest(std::string_view domain, TrackerStatus status);

  // A snapshot of the current page, unsorted and unresolved. The caller resolves
  // entities and orders the rows — that work needs the entity dataset, which
  // lives on the privacy sequence, and must not be done here on the UI thread.
  std::vector<DomainRow> PageRows() const;

  // Request totals across every domain, including any folded into the tail
  // bucket, so the headline count never disagrees with reality just because the
  // page contacted more domains than we list. `distinct_domains` is the one
  // field that does saturate — see its own comment.
  struct PageTotals {
    uint32_t detected = 0;
    uint32_t blocked = 0;
    uint32_t allowed = 0;
    uint32_t randomized = 0;
    // Distinct third-party domains, SATURATING at kMaxDomainsPerPage. Feeds
    // IntensityInputs. A floor rather than an exact count on a page past the
    // cap — see RecordRequest for why counting them exactly would count
    // requests instead. No effect on the score: its top rung is 20 domains.
    uint32_t distinct_domains = 0;
    // §6.3's fourth score input. THIRD-PARTY cookie accesses only: a site's
    // own cookies are how it remembers you are logged in, and counting them as
    // tracking would score every functioning website as hostile.
    //
    // "Attempted", not "set": the count must not depend on whether we happened
    // to stop it, or Protection Applied would be double-counted into the
    // site's own Tracking Intensity.
    //
    // DISTINCT cookies, not accesses. The UI calls this row "Tracking
    // cookies", so it has to be a number of cookies. Two other readings were
    // both wrong: one notification is not one cookie (CookieAccessDetails
    // carries a LIST, so a request sending ten of them arrived as one), and a
    // running total of accesses is not a number of cookies either — the same
    // cookie re-read on every request would climb for as long as the page was
    // open, and the score ladder would then measure how busy a page is rather
    // than how much it tracks.
    uint32_t cookies_attempted = 0;
    // The subset actually prevented, feeding §6.6's "Tracking cookies". Also
    // distinct, so it can be compared with the line above.
    uint32_t cookies_blocked = 0;
  };
  PageTotals totals() const { return totals_; }

  // Deliberately NO change notification. The panel reads a snapshot when it
  // opens and does not live-update, so a callback list here would be API with
  // no caller — and firing it per request during a page load would put a
  // notification on the request path for nobody's benefit. Add it back with a
  // subscriber, not before.

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  // Where a request's real outcome arrives (§2.1).
  //
  // **Why here and not by intercepting the loader.** An earlier version put a
  // URLLoaderClient interceptor on every unblocked request to learn whether it
  // completed. That works, but it reroutes responses that would go straight
  // from the network service to the renderer through the BROWSER process, and
  // OnTransferSizeUpdated fires repeatedly during every download — so it woke
  // the browser process continuously for bookkeeping.
  //
  // This notification is one the browser already receives, carries net_error
  // and the final URL, and arrives on the thread the per-page state lives on.
  // Same information, no interception, nothing added to the request path.
  void ResourceLoadComplete(
      content::RenderFrameHost* render_frame_host,
      const content::GlobalRequestID& request_id,
      const blink::mojom::ResourceLoadInfo& resource_load_info) override;

  // §6.3's tracking-cookie input and §6.6's "Tracking cookies" category.
  //
  // Both overloads: cookies are accessed by the navigation itself as well as
  // by the committed document, and a redirect chain's cookies arrive on the
  // NavigationHandle one. Taking only the frame overload would silently miss
  // the accesses that happen before a page exists — which is exactly when a
  // tracker most wants to read one.
  void OnCookiesAccessed(content::RenderFrameHost* render_frame_host,
                         const content::CookieAccessDetails& details) override;
  void OnCookiesAccessed(content::NavigationHandle* navigation_handle,
                         const content::CookieAccessDetails& details) override;

 private:
  friend class content::WebContentsUserData<PrivacyTabHelper>;

  explicit PrivacyTabHelper(content::WebContents* contents);

  // Shared by both OnCookiesAccessed overloads.
  void RecordCookieAccess(const content::CookieAccessDetails& details);

  // Identity of the cookies already counted on this page, so a cookie read on
  // every request is counted once. Bounded for the same reason `domains_` is:
  // a page can name as many cookies as it likes, and this is per tab.
  std::set<std::string> cookie_keys_;
  std::set<std::string> blocked_cookie_keys_;

  // Matches PrivacyAggregator::kMaxDomainsPerSite. A page past this is either
  // pathological or hostile (§11.3); the tail folds into one named bucket so
  // the counts stay true even though the names stop.
  static constexpr size_t kMaxDomainsPerPage = 256;
  // A page can name as many cookies as it likes, and these sets are per tab.
  // The score's top rung is far below this, so the cap cannot change a score.
  static constexpr size_t kMaxCookiesPerPage = 512;

  std::map<std::string, DomainRow, std::less<>> domains_;
  // Everything past the cap, counted but not named.
  DomainRow overflow_;
  PageTotals totals_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_PRIVACY_TAB_HELPER_H_

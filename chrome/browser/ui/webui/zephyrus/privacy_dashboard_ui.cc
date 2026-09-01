// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/zephyrus/privacy_dashboard_ui.h"

#include "base/feature_list.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/i18n/message_formatter.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/zephyrus/privacy/privacy_cross_site.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/l10n/l10n_util.h"

namespace zephyrus_privacy {
namespace {

// Every value that reaches the page goes through this. The strings here are
// site names and company names that ultimately originate from the network, so
// treating them as markup would be a stored-XSS hole in the one page whose
// whole subject is the user's browsing.
std::string Esc(const std::string& raw) {
  return base::EscapeForHTML(raw);
}

std::string Num(uint64_t n) {
  return base::NumberToString(n);
}

// Localised text.
//
// Deliberately NOT escaped, unlike Esc() above. These are resource strings
// authored by us and translated through the project's own pipeline — they are
// code, not runtime data. Escaping them would also make it impossible to pass
// markup through a placeholder, which the cross-site headline needs so the
// company name can be bold wherever the target language puts it.
std::string L(int message_id) {
  return l10n_util::GetStringUTF8(message_id);
}

// ICU counts are formatted, never concatenated: "1 site" and "5 sites" are not
// one string with a number glued on, and languages with more than two plural
// forms cannot be served by picking between two.
//
// int, not uint64_t: ICU takes a signed count, and a saturating cast is
// preferable to a silent wrap on an absurd value.
int PluralCount(uint64_t n) {
  return static_cast<int>(
      std::min<uint64_t>(n, std::numeric_limits<int>::max()));
}

std::string LCount(int message_id, uint64_t count) {
  return base::UTF16ToUTF8(base::i18n::MessageFormatter::FormatWithNamedArgs(
      l10n_util::GetStringUTF16(message_id), "COUNT", PluralCount(count)));
}

std::string LShownOfTotal(int message_id, uint64_t shown, uint64_t total) {
  return base::UTF16ToUTF8(base::i18n::MessageFormatter::FormatWithNamedArgs(
      l10n_util::GetStringUTF16(message_id), "SHOWN", PluralCount(shown),
      "TOTAL", PluralCount(total)));
}

// `entity_html` is already escaped AND already wrapped in its markup, because
// where the company name falls in the sentence is the translator's decision,
// not the layout's.
std::string LEntityCount(int message_id,
                         const std::string& entity_html,
                         uint64_t count) {
  return base::UTF16ToUTF8(base::i18n::MessageFormatter::FormatWithNamedArgs(
      l10n_util::GetStringUTF16(message_id), "ENTITY", entity_html, "COUNT",
      PluralCount(count)));
}

// §8.9 asks for pagination. One screenful, not the whole 5,000-row ring.
constexpr int kTimelinePageSize = 50;

// §6.7 has no site threshold, so unlike the cross-site list this one grows
// with ordinary browsing — a normal day already produced 27 companies in
// testing, and the dataset can name 19,130. The cap is on the same principle
// as the timeline's: a page that renders every row is a page nobody reads.
// Ordering makes the cut safe, since the rows that got through sort first.
constexpr size_t kMaxExposureRows = 50;

// Cross-site is bounded by its >=3 threshold, but only loosely: 50 sites of
// ordinary browsing produced 22 cards and 37 site rows in the largest one.
// The site list is EVIDENCE for the claim, not a ranked convenience, so it is
// cut later and the cut is always stated -- an unexplained short list would
// understate the spread the headline is asserting.
constexpr size_t kMaxCrossSiteCards = 25;
constexpr size_t kMaxSitesPerCard = 12;

constexpr char kStyle[] =
    "<style>"
    ":root{color-scheme:dark}"
    "body{background:#0e1123;color:#e8e9f0;"
    "font:14px/1.55 system-ui,-apple-system,Segoe UI,sans-serif;"
    "margin:0;padding:40px 24px}"
    "main{max-width:760px;margin:0 auto}"
    "h1{font-size:24px;font-weight:600;margin:0 0 4px}"
    "h2{font-size:15px;font-weight:600;margin:32px 0 10px}"
    ".sub{color:#9ba0b5;margin:0 0 28px}"
    ".card{background:#171a2e;border-radius:14px;padding:18px 20px;"
    "margin-bottom:12px}"
    ".big{font-size:30px;font-weight:600;letter-spacing:-.5px}"
    ".muted{color:#9ba0b5}"
    ".faint{color:#767c94;font-size:12px}"
    "table{width:100%;border-collapse:collapse}"
    "th{text-align:start;font-weight:500;color:#767c94;font-size:12px;"
    "padding:0 0 6px}"
    "td{padding:5px 0;vertical-align:top}"
    "td.n{text-align:end;font-variant-numeric:tabular-nums;color:#9ba0b5}"
    ".own{color:#767c94}"
    ".leak{color:#ffb454}"
    ".ok{color:#6ee7a8}"
    "</style>";

// §6.4. Names sites and companies, which is exactly why this page is separate
// from the diagnostics host.
std::string RenderCrossSite(const std::vector<CrossSiteEntity>& entities) {
  if (entities.empty()) {
    // An empty list is the common and good case, and it must not read as a
    // failure to load. It also must not overclaim: this covers the retention
    // window only, and only companies the dataset can name.
    return base::StrCat({
        "<h2>", L(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_HEADING),
        "</h2><div class=card><div>",
        L(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_EMPTY),
        "</div><div class=faint style='margin-top:6px'>",
        L(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_EMPTY_NOTE),
        "</div></div>"});
  }

  std::string out = base::StrCat(
      {"<h2>", L(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_HEADING), "</h2>"});
  for (const CrossSiteEntity& e : base::span(entities).first(
           std::min(entities.size(), kMaxCrossSiteCards))) {
    out += "<div class=card>";
    // "could have been used" — §6.4 is explicit that the claim is capability,
    // not evidence of linkage. We observed the requests; we did not observe
    // what was done with them (§2.1).
    out += base::StrCat({
        "<div>",
        LEntityCount(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_CLAIM,
                     base::StrCat({"<b>", Esc(e.entity_name), "</b>"}),
                     e.qualifying_sites),
        "</div><div class=muted style='margin-top:2px'>",
        L(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_CAPABILITY), "</div>"});

    out += base::StrCat({"<table style='margin-top:12px'><tr><th>",
                         L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_SITE),
                         "</th><th class=n>",
                         L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_BLOCKED),
                         "</th><th class=n>",
                         L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_ALLOWED),
                         "</th></tr>"});
    for (const CrossSiteSite& s : base::span(e.sites).first(
             std::min(e.sites.size(), kMaxSitesPerCard))) {
      out += base::StrCat({
          "<tr><td", s.owned_by_entity ? " class=own>" : ">", Esc(s.etld1),
          s.owned_by_entity
              ? base::StrCat({" <span class=faint>",
                              L(IDS_ZEPHYRUS_PRIVACY_DASH_OWN_SITE),
                              "</span>"})
              : std::string(),
          "</td><td class=n>", Num(s.blocked), "</td><td class=n>",
          Num(s.allowed), "</td></tr>"});
    }
    out += "</table>";
    if (e.sites.size() > kMaxSitesPerCard) {
      out += base::StrCat({"<div class=faint style='margin-top:6px'>",
                           LShownOfTotal(IDS_ZEPHYRUS_PRIVACY_DASH_SITES_TRUNCATED,
                                         kMaxSitesPerCard, e.sites.size()),
                           "</div>"});
    }

    // §6.4: "If anything was ALLOWED, say so prominently. Hiding a leak to
    // improve the number defeats the premise."
    if (e.anything_got_through()) {
      out += base::StrCat({"<div class=leak style='margin-top:10px'>",
                           LCount(IDS_ZEPHYRUS_PRIVACY_DASH_LEAK, e.allowed),
                           "</div>"});
    } else {
      out += base::StrCat(
          {"<div class=ok style='margin-top:10px'>",
           LCount(IDS_ZEPHYRUS_PRIVACY_DASH_ALL_BLOCKED, e.blocked), "</div>"});
    }
    out += "</div>";
  }
  if (entities.size() > kMaxCrossSiteCards) {
    out += base::StrCat(
        {"<div class='card faint'>",
         LShownOfTotal(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_TRUNCATED,
                       kMaxCrossSiteCards, entities.size()),
         "</div>"});
  }
  return out;
}

// §6.7. Counts, never a rating — the section opens by rejecting dot ratings
// because they "imply a magnitude no observed event supports".
std::string RenderExposure(const std::vector<ExposureEntity>& entities) {
  if (entities.empty()) {
    return base::StrCat({"<h2>",
                         L(IDS_ZEPHYRUS_PRIVACY_DASH_EXPOSURE_HEADING),
                         "</h2><div class=card><div>",
                         L(IDS_ZEPHYRUS_PRIVACY_DASH_EXPOSURE_EMPTY),
                         "</div></div>"});
  }
  std::string out = base::StrCat({
      "<h2>", L(IDS_ZEPHYRUS_PRIVACY_DASH_EXPOSURE_HEADING),
      "</h2><div class=card><table><tr><th>",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_COMPANY), "</th><th class=n>",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_REQUESTS), "</th><th class=n>",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_SITES), "</th><th class=n>",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_BLOCKED), "</th><th class=n>",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_ALLOWED), "</th></tr>"});
  for (const ExposureEntity& e :
       base::span(entities).first(std::min(entities.size(), kMaxExposureRows))) {
    // Allowed is the column that matters, so it is the one that gets colour —
    // and only when it is non-zero, so a clean row stays quiet.
    out += base::StrCat({
        "<tr><td>", Esc(e.entity_name),
        // §9.5 reported, not applied: the count stays whole, the row says what
        // it is. Without this the publisher of the page the user opened sits
        // at the top of "who tried to reach you".
        e.own_sites_only
            ? base::StrCat({" <span class=faint>",
                            L(IDS_ZEPHYRUS_PRIVACY_DASH_OWN_COMPANY),
                            "</span>"})
            : std::string(),
        "</td><td class=n>", Num(e.requests),
        "</td><td class=n>", Num(e.sites), "</td><td class=n>", Num(e.blocked),
        "</td><td class=n", e.allowed ? " style='color:#ffb454'>" : ">",
        Num(e.allowed), "</td></tr>"});
  }
  out += "</table><div class=faint style='margin-top:10px'>";
  out += L(IDS_ZEPHYRUS_PRIVACY_DASH_EXPOSURE_ORDER_NOTE);
  // Only explain the marker when one is actually on screen.
  if (std::ranges::any_of(entities, [](const ExposureEntity& e) {
        return e.own_sites_only;
      })) {
    out += base::StrCat({" ", L(IDS_ZEPHYRUS_PRIVACY_DASH_OWN_COMPANY_NOTE)});
  }
  // Say that the list was cut. Truncating in silence would make "who tried to
  // reach you" a smaller number than what was actually observed.
  if (entities.size() > kMaxExposureRows) {
    out += base::StrCat(
        {" ", LShownOfTotal(IDS_ZEPHYRUS_PRIVACY_DASH_EXPOSURE_TRUNCATED,
                            kMaxExposureRows, entities.size())});
  }
  out += "</div>";
  return out + "</div>";
}

// §2.4: every event line binds to an EventType, in one place. A switch with no
// default, so adding a value to the enum fails the build here rather than
// rendering as something it is not.
// Takes the STATUS as well as the type.
//
// It did not, and that was a §2 violation hiding in plain sight: once the §6.5
// heuristic started recording POTENTIAL, every uncertain match still rendered
// as "fingerprinting attempt detected on ..." — the strongest possible wording
// for the weakest possible evidence. §2.1 defines POTENTIAL as "heuristic
// match; not certain", so the line has to say that.
std::string EventTypeText(EventType type, TrackerStatus status) {
  switch (type) {
    case EventType::kFirstSeenOnSite:
      return L(IDS_ZEPHYRUS_PRIVACY_DASH_EVENT_FIRST_SEEN);
    case EventType::kCrossSiteDetected:
      return L(IDS_ZEPHYRUS_PRIVACY_DASH_EVENT_CROSS_SITE);
    case EventType::kFingerprintAttempt:
      if (status == TrackerStatus::kPotential) {
        return L(IDS_ZEPHYRUS_PRIVACY_DASH_EVENT_FINGERPRINT_POTENTIAL);
      }
      // kRandomized is recorded ONLY when the browser actually perturbed the
      // values (see RecordFingerprintSurface), so this is the one place the
      // stronger claim is earned. With Phase 4 off the status never takes that
      // value and every row still reads "detected" — which is why this reads
      // on the status rather than on a feature flag.
      if (status == TrackerStatus::kRandomized) {
        return L(IDS_ZEPHYRUS_PRIVACY_DASH_EVENT_FINGERPRINT_RANDOMIZED);
      }
      return L(IDS_ZEPHYRUS_PRIVACY_DASH_EVENT_FINGERPRINT);
    case EventType::kUserAllowedSite:
      return L(IDS_ZEPHYRUS_PRIVACY_DASH_EVENT_USER_ALLOWED);
    case EventType::kWebrtcAddressRequest:
      return L(IDS_ZEPHYRUS_PRIVACY_DASH_EVENT_WEBRTC);
  }
}

// §6.8: "sparse, notable events only". The list is capped for display as well
// as in storage — a page that renders 5,000 rows is not a timeline, it is a
// log, and §8.9 asks for pagination rather than everything at once.
std::string RenderTimeline(
    const std::vector<PrivacyIntelligenceService::TimelineItem>& items) {
  if (items.empty()) {
    return base::StrCat({"<h2>",
                         L(IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_HEADING),
                         "</h2><div class=card><div>",
                         L(IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_EMPTY),
                         "</div><div class=faint style='margin-top:6px'>",
                         L(IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_EMPTY_NOTE),
                         "</div></div>"});
  }
  std::string out =
      base::StrCat({"<h2>", L(IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_HEADING),
                    "</h2><div class=card><table>"});
  for (const auto& it : items) {
    // Local time: this is a human reading their own day back, and UTC would be
    // actively confusing. The stored value stays UTC (§9.6).
    //
    // Formatted through ICU rather than printf: "%02d:%02d" is a 24-hour clock
    // with Western digits, which is wrong for every locale that writes 9:05 PM
    // or uses its own numerals. The column is sized in ch so it survives the
    // longer forms.
    out += base::StrCat({
        "<tr><td class=faint style='width:9ch'>",
        base::UTF16ToUTF8(base::TimeFormatTimeOfDay(it.when)),
        "</td><td>", EventTypeText(it.event_type, it.status), " <b>",
        Esc(it.site_etld1), "</b>",
        it.entity_name.empty()
            ? std::string()
            : base::StrCat({" <span class=faint>(", Esc(it.entity_name),
                            ")</span>"}),
        "</td></tr>"});
  }
  out += "</table><div class=faint style='margin-top:10px'>";
  out += L(IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_NOTE);
  out += "</div>";
  return out + "</div>";
}

std::string RenderPage(
    const std::optional<DatabaseStats>& stats,
    const std::vector<CrossSiteEntity>& cross_site,
    const std::vector<ExposureEntity>& exposure,
    const std::vector<PrivacyIntelligenceService::TimelineItem>& timeline) {
  // dir and lang on the root element: without them a right-to-left UI renders
  // this page left-to-right, and the CSS logical properties above have nothing
  // to resolve against. This is the part of localisation that string lookup
  // alone does not buy.
  const std::string title = L(IDS_ZEPHYRUS_PRIVACY_DASH_TITLE);
  std::string body = base::StrCat({
      "<!doctype html><html dir=", base::i18n::IsRTL() ? "rtl" : "ltr",
      " lang=", base::i18n::GetConfiguredLocale(),
      "><head><meta charset=utf-8>",
      "<title>", title, "</title>", kStyle, "</head><body><main>",
      "<h1>", title, "</h1>"});

  if (!stats) {
    // §5.3 makes running without storage a supported state, and §10 requires
    // the degraded case to be VISIBLE — an empty page that is actually "we
    // deliberately stored nothing" must not read as "nothing happened".
    body += base::StrCat({"<p class=sub>",
                          L(IDS_ZEPHYRUS_PRIVACY_DASH_NO_STORAGE_TITLE),
                          "</p><div class=card>",
                          L(IDS_ZEPHYRUS_PRIVACY_DASH_NO_STORAGE_BODY),
                          "</div>"});
    return body + "</main></body></html>";
  }

  body += base::StrCat(
      {"<p class=sub>", L(IDS_ZEPHYRUS_PRIVACY_DASH_SUBTITLE), "</p>"});
  body += base::StrCat({
      "<div class=card><div class=big>", Num(stats->lifetime_blocked),
      "</div><div class=muted>", L(IDS_ZEPHYRUS_PRIVACY_DASH_STAT_BLOCKED),
      "</div></div>",
      // "Seen" is DERIVED here, not read straight from the column.
      //
      // §2.1's statuses are mutually exclusive per event -- DETECTED is
      // "request made [...] no action taken" -- so a BLOCKED request is never
      // stored as DETECTED, and `lifetime_detected` (itself detected+allowed)
      // genuinely excludes them. Printing that column under the sentence below
      // produced "9 blocked / 0 seen" on a profile where nine requests were
      // blocked and nothing else happened: a headline flatly contradicted by
      // the line under it.
      //
      // Summing at DISPLAY time rather than widening the stored column keeps
      // the persisted semantics matching §2.1 and needs no migration of
      // existing lifetime rows.
      "<div class=card><div class=big>",
      Num(stats->lifetime_detected + stats->lifetime_blocked),
      "</div><div class=muted>", L(IDS_ZEPHYRUS_PRIVACY_DASH_STAT_SEEN),
      "</div><div class=faint style='margin-top:6px'>",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_STAT_SEEN_NOTE), "</div></div>"});

  body += RenderExposure(exposure);
  body += RenderCrossSite(cross_site);
  body += RenderTimeline(timeline);
  return body + "</main></body></html>";
}

void Respond(content::WebUIDataSource::GotDataCallback callback,
             const std::string& html) {
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::string(html)));
}

// Two asynchronous reads before anything can be drawn: the database for the
// headline counters, then the cross-site query which itself needs the entity
// dataset. Chained rather than parallel because the page is one document.
void HandleRequest(base::WeakPtr<Profile> profile,
                   const std::string& path,
                   content::WebUIDataSource::GotDataCallback callback) {
  if (!profile) {
    Respond(std::move(callback), RenderPage(std::nullopt, {}, {}, {}));
    return;
  }
  PrivacyIntelligenceService* service =
      PrivacyIntelligenceServiceFactory::GetForBrowserContext(profile.get());
  if (!service) {
    Respond(std::move(callback), RenderPage(std::nullopt, {}, {}, {}));
    return;
  }

  service->GetDatabaseStats(base::BindOnce(
      [](base::WeakPtr<Profile> profile,
         content::WebUIDataSource::GotDataCallback cb,
         std::optional<DatabaseStats> stats) {
        PrivacyIntelligenceService* svc =
            profile ? PrivacyIntelligenceServiceFactory::GetForBrowserContext(
                          profile.get())
                    : nullptr;
        if (!svc) {
          Respond(std::move(cb), RenderPage(stats, {}, {}, {}));
          return;
        }
        // Third hop: cross-site, then the timeline. Chained because the page is
        // one document and cannot be sent twice.
        svc->GetCrossSiteEntities(base::BindOnce(
            [](base::WeakPtr<Profile> profile2,
               content::WebUIDataSource::GotDataCallback cb2,
               std::optional<DatabaseStats> stats2,
               std::vector<CrossSiteEntity> entities) {
              PrivacyIntelligenceService* svc2 =
                  profile2
                      ? PrivacyIntelligenceServiceFactory::GetForBrowserContext(
                            profile2.get())
                      : nullptr;
              if (!svc2) {
                Respond(std::move(cb2), RenderPage(stats2, entities, {}, {}));
                return;
              }
              svc2->GetExposureToday(base::BindOnce(
                  [](base::WeakPtr<Profile> profile3,
                     content::WebUIDataSource::GotDataCallback cb3,
                     std::optional<DatabaseStats> stats3,
                     std::vector<CrossSiteEntity> entities3,
                     std::vector<ExposureEntity> exposure) {
                    PrivacyIntelligenceService* svc3 =
                        profile3 ? PrivacyIntelligenceServiceFactory::
                                       GetForBrowserContext(profile3.get())
                                 : nullptr;
                    if (!svc3) {
                      Respond(std::move(cb3),
                              RenderPage(stats3, entities3, exposure, {}));
                      return;
                    }
                    svc3->GetTimeline(
                        kTimelinePageSize,
                        base::BindOnce(
                            [](content::WebUIDataSource::GotDataCallback cb4,
                               std::optional<DatabaseStats> stats4,
                               std::vector<CrossSiteEntity> entities4,
                               std::vector<ExposureEntity> exposure4,
                               std::vector<
                                   PrivacyIntelligenceService::TimelineItem>
                                   timeline) {
                              Respond(std::move(cb4),
                                      RenderPage(stats4, entities4, exposure4,
                                                 timeline));
                            },
                            std::move(cb3), stats3, std::move(entities3),
                            std::move(exposure)));
                  },
                  profile2, std::move(cb2), stats2, std::move(entities)));
            },
            profile, std::move(cb), stats));
      },
      profile, std::move(callback)));
}

}  // namespace

PrivacyDashboardUIConfig::PrivacyDashboardUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         chrome::kChromeUIZephyrusPrivacyHost) {}

bool PrivacyDashboardUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  // BOTH flags: the dashboard names sites and companies, so it must not open
  // when collection is off — it could only ever show an empty page, and §5.3
  // is about a degraded state being legible, not about offering a surface with
  // nothing behind it. §13.2's own flag then allows turning just this off while
  // collection keeps running.
  return base::FeatureList::IsEnabled(kZephyrusPrivacyDashboard) &&
         IsCollectionEnabled();
}

PrivacyDashboardUI::PrivacyDashboardUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, chrome::kChromeUIZephyrusPrivacyHost);
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(&HandleRequest, profile->GetWeakPtr()));
}

PrivacyDashboardUI::~PrivacyDashboardUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(PrivacyDashboardUI)

}  // namespace zephyrus_privacy

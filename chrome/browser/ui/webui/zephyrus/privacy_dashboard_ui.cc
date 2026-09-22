// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/zephyrus/privacy_dashboard_ui.h"

#include "base/feature_list.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/message_formatter.h"
#include "base/i18n/number_formatting.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/third_party/icu/icu_utf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "chrome/browser/zephyrus/privacy/privacy_cross_site.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/url_data_source.h"
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

// Locale-aware grouping for the numbers a person reads ("2,418", "२,४१८").
// Saturates rather than wraps: FormatNumber takes a signed value.
std::string Fmt(uint64_t n) {
  return base::UTF16ToUTF8(base::FormatNumber(static_cast<int64_t>(
      std::min<uint64_t>(n, std::numeric_limits<int64_t>::max()))));
}

// The avatar letter for a company: its first character, upper-cased.
//
// First CHARACTER, not first byte -- a UTF-8 name that starts outside ASCII
// would otherwise be cut mid-sequence and render as a replacement glyph. A
// surrogate pair is kept whole for the same reason. Escaped like every other
// runtime string on this page.
std::string Monogram(const std::string& name) {
  std::u16string wide;
  base::TrimWhitespace(base::UTF8ToUTF16(name), base::TRIM_LEADING, &wide);
  if (wide.empty()) {
    return "?";
  }
  const size_t len = (CBU16_IS_LEAD(wide[0]) && wide.size() > 1) ? 2 : 1;
  return Esc(base::UTF16ToUTF8(base::i18n::ToUpper(wide.substr(0, len))));
}

// M3 Expressive, from the browser's own ColorProvider.
//
// Colours come from chrome://theme/colors.css?sets=zephyrus -- the same M3
// roles the native UI paints with -- aliased to short names below so the rules
// stay readable. Type is M3's scale: displayLarge for the hero count,
// displaySmall for the page title, titleLarge for sections, bodyLarge/Medium,
// labelSmall.
//
// Layout is M3 Expressive's: a bento of tonal tiles for the headline numbers,
// and SEGMENTED lists (items 2dp apart, 4dp inner corners, 24dp at the ends of
// the group) in place of tables. Nothing is separated by a line any more.
//
// Colour carries meaning and only three meanings: `error` is "something got
// through" (§6.4: a leak is said prominently), `tertiary` is "nothing got
// through", everything else is neutral. The brand colour is the hero's, and
// the bars'.
constexpr char kStyle[] =
    "<meta name=color-scheme content='light dark'>"
    "<meta name=viewport content='width=device-width'>"
    "<link rel=stylesheet href='chrome://theme/colors.css?sets=zephyrus'>"
    "<style>"
    ":root{--s:var(--color-zephyrus-surface);"
    "--c:var(--color-zephyrus-surface-container);"
    "--cl:var(--color-zephyrus-surface-container-low);"
    "--chh:var(--color-zephyrus-surface-container-highest);"
    "--on:var(--color-zephyrus-on-surface);"
    "--onv:var(--color-zephyrus-on-surface-variant);"
    "--p:var(--color-zephyrus-primary);--onp:var(--color-zephyrus-on-primary);"
    "--pc:var(--color-zephyrus-primary-container);"
    "--onpc:var(--color-zephyrus-on-primary-container);"
    "--sc:var(--color-zephyrus-secondary-container);"
    "--onsc:var(--color-zephyrus-on-secondary-container);"
    "--tc:var(--color-zephyrus-tertiary-container);"
    "--ontc:var(--color-zephyrus-on-tertiary-container);"
    "--t:var(--color-zephyrus-tertiary);--e:var(--color-zephyrus-error);"
    "--ec:var(--color-zephyrus-error-container);"
    "--onec:var(--color-zephyrus-on-error-container);"
    "--ov:var(--color-zephyrus-outline-variant)}"
    "body{background:var(--s);color:var(--on);"
    "font:14px/20px system-ui,-apple-system,Segoe UI,sans-serif;"
    "letter-spacing:.25px;margin:0;padding:48px 24px 64px}"
    "main{max-width:840px;margin:0 auto}"
    "b{font-weight:500}"
    "h1{font-size:36px;line-height:44px;font-weight:400;letter-spacing:0;"
    "margin:0}"
    ".sub{color:var(--onv);font-size:16px;line-height:24px;margin:8px 0 32px}"
    "h2{font-size:22px;line-height:28px;font-weight:400;letter-spacing:0;"
    "margin:40px 0 16px}"
    // Bento.
    ".bento{display:grid;grid-template-columns:minmax(0,1.6fr) minmax(0,1fr);"
    "gap:8px}"
    ".hero{grid-row:span 2;background:var(--pc);color:var(--onpc);"
    "border-radius:28px;padding:24px;display:flex;flex-direction:column;"
    "justify-content:space-between;gap:24px;min-height:200px}"
    ".badge{width:64px;height:64px;display:block}"
    ".badge .shape{fill:var(--p)}.badge .glyph{fill:var(--onp)}"
    ".hero .n{font-size:57px;line-height:64px;letter-spacing:-.25px}"
    ".hero .l{font-size:16px;line-height:24px;margin-top:4px}"
    ".hero .seen{font-size:14px;line-height:20px;margin-top:16px}"
    ".hero .fine{font-size:12px;line-height:16px;margin-top:2px}"
    ".tile{background:var(--c);border-radius:24px;padding:20px}"
    ".tile.s{background:var(--sc);color:var(--onsc)}"
    ".tile.t{background:var(--tc);color:var(--ontc)}"
    ".tile .n{font-size:36px;line-height:44px}"
    ".tile .l{margin-top:4px}"
    "@media (max-width:600px){.bento{grid-template-columns:minmax(0,1fr)}"
    ".hero{grid-row:auto}}"
    // Segmented list.
    ".list{display:flex;flex-direction:column;gap:2px}"
    ".item{background:var(--c);border-radius:4px;padding:16px 20px;"
    "display:flex;gap:16px;align-items:center}"
    ".item:first-child{border-start-start-radius:24px;"
    "border-start-end-radius:24px}"
    ".item:last-child{border-end-start-radius:24px;"
    "border-end-end-radius:24px}"
    ".av{width:40px;height:40px;border-radius:50%;background:var(--sc);"
    "color:var(--onsc);display:flex;align-items:center;"
    "justify-content:center;font-size:16px;font-weight:500;flex:none}"
    ".grow{flex:1;min-width:0}"
    ".name{font-size:16px;line-height:24px;overflow-wrap:anywhere}"
    ".count{font-size:22px;line-height:28px;font-variant-numeric:tabular-nums;"
    "flex:none}"
    // A request bar, not a rating (§6.7): its length is this company's
    // requests against the busiest one listed, split into what was blocked and
    // what got through. Two facts, drawn to scale.
    ".bar{display:flex;height:8px;border-radius:4px;background:var(--chh);"
    "margin-top:8px;overflow:hidden}"
    ".bar>i{display:block;height:100%}"
    ".bar>.b{background:var(--p)}.bar>.a{background:var(--e)}"
    ".meta{font-size:12px;line-height:16px;color:var(--onv);margin-top:8px;"
    "display:flex;gap:4px 16px;flex-wrap:wrap}"
    ".bad{color:var(--e)}"
    ".tag{font-size:11px;line-height:16px;font-weight:500;letter-spacing:.5px;"
    "border-radius:8px;padding:2px 8px;background:var(--chh);"
    "color:var(--onv);margin-inline-start:8px;white-space:nowrap}"
    ".note{font-size:12px;line-height:16px;color:var(--onv);"
    "margin:12px 20px 0}"
    ".empty{background:var(--cl);border-radius:24px;padding:24px}"
    ".empty .name{margin-bottom:4px}"
    ".empty .fine{color:var(--onv)}"
    // Cross-site cards.
    ".card{background:var(--c);border-radius:24px;padding:20px;"
    "margin-bottom:8px}"
    ".card .head{display:flex;gap:16px;align-items:flex-start}"
    ".card .cap{color:var(--onv);margin-top:2px}"
    ".chips{display:flex;flex-wrap:wrap;gap:8px;margin-top:16px}"
    ".chip{display:inline-flex;align-items:center;gap:8px;min-height:32px;"
    "padding:0 12px;border-radius:8px;border:1px solid var(--ov);"
    "overflow-wrap:anywhere}"
    ".chip.own{border-style:dashed;color:var(--onv)}"
    ".chip .k{font-size:12px;color:var(--onv);"
    "font-variant-numeric:tabular-nums;white-space:nowrap}"
    ".chip .k.bad{color:var(--e)}"
    ".pill{display:inline-block;margin-top:16px;border-radius:8px;"
    "padding:6px 12px}"
    ".pill.bad{background:var(--ec);color:var(--onec)}"
    ".pill.ok{background:var(--tc);color:var(--ontc)}"
    // Timeline.
    ".tl{list-style:none;margin:0;padding:12px 0;background:var(--c);"
    "border-radius:24px}"
    ".tl li{display:grid;grid-template-columns:9ch 28px minmax(0,1fr);"
    "padding:6px 20px;position:relative}"
    ".tl time{color:var(--onv);font-size:12px;line-height:20px;"
    "font-variant-numeric:tabular-nums}"
    ".tl .dot{width:10px;height:10px;border-radius:50%;background:var(--p);"
    "margin:5px auto 0;position:relative;z-index:1}"
    ".tl .dot.warn{background:var(--e)}.tl .dot.ok{background:var(--t)}"
    ".tl .dot.user{background:var(--c);box-shadow:inset 0 0 0 2px var(--onv)}"
    // The rail joining the dots: from this dot to the next, so the last
    // entry does not trail a line into nothing.
    ".tl li:not(:last-child)::after{content:'';position:absolute;"
    "inset-inline-start:calc(20px + 9ch + 13px);top:21px;bottom:-11px;"
    "width:2px;background:var(--ov)}"
    ".tl .fine{color:var(--onv)}"
    "</style>";

// The hero badge: M3 Expressive's "cookie" shape (a nine-lobed scallop) with a
// shield check on it. Static markup, no runtime data.
constexpr char kBadge[] =
    "<svg class=badge viewBox='0 0 100 100' aria-hidden=true>"
    "<path class=shape d='"
    "M50.0 0.5L52.8 1.2L55.5 3.1L57.8 5.7L59.9 8.4L61.8 10.6L63.9 11.9L66.3 "
    "12.3L69.2 11.8L72.5 11.0L76.0 10.5L79.2 10.8L81.8 12.1L83.6 14.4L84.4 "
    "17.6L84.5 21.1L84.3 24.5L84.3 27.4L85.1 29.7L86.7 31.6L89.3 33.1L92.3 "
    "34.6L95.3 36.4L97.6 38.7L98.7 41.4L98.6 44.3L97.2 47.3L95.0 50.0L92.7 "
    "52.5L90.8 54.8L89.9 57.0L90.0 59.5L91.0 62.3L92.3 65.4L93.4 68.7L93.7 "
    "71.9L92.9 74.7L90.9 76.9L87.9 78.2L84.5 78.9L81.1 79.3L78.2 79.9L76.0 "
    "81.0L74.5 83.0L73.5 85.7L72.5 89.0L71.2 92.2L69.4 94.9L66.9 96.5L64.0 "
    "96.8L60.9 96.0L57.8 94.3L55.0 92.5L52.4 91.0L50.0 90.5L47.6 91.0L45.0 "
    "92.5L42.2 94.3L39.1 96.0L36.0 96.8L33.1 96.5L30.6 94.9L28.8 92.2L27.5 "
    "89.0L26.5 85.7L25.5 83.0L24.0 81.0L21.8 79.9L18.9 79.3L15.5 78.9L12.1 "
    "78.2L9.1 76.9L7.1 74.8L6.3 71.9L6.6 68.7L7.7 65.4L9.0 62.3L10.0 "
    "59.5L10.1 57.0L9.2 54.8L7.3 52.5L5.0 50.0L2.8 47.3L1.4 44.3L1.3 "
    "41.4L2.4 38.7L4.7 36.4L7.7 34.6L10.7 33.1L13.3 31.6L14.9 29.7L15.7 "
    "27.4L15.7 24.5L15.5 21.1L15.6 17.6L16.4 14.4L18.2 12.1L20.8 10.8L24.0 "
    "10.5L27.5 11.0L30.8 11.8L33.7 12.3L36.1 11.9L38.2 10.6L40.1 8.4L42.2 "
    "5.7L44.5 3.1L47.2 1.2Z"
    "'/><path class=glyph transform='translate(26 26)' d='M24 4 8 10v11c0 "
    "10.2 6.8 19.6 16 22 9.2-2.4 16-11.8 16-22V10L24 4zm-3.2 29.4-7.6-7.6 "
    "2.8-2.8 4.8 4.8 11.6-11.6 2.8 2.8-14.4 14.4z'/></svg>";

// A segmented-list group. Its items must be direct siblings with nothing else
// between them: the rounded ends come from :first-child and :last-child.
constexpr char kListOpen[] = "<div class=list>";
constexpr char kListClose[] = "</div>";

std::string EmptyCard(int headline_id, int note_id) {
  return base::StrCat({"<div class=empty><div class=name>", L(headline_id),
                       "</div>",
                       note_id ? base::StrCat({"<div class=fine>", L(note_id),
                                               "</div>"})
                               : std::string(),
                       "</div>"});
}

// §6.4. Names sites and companies, which is exactly why this page is separate
// from the diagnostics host.
std::string RenderCrossSite(const std::vector<CrossSiteEntity>& entities) {
  std::string out = base::StrCat(
      {"<h2>", L(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_HEADING), "</h2>"});
  if (entities.empty()) {
    // An empty list is the common and good case, and it must not read as a
    // failure to load. It also must not overclaim: this covers the retention
    // window only, and only companies the dataset can name.
    return out + EmptyCard(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_EMPTY,
                           IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_EMPTY_NOTE);
  }

  for (const CrossSiteEntity& e : base::span(entities).first(
           std::min(entities.size(), kMaxCrossSiteCards))) {
    // "could have been used" — §6.4 is explicit that the claim is capability,
    // not evidence of linkage. We observed the requests; we did not observe
    // what was done with them (§2.1).
    out += base::StrCat({
        "<div class=card><div class=head><div class=av>",
        Monogram(e.entity_name), "</div><div class=grow><div class=name>",
        LEntityCount(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_CLAIM,
                     base::StrCat({"<b>", Esc(e.entity_name), "</b>"}),
                     e.qualifying_sites),
        "</div><div class=cap>",
        L(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_CAPABILITY),
        "</div></div></div><div class=chips>"});

    // The sites are the EVIDENCE for the headline, one chip each, with what
    // was blocked and what got through on that site.
    for (const CrossSiteSite& s : base::span(e.sites).first(
             std::min(e.sites.size(), kMaxSitesPerCard))) {
      out += base::StrCat({
          "<span class='chip", s.owned_by_entity ? " own'>" : "'>",
          Esc(s.etld1),
          s.owned_by_entity
              ? base::StrCat({" <span class=k>",
                              L(IDS_ZEPHYRUS_PRIVACY_DASH_OWN_SITE),
                              "</span>"})
              : std::string(),
          "<span class=k>", L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_BLOCKED), " ",
          Fmt(s.blocked), "</span>",
          // Only when non-zero, so a clean chip stays quiet and a leak is the
          // thing that stands out.
          s.allowed ? base::StrCat({"<span class='k bad'>",
                                    L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_ALLOWED),
                                    " ", Fmt(s.allowed), "</span>"})
                    : std::string(),
          "</span>"});
    }
    out += "</div>";
    if (e.sites.size() > kMaxSitesPerCard) {
      out += base::StrCat({"<div class=note style='margin-inline:0'>",
                           LShownOfTotal(IDS_ZEPHYRUS_PRIVACY_DASH_SITES_TRUNCATED,
                                         kMaxSitesPerCard, e.sites.size()),
                           "</div>"});
    }

    // §6.4: "If anything was ALLOWED, say so prominently. Hiding a leak to
    // improve the number defeats the premise."
    out += e.anything_got_through()
               ? base::StrCat({"<div class='pill bad'>",
                               LCount(IDS_ZEPHYRUS_PRIVACY_DASH_LEAK, e.allowed),
                               "</div>"})
               : base::StrCat(
                     {"<div class='pill ok'>",
                      LCount(IDS_ZEPHYRUS_PRIVACY_DASH_ALL_BLOCKED, e.blocked),
                      "</div>"});
    out += "</div>";
  }
  if (entities.size() > kMaxCrossSiteCards) {
    out += base::StrCat(
        {"<div class=note>",
         LShownOfTotal(IDS_ZEPHYRUS_PRIVACY_DASH_CROSSSITE_TRUNCATED,
                       kMaxCrossSiteCards, entities.size()),
         "</div>"});
  }
  return out;
}

// §6.7. Counts, never a rating — the section opens by rejecting dot ratings
// because they "imply a magnitude no observed event supports".
std::string RenderExposure(const std::vector<ExposureEntity>& entities) {
  std::string out = base::StrCat(
      {"<h2>", L(IDS_ZEPHYRUS_PRIVACY_DASH_EXPOSURE_HEADING), "</h2>"});
  if (entities.empty()) {
    return out + EmptyCard(IDS_ZEPHYRUS_PRIVACY_DASH_EXPOSURE_EMPTY, 0);
  }
  const auto shown =
      base::span(entities).first(std::min(entities.size(), kMaxExposureRows));
  // The bars' scale: the busiest company ON SCREEN, so the longest bar is
  // always full width and every other is drawn against it.
  uint64_t max_requests = 1;
  for (const ExposureEntity& e : shown) {
    max_requests = std::max<uint64_t>(max_requests, e.requests);
  }

  out += kListOpen;
  for (const ExposureEntity& e : shown) {
    // Each segment as a share of the whole bar, so the two sit end to end at
    // the right lengths. requests is the denominator for both because blocked
    // + allowed need not add up to it (DETECTED is neither).
    auto pct = [&](uint64_t part) {
      return base::StringPrintf("%.2f%%", 100.0 * part / max_requests);
    };
    out += base::StrCat({
        "<div class=item><div class=av>", Monogram(e.entity_name),
        "</div><div class=grow><div class=name>", Esc(e.entity_name),
        // §9.5 reported, not applied: the count stays whole, the row says what
        // it is. Without this the publisher of the page the user opened sits
        // at the top of "who tried to reach you".
        e.own_sites_only
            ? base::StrCat({"<span class=tag>",
                            L(IDS_ZEPHYRUS_PRIVACY_DASH_OWN_COMPANY),
                            "</span>"})
            : std::string(),
        "</div><div class=bar><i class=b style='width:", pct(e.blocked),
        "'></i><i class=a style='width:", pct(e.allowed),
        "'></i></div><div class=meta><span>",
        L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_SITES), " ", Fmt(e.sites),
        "</span><span>", L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_BLOCKED), " ",
        Fmt(e.blocked), "</span>",
        // Allowed is the number that matters, so it is the one that gets
        // colour — and only when it is non-zero, so a clean row stays quiet.
        "<span", e.allowed ? " class=bad>" : ">",
        L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_ALLOWED), " ", Fmt(e.allowed),
        "</span></div></div><div class=count title='",
        Esc(L(IDS_ZEPHYRUS_PRIVACY_DASH_COL_REQUESTS)), "'>", Fmt(e.requests),
        "</div></div>"});
  }
  out += kListClose;

  out += base::StrCat({"<div class=note>",
                       L(IDS_ZEPHYRUS_PRIVACY_DASH_EXPOSURE_ORDER_NOTE)});
  // Only explain the marker when one is actually on screen.
  if (std::ranges::any_of(shown, [](const ExposureEntity& e) {
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

// The timeline dot. It follows the same three meanings as the rest of the
// page and no more: randomized fingerprinting is protection (tertiary), a
// detected or possible attempt is exposure (error), the user's own choice is
// an outline, and everything else is plain primary. Reads the STATUS for the
// same reason EventTypeText does.
const char* EventDotClass(EventType type, TrackerStatus status) {
  switch (type) {
    case EventType::kFingerprintAttempt:
      return status == TrackerStatus::kRandomized ? "dot ok" : "dot warn";
    case EventType::kUserAllowedSite:
      return "dot user";
    case EventType::kFirstSeenOnSite:
    case EventType::kCrossSiteDetected:
    case EventType::kWebrtcAddressRequest:
      return "dot";
  }
}

// §6.8: "sparse, notable events only". The list is capped for display as well
// as in storage — a page that renders 5,000 rows is not a timeline, it is a
// log, and §8.9 asks for pagination rather than everything at once.
std::string RenderTimeline(
    const std::vector<PrivacyIntelligenceService::TimelineItem>& items) {
  std::string out = base::StrCat(
      {"<h2>", L(IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_HEADING), "</h2>"});
  if (items.empty()) {
    return out + EmptyCard(IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_EMPTY,
                           IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_EMPTY_NOTE);
  }
  out += "<ol class=tl>";
  for (const auto& it : items) {
    // Local time: this is a human reading their own day back, and UTC would be
    // actively confusing. The stored value stays UTC (§9.6).
    //
    // Formatted through ICU rather than printf: "%02d:%02d" is a 24-hour clock
    // with Western digits, which is wrong for every locale that writes 9:05 PM
    // or uses its own numerals. The column is sized in ch so it survives the
    // longer forms.
    out += base::StrCat({
        "<li><time>", base::UTF16ToUTF8(base::TimeFormatTimeOfDay(it.when)),
        "</time><span class='", EventDotClass(it.event_type, it.status),
        "'></span><div>", EventTypeText(it.event_type, it.status), " <b>",
        Esc(it.site_etld1), "</b>",
        it.entity_name.empty()
            ? std::string()
            : base::StrCat({" <span class=fine>(", Esc(it.entity_name),
                            ")</span>"}),
        "</div></li>"});
  }
  out += "</ol>";
  return out + base::StrCat({"<div class=note>",
                             L(IDS_ZEPHYRUS_PRIVACY_DASH_TIMELINE_NOTE),
                             "</div>"});
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
                          "</p><div class=empty><div class=name>",
                          L(IDS_ZEPHYRUS_PRIVACY_DASH_NO_STORAGE_BODY),
                          "</div></div>"});
    return body + "</main></body></html>";
  }

  body += base::StrCat(
      {"<p class=sub>", L(IDS_ZEPHYRUS_PRIVACY_DASH_SUBTITLE), "</p>"});

  // The bento. The hero is what was stopped; the two tiles beside it are who
  // was there today and who was seen following across sites. The cross-site
  // tile is tertiary only when it is zero -- a company seen across sites is
  // not good news, so it must not wear the "all clear" colour.
  body += base::StrCat({
      "<div class=bento><div class=hero>", kBadge, "<div><div class=n>",
      Fmt(stats->lifetime_blocked), "</div><div class=l>",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_STAT_BLOCKED),
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
      "</div><div class=seen><b>",
      Fmt(stats->lifetime_detected + stats->lifetime_blocked), "</b> ",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_STAT_SEEN), "</div><div class=fine>",
      L(IDS_ZEPHYRUS_PRIVACY_DASH_STAT_SEEN_NOTE), "</div></div></div>",
      "<div class=tile><div class=n>", Fmt(exposure.size()),
      "</div><div class=l>",
      LCount(IDS_ZEPHYRUS_PRIVACY_DASH_TILE_COMPANIES, exposure.size()),
      "</div></div><div class='tile ", cross_site.empty() ? "t" : "s",
      "'><div class=n>", Fmt(cross_site.size()), "</div><div class=l>",
      LCount(IDS_ZEPHYRUS_PRIVACY_DASH_TILE_CROSSSITE, cross_site.size()),
      "</div></div></div>"});

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
  // colors.css lives on chrome://theme, which is registered per profile by
  // whichever page asks first. This page may BE the first, so ask.
  content::URLDataSource::Add(profile, std::make_unique<ThemeSource>(profile));
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(&HandleRequest, profile->GetWeakPtr()));
}

PrivacyDashboardUI::~PrivacyDashboardUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(PrivacyDashboardUI)

}  // namespace zephyrus_privacy

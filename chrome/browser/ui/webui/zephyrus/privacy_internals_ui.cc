// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/zephyrus/privacy_internals_ui.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "base/feature_list.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/common/url_constants.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui_data_source.h"

namespace zephyrus_privacy {

namespace {

// The browser's own M3 roles, from chrome://theme/colors.css?sets=zephyrus,
// so the page looks like part of the browser rather than a stray debug dump.
//
// Same grammar as chrome://privacy: a row of tonal tiles for the numbers that
// say whether the pipeline is healthy, then SEGMENTED lists (2dp apart, 4dp
// inner corners, 24dp at the group's ends) instead of ruled tables, and every
// state as a tonal pill -- tertiary for working, error for a state §5.3/§4.4.1
// require to be visible, neutral for off.
constexpr char kStyle[] =
    ":root{--s:var(--color-zephyrus-surface);"
    "--c:var(--color-zephyrus-surface-container);"
    "--chh:var(--color-zephyrus-surface-container-highest);"
    "--on:var(--color-zephyrus-on-surface);"
    "--onv:var(--color-zephyrus-on-surface-variant);"
    "--p:var(--color-zephyrus-primary);"
    "--tc:var(--color-zephyrus-tertiary-container);"
    "--ontc:var(--color-zephyrus-on-tertiary-container);"
    "--ec:var(--color-zephyrus-error-container);"
    "--onec:var(--color-zephyrus-on-error-container)}"
    "body{background:var(--s);color:var(--on);"
    "font:14px/20px system-ui,sans-serif;letter-spacing:.25px;margin:0;"
    "padding:48px 24px 64px}"
    "main{max-width:720px;margin:0 auto}"
    "h1{font-size:36px;line-height:44px;font-weight:400;margin:0}"
    "p.sub{color:var(--onv);font-size:16px;line-height:24px;margin:8px 0 32px}"
    // M3 list subheader: titleSmall in primary.
    "h2{font-size:14px;line-height:20px;font-weight:500;letter-spacing:.1px;"
    "color:var(--p);margin:32px 20px 12px}"
    ".tiles{display:grid;grid-template-columns:repeat(auto-fit,"
    "minmax(160px,1fr));gap:8px}"
    ".tile{background:var(--c);border-radius:24px;padding:20px}"
    ".tile.warn{background:var(--ec);color:var(--onec)}"
    ".tile .n{font-size:36px;line-height:44px;"
    "font-variant-numeric:tabular-nums}"
    ".tile .l{margin-top:4px}"
    ".list{display:flex;flex-direction:column;gap:2px}"
    ".item{background:var(--c);border-radius:4px;padding:14px 20px;"
    "display:flex;gap:16px;align-items:center;justify-content:space-between}"
    ".item:first-child{border-start-start-radius:24px;"
    "border-start-end-radius:24px}"
    ".item:last-child{border-end-start-radius:24px;"
    "border-end-end-radius:24px}"
    ".k{color:var(--onv)}"
    ".v{text-align:end;font-variant-numeric:tabular-nums;font-weight:500;"
    "overflow-wrap:anywhere;min-width:0}"
    ".ok,.warn,.off{display:inline-block;border-radius:8px;padding:2px 10px;"
    "font-weight:500}"
    ".ok{background:var(--tc);color:var(--ontc)}"
    ".warn{background:var(--ec);color:var(--onec)}"
    ".off{background:var(--chh);color:var(--onv)}";

std::string Row(std::string_view key, const std::string& value) {
  return base::StrCat(
      {"<div class=item><span class=k>", key, "</span><span class=v>", value,
       "</span></div>"});
}

// A headline counter. `warn` only for the one number whose being non-zero is
// itself the problem, so the tile row reads at a glance.
std::string Tile(std::string_view label, uint64_t n, bool warn = false) {
  return base::StrCat({"<div class='tile", warn ? " warn" : "",
                       "'><div class=n>", base::NumberToString(n),
                       "</div><div class=l>", label, "</div></div>"});
}

std::string Num(uint64_t n) {
  return base::NumberToString(n);
}

std::string ModeName(Mode mode) {
  switch (mode) {
    case Mode::kDisabled:
      return "<span class=off>Disabled</span>";
    case Mode::kCollectOnly:
      return "<span class=ok>Collect only &mdash; no UI surfaces</span>";
    case Mode::kEnabled:
      return "<span class=ok>Enabled</span>";
  }
  return "Unknown";
}

std::string DatasetName(DatasetId id) {
  switch (id) {
    case DatasetId::kUnknown:
      return "none";
    case DatasetId::kTrackerRadar:
      // §4.2: the licence requires attribution wherever the data is used, and
      // this page is one of the places it is used.
      return "DuckDuckGo Tracker Radar (CC BY-NC-SA 4.0)";
    case DatasetId::kDisconnect:
      return "Disconnect entity list";
    case DatasetId::kGhosteryTrackerDb:
      return "Ghostery TrackerDB (CC BY-NC-SA 4.0)";
  }
  return "unrecognised";
}

// §4.4.1 requires internals to ALWAYS show the dataset date and computed age,
// because a stale dataset silently turns "no trackers detected" into a false
// claim, and the only defence while there is no updater is that the age is
// visible.
std::string FreshnessLine(const PipelineStats& stats) {
  switch (stats.dataset_freshness) {
    case DatasetFreshness::kAbsent:
      return "<span class=off>n/a</span>";
    case DatasetFreshness::kFresh:
      return base::StrCat({"<span class=ok>",
                           base::NumberToString(stats.dataset_age_days),
                           " days &mdash; fresh</span>"});
    case DatasetFreshness::kAging:
      return base::StrCat({"<span class=warn>",
                           base::NumberToString(stats.dataset_age_days),
                           " days &mdash; aging; unqualified coverage "
                           "language suppressed</span>"});
    case DatasetFreshness::kStale:
      return base::StrCat(
          {"<span class=warn>",
           stats.dataset_age_days < 0
               ? std::string("undated")
               : base::StrCat({base::NumberToString(stats.dataset_age_days),
                               " days"}),
           " &mdash; STALE; \"no trackers detected\" must be qualified"
           "</span>"});
  }
  return "unknown";
}

std::string Escape(std::string_view raw) {
  // Artifact metadata is attacker-controllable in the sense that the file came
  // from disk. It is signature-verified before we get here, but escaping costs
  // nothing and keeps one bad string from being able to inject markup.
  std::string out;
  for (const char c : raw) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string DatasetLine(const PipelineStats& stats) {
  if (stats.has_dataset) {
    return "<span class=ok>" + DatasetName(stats.dataset_id) + "</span>";
  }
  // Deliberately not "error": §4.1 makes no-dataset a supported state, and the
  // feature is fully functional here — it just shows bare domains instead of
  // company names. Saying "attribution unavailable" rather than "failed" is the
  // honest description of what the user loses.
  return "<span class=off>No dataset &mdash; domains shown without company "
         "attribution</span>";
}

std::string PersistenceLine(const PipelineStats& stats) {
  if (stats.persistence_ready) {
    return "<span class=ok>Writing to disk</span>";
  }
  if (stats.persistence_refused) {
    // §5.3 requires this state to be VISIBLE. An empty history that is really a
    // deliberate refusal must not read as a bug.
    return
        "<span class=warn>Refused &mdash; no keystore; in-memory only</span>";
  }
  return "<span class=off>Waiting for the keystore&hellip;</span>";
}

std::string BuildPage(const PipelineStats& stats,
                      const std::optional<DatabaseStats>& db) {
  std::string out = base::StrCat({
      "<!doctype html><meta charset=utf-8>"
      "<meta http-equiv=refresh content=2>"
      "<meta name=color-scheme content='light dark'>"
      "<title>Privacy Intelligence internals</title>"
      "<link rel=stylesheet href='chrome://theme/colors.css?sets=zephyrus'>"
      "<style>",
      kStyle,
      "</style><main><h1>Privacy Intelligence</h1>"
      "<p class=sub>Pipeline diagnostics. Counters only &mdash; this page "
      "never shows a site, domain or entity.</p><div class=tiles>",
      Tile("Events recorded", stats.events_recorded),
      Tile("Rows handed to the database", stats.rows_flushed),
      // A dropped event is data the dashboard will never show; the one
      // counter here whose being non-zero is the finding.
      Tile("Events dropped", stats.events_dropped, stats.events_dropped > 0),
      "</div><h2>Collection</h2><div class=list>",
      Row("Mode", ModeName(stats.mode)),
      Row("Persistence", PersistenceLine(stats)),
      Row("Entity dataset", DatasetLine(stats)),
      Row("Dataset entries", Num(stats.dataset_entries)),
      Row("Dataset version", Escape(stats.dataset_version)),
      Row("Dataset source", Escape(stats.dataset_source)),
      Row("Dataset licence", Escape(stats.dataset_licence)),
      Row("Dataset age", FreshnessLine(stats)),
      "</div><h2>Event pipeline</h2><div class=list>",
      Row("Events recorded", Num(stats.events_recorded)),
      Row("Events drained", Num(stats.events_drained)),
      Row("Events dropped (ring full)", Num(stats.events_dropped)),
      Row("Ring depth now", Num(stats.current_ring_depth)),
      Row("Ring depth peak", Num(stats.peak_ring_depth)),
      Row("Names in the string channel", Num(stats.known_domain_strings)),
      Row("Rows handed to the database", Num(stats.rows_flushed)),
      "</div><h2>Stored</h2><div class=list>",
  });

  if (db) {
    out += Row("Daily rows", Num(db->daily_rows));
    out += Row("Timeline rows", Num(db->timeline_rows));
    out += Row("Sites", Num(db->site_rows));
    out += Row("File size", base::StrCat({Num(db->file_size_bytes / 1024),
                                          " KB"}));
    out += Row("Lifetime detected", Num(db->lifetime_detected));
    out += Row("Lifetime blocked", Num(db->lifetime_blocked));
    out += Row("Lifetime randomized", Num(db->lifetime_randomized));
  } else {
    out += Row("Database", "<span class=off>Not open</span>");
  }
  out += "</div></main>";
  return out;
}

void Respond(content::WebUIDataSource::GotDataCallback callback,
             PipelineStats stats,
             std::optional<DatabaseStats> db) {
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(BuildPage(stats, db)));
}

void HandleRequest(base::WeakPtr<Profile> profile,
                   const std::string& path,
                   content::WebUIDataSource::GotDataCallback callback) {
  auto* service =
      profile ? PrivacyIntelligenceServiceFactory::GetForBrowserContext(
                    profile.get())
              : nullptr;
  if (!service) {
    // The feature is off, or this is an off-the-record profile where the
    // factory deliberately builds nothing (§5.2).
    PipelineStats empty;
    Respond(std::move(callback), empty, std::nullopt);
    return;
  }
  service->GetFullStats(base::BindOnce(
      [](content::WebUIDataSource::GotDataCallback cb, PipelineStats stats,
         std::optional<DatabaseStats> db) {
        Respond(std::move(cb), stats, std::move(db));
      },
      std::move(callback)));
}

}  // namespace

PrivacyInternalsUIConfig::PrivacyInternalsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         chrome::kChromeUIZephyrusPrivacyInternalsHost) {}

bool PrivacyInternalsUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  // Gated on its OWN flag, not on collection being enabled: the whole point of
  // a diagnostic surface is to be reachable when the pipeline is off or broken,
  // which is exactly when someone needs to see "Mode: Disabled" rather than a
  // page that will not open.
  return base::FeatureList::IsEnabled(kZephyrusPrivacyInternals);
}

PrivacyInternalsUI::PrivacyInternalsUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      profile, chrome::kChromeUIZephyrusPrivacyInternalsHost);
  // colors.css lives on chrome://theme, which is registered per profile by
  // whichever page asks first. This page may BE the first, so ask.
  content::URLDataSource::Add(profile, std::make_unique<ThemeSource>(profile));
  source->SetRequestFilter(
      base::BindRepeating(
          [](const std::string& path) { return true; }),
      base::BindRepeating(&HandleRequest, profile->GetWeakPtr()));
}

PrivacyInternalsUI::~PrivacyInternalsUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(PrivacyInternalsUI)

}  // namespace zephyrus_privacy

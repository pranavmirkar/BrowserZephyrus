// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include <optional>
#include "base/logging.h"

#include <utility>

#include "base/base_paths.h"
#include "base/no_destructor.h"
#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/path_service.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_procedural_script.h"
#include "chrome/browser/zephyrus/adblock/adblock_redirect_resources.h"
#include "chrome/browser/zephyrus/adblock/adblock_shared_page_rules.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_updater.h"
#include "chrome/common/chrome_paths.h"
#include "base/strings/string_util.h"
#include <algorithm>

#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

namespace {

// Pref keys (registered as literals in browser_prefs.cc).
constexpr char kPrefEnabled[] = "zephyrus.adblock.enabled";
constexpr char kPrefAggressivePopup[] = "zephyrus.adblock.aggressive_popup";
constexpr char kPrefAllowlist[] = "zephyrus.adblock.allowlist";
constexpr char kPrefTotalBlocked[] = "zephyrus.adblock.total_blocked";
constexpr char kPrefRuleCount[] = "zephyrus.adblock.rule_count";

// How often the running blocked-count is flushed to prefs for the settings page.
constexpr base::TimeDelta kStatsFlushPeriod = base::Seconds(5);

// Normalizes a user-entered domain: lowercased, scheme/path/leading-dot/"www."
// stripped, so "https://www.Example.com/x" and "example.com" match the same.
std::string NormalizeDomain(const std::string& input) {
  std::string d = base::ToLowerASCII(input);
  size_t scheme = d.find("://");
  if (scheme != std::string::npos) {
    d = d.substr(scheme + 3);
  }
  size_t slash = d.find('/');
  if (slash != std::string::npos) {
    d = d.substr(0, slash);
  }
  while (!d.empty() && d.front() == '.') {
    d = d.substr(1);
  }
  if (d.rfind("www.", 0) == 0) {
    d = d.substr(4);
  }
  return d;
}

// How often the lists are refreshed. Every list once a day; uBO's quick-fixes
// on the 8-hour expiry its publisher declares, because that is where YouTube
// fixes land -- on the daily cycle alone they could arrive a day and a half
// late. The check itself only reads the file's header, so it runs hourly: a
// 6-hour check period added up to 6 hours to every one of those deadlines.
constexpr base::TimeDelta kUpdateInterval = base::Hours(24);
constexpr base::TimeDelta kQuickFixesInterval = base::Hours(8);
constexpr base::TimeDelta kUpdateCheckPeriod = base::Hours(1);
// Delay before the first check after startup, so it doesn't compete with launch.
constexpr base::TimeDelta kInitialUpdateDelay = base::Seconds(45);

// The longest the first requests of a session wait for the full filter list.
// Loading it takes a few hundred milliseconds; this is only the fail-open for
// a list that cannot load, after which protection is the starter list, as it
// was before the gate existed.
constexpr base::TimeDelta kStartupHoldTimeout = base::Seconds(2);

// Combined auto-updated list location (shared across profiles).
base::FilePath DownloadedListPath() {
  base::FilePath dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &dir)) {
    return base::FilePath();
  }
  return dir.AppendASCII("ZephyrusAdBlock").AppendASCII("filters.txt");
}

// The compiled index built from that list, cached so the next start has it in
// force in milliseconds instead of rebuilding it (shared across profiles).
base::FilePath IndexCachePath() {
  base::FilePath dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &dir)) {
    return base::FilePath();
  }
  return dir.AppendASCII("ZephyrusAdBlock").AppendASCII("filters.zidx");
}

// Which refresh, if any, the downloaded list is due. Reads only the file's
// header and modification time. Runs on a background thread.
std::optional<UpdateKind> DownloadedListDue() {
  const base::FilePath path = DownloadedListPath();
  base::File::Info info;
  if (path.empty() || !base::GetFileInfo(path, &info) || info.size == 0) {
    return UpdateKind::kFull;  // Never downloaded: fetch everything.
  }
  std::string head;
  // Fails for any file over the limit, but leaves the first bytes in `head`,
  // which is all the header needs.
  base::ReadFileToStringWithMaxSize(path, &head, 1024);
  const CombinedListHeader header = ParseCombinedListHeader(head);
  // Written by an older updater (no includes resolved, before this build):
  // refresh now rather than leave an upgrade waiting up to a day to apply.
  if (header.format != kCombinedListFormat) {
    return UpdateKind::kFull;
  }
  const base::Time now = base::Time::Now();
  const base::Time full = base::Time::FromTimeT(header.full_update_seconds);
  // A time in the future means the clock moved backwards since it was
  // written. Treated as due, or updates would stall until the clock caught up.
  const base::TimeDelta since_full = now - full;
  if (header.full_update_seconds == 0 || since_full.is_negative() ||
      since_full >= kUpdateInterval) {
    return UpdateKind::kFull;
  }
  const base::TimeDelta since_write = now - info.last_modified;
  if (since_write.is_negative() || since_write >= kQuickFixesInterval) {
    return UpdateKind::kQuickFixes;
  }
  return std::nullopt;
}

// Every live service, so a finished update reloads all of them. The download
// is shared by every profile -- including off-the-record ones, which never
// fetch -- and an incognito window used to keep the rules it opened with until
// the browser restarted.
base::RepeatingClosureList& ListUpdatedCallbacks() {
  static base::NoDestructor<base::RepeatingClosureList> callbacks;
  return *callbacks;
}

// Filter list bundled next to the binary (copied by the build). Contains
// EasyList + EasyPrivacy.
constexpr char kFilterFileName[] = "zephyrus_filters.txt";

// Starter filter list (EasyList/ABP syntax). A curated high-impact subset that
// blocks the most common ad/tracker endpoints out of the box. Phase 5 will
// replace this with the bundled full EasyList + EasyPrivacy.
constexpr char kStarterFilterList[] = R"FILTERS(
! Zephyrus starter ad/tracker list
||doubleclick.net^
||googlesyndication.com^
||googleadservices.com^
||google-analytics.com^
||googletagmanager.com^
||googletagservices.com^
||adservice.google.com^
||pagead2.googlesyndication.com^
||partner.googleadservices.com^
||analytics.google.com^
||stats.g.doubleclick.net^
||adnxs.com^
||adsystem.com^
||amazon-adsystem.com^
||scorecardresearch.com^
||quantserve.com^
||criteo.com^
||criteo.net^
||taboola.com^
||outbrain.com^
||moatads.com^
||rubiconproject.com^
||pubmatic.com^
||openx.net^
||casalemedia.com^
||advertising.com^
||adform.net^
||adcolony.com^
||applovin.com^
||inmobi.com^
||mopub.com^
||smartadserver.com^
||yieldmo.com^
||sharethrough.com^
||teads.tv^
||3lift.com^
||bidswitch.net^
||demdex.net^
||bluekai.com^
||adsrvr.org^
||serving-sys.com^
||facebook.com/tr^
||connect.facebook.net/*/fbevents.js
||analytics.tiktok.com^
||hotjar.com^
||mixpanel.com^
||segment.io^
||segment.com^
||fullstory.com^
||branch.io^
||chartbeat.com^
||newrelic.com/*beacon
||nr-data.net^
||bat.bing.com^
||clarity.ms^
||yandex.ru/*metrika
||mc.yandex.ru^
||sentry.io/api/*/envelope
||doubleverify.com^
||adsafeprotected.com^
/adsense/*
/pagead/*
&ad_type=
-advertising.
||ads.*^$third-party
)FILTERS";

// Starter SCRIPTLET list (uBO ##+js syntax). Parsed synchronously at service
// construction so the highest-value scriptlets (YouTube ad neutralizers) are
// ready before the first navigation — the bundled full list loads async and can
// race a fast YouTube page load, leaving that first watch page un-blocked. The
// async list later replaces the scriptlet engine with the complete set.
constexpr char kStarterScriptletList[] = R"RULES(
! Zephyrus starter scriptlet list (YouTube-critical)
m.youtube.com,music.youtube.com,tv.youtube.com,www.youtube.com,youtubekids.com,youtube-nocookie.com##+js(set, ytInitialPlayerResponse.playerAds, undefined)
m.youtube.com,music.youtube.com,tv.youtube.com,www.youtube.com,youtubekids.com,youtube-nocookie.com##+js(set, ytInitialPlayerResponse.adPlacements, undefined)
m.youtube.com,music.youtube.com,tv.youtube.com,www.youtube.com,youtubekids.com,youtube-nocookie.com##+js(set, ytInitialPlayerResponse.adSlots, undefined)
m.youtube.com,music.youtube.com,tv.youtube.com,www.youtube.com,youtubekids.com,youtube-nocookie.com##+js(set, playerResponse.adPlacements, undefined)
m.youtube.com,music.youtube.com,youtubekids.com,youtube-nocookie.com##+js(json-prune, playerResponse.adPlacements playerResponse.playerAds playerResponse.adSlots adPlacements playerAds adSlots important)
www.youtube.com##+js(json-prune-fetch-response, adPlacements adSlots playerResponse.adPlacements playerResponse.adSlots [].playerResponse.adPlacements [].playerResponse.adSlots, , propsToMatch, /player?)
www.youtube.com##+js(json-prune-xhr-response, adPlacements adSlots playerResponse.adPlacements playerResponse.adSlots [].playerResponse.adPlacements [].playerResponse.adSlots, , propsToMatch, /player)
www.youtube.com##+js(trusted-replace-fetch-response, '"adPlacements"', '"no_ads"', player?)
www.youtube.com##+js(trusted-replace-fetch-response, '"adSlots"', '"no_ads"', player?)
tv.youtube.com##+js(trusted-replace-xhr-response, '"adPlacements"', '"no_ads"', /playlist\?list=|\/player(?:\?.+)?$|watch\?[tv]=/)
www.youtube.com##+js(trusted-replace-xhr-response, /"adPlacements.*?("adSlots"|"adBreakHeartbeatParams")/gms, $1, /\/player(?:\?.+)?$/)
youtube.com##+js(json-prune, entries.[-].command.reelWatchEndpoint.adClientParams.isAd)
www.youtube.com##+js(trusted-replace-fetch-response, '"adSlots"', '"no_ads"', /get_watch?)
www.youtube.com##+js(json-prune-fetch-response, adPlacements adSlots playerResponse.adPlacements playerResponse.adSlots, , propsToMatch, /get_watch?)
www.youtube.com##+js(nano-stb, [native code], 17000, 0.001)
www.youtube.com##+js(trusted-prevent-dom-bypass, Node.prototype.appendChild, fetch)
www.youtube.com##+js(trusted-prevent-dom-bypass, Node.prototype.appendChild, Request)
www.youtube.com##+js(trusted-prevent-dom-bypass, Node.prototype.appendChild, JSON.parse)
)RULES";

// Starter COSMETIC list (uBO ## element-hiding syntax). Sync-parsed at ctor for
// the same reason as the scriptlet starter: cosmetic CSS is injected at
// DidFinishNavigation, and on a cold-start YouTube load the async full list
// isn't ready yet → empty injection → feed ads (the "Sponsored" home tiles)
// never hide (and SPA nav doesn't re-inject). These use native CSS :has() which
// Chromium renders, so whole ad tiles collapse. (Player pre-roll ads are NOT
// covered here — those are neutralized by the scriptlets, not element-hiding.)
constexpr char kStarterCosmeticList[] = R"RULES(
! Zephyrus starter cosmetic list (YouTube ad tiles / banners / overlays)
youtube.com##ytd-ad-slot-renderer
youtube.com##ad-slot-renderer
youtube.com##ytd-in-feed-ad-layout-renderer
youtube.com##ytd-rich-item-renderer:has(ytd-ad-slot-renderer)
youtube.com##ytd-rich-item-renderer:has(ytd-in-feed-ad-layout-renderer)
youtube.com##ytd-rich-section-renderer:has(ytd-ad-slot-renderer)
youtube.com##ytd-item-section-renderer:has(> #contents > ytd-ad-slot-renderer)
youtube.com##ytd-watch-next-secondary-results-renderer ytd-ad-slot-renderer
youtube.com###masthead-ad
youtube.com###player-ads
youtube.com##ytd-display-ad-renderer
youtube.com##ytd-promoted-sparkles-web-renderer
youtube.com##ytd-promoted-video-renderer
youtube.com##ytd-compact-promoted-video-renderer
youtube.com##ytd-companion-slot-renderer
youtube.com##ytd-action-companion-ad-renderer
youtube.com##ytd-statement-banner-renderer
youtube.com##ytd-banner-promo-renderer
youtube.com##ytd-brand-video-shelf-renderer
youtube.com##ytd-brand-video-singleton-renderer
youtube.com##.ytp-ad-module
youtube.com##.ytp-ad-overlay-slot
youtube.com##.ytp-ad-image-overlay
youtube.com##ytm-rich-item-renderer:has(ad-slot-renderer)
youtube.com##ytm-companion-slot
! --- Cookie/consent banners: the top consent-management platforms, so the
! --- calm-web behavior works out of the box, before the full EasyList Cookie
! --- List arrives via the first filter update. Generic (all-site) selectors.
###onetrust-consent-sdk
###onetrust-banner-sdk
###CybotCookiebotDialog
###CybotCookiebotDialogBodyUnderlay
###didomi-host
###didomi-popup
##.qc-cmp2-container
###qc-cmp2-container
##.fc-consent-root
###sp_message_container_general
! Sourcepoint numbers its container per property, so match the prefix. NOT
! `.sp-message-open`: that is the state class Sourcepoint puts on <html>, so
! hiding it blanks the entire document — it did exactly that on theguardian.com
! until 2026-08-11. The scroll-lock that class applies is released by the
! renderer's unlock pass, not by hiding anything.
##[id^="sp_message_container_"]
##.sp_veil
###usercentrics-root
###cookiescript_injected
##.cc-window.cc-banner
###cookie-law-info-bar
##.cky-consent-container
###truste-consent-track
##.truste_box_overlay
##.truste_overlay
###consent_blackbar
##.osano-cm-window
###hs-eu-cookie-confirmation
##.cmplz-cookiebanner
###cmplz-cookiebanner-container
###CookieConsent
##.iubenda-cs-container
###iubenda-cs-banner
###gdpr-cookie-message
##.js-consent-banner
)RULES";

// Builds the network + cosmetic engines on a background thread: the embedded
// starter rules plus the bundled EasyList + EasyPrivacy file (if present).
// With the compiled index on, the network rules come back as the shared index
// when this process or the disk cache already has one for this exact list, and
// the returned network engine is then null. Otherwise (a first run, or a list
// that just changed) the engine comes back as it always did, in the 60 ms it
// always took, so protection never waits on the half-second index build; the
// service builds the index afterwards and swaps it in.
ZephyrusAdblockService::BuiltEngines BuildEnginesFromBundledList() {
  auto network = std::make_unique<AdblockFilterEngine>();
  network->AddRules(kStarterFilterList);

  // Prefer the auto-updated list downloaded to the user-data dir; fall back to
  // the snapshot bundled next to the binary if it's missing (fresh install /
  // update never ran / offline).
  // Bounded + validated read. The updater checks content before WRITING, but
  // this is the READ path and it must not assume the file got there that way:
  // a copy written by a build that predates the validation, or by anything else
  // with write access to the profile directory, would otherwise be handed
  // straight to the rule engines. An unbounded ReadFileToString here also made
  // startup allocate whatever size that file happened to be.
  //
  // On rejection we fall through to the bundled snapshot, which is the same
  // "never fail open" shape the rest of the update path uses.
  static constexpr size_t kMaxFilterFileBytes = 64u * 1024 * 1024;
  auto read_filter_file = [](const base::FilePath& path, std::string* out) {
    const std::optional<int64_t> size = base::GetFileSize(path);
    if (!size.has_value() || *size <= 0 ||
        static_cast<uint64_t>(*size) > kMaxFilterFileBytes) {
      return false;
    }
    if (!base::ReadFileToStringWithMaxSize(path, out, kMaxFilterFileBytes)) {
      return false;
    }
    if (!LooksLikeFilterList(*out)) {
      LOG(ERROR) << "[Zephyrus] stored filter list is not a filter list, "
                    "ignoring it: "
                 << path;
      out->clear();
      return false;
    }
    return true;
  };

  std::string contents;
  bool read = false;
  scoped_refptr<const AdblockSharedIndex> shared_index;
  scoped_refptr<const AdblockRegexMatcher> regex;
  uint64_t list_key = 0;
  base::FilePath downloaded = DownloadedListPath();
  if (!downloaded.empty()) {
    read = read_filter_file(downloaded, &contents);
  }
  if (!read) {
    base::FilePath dir;
    if (base::PathService::Get(base::DIR_ASSETS, &dir)) {
      read = read_filter_file(dir.AppendASCII(kFilterFileName), &contents);
    }
  }
  if (read) {
    // Resolve uBO's !#if blocks first; see PreprocessFilterList.
    contents = PreprocessFilterList(contents);
    if (base::FeatureList::IsEnabled(kZephyrusAdblockCompiledIndex)) {
      list_key = AdblockSharedIndex::KeyFor(contents, kStarterFilterList);
      shared_index = AdblockSharedIndex::GetCached(list_key, IndexCachePath());
    }
    if (shared_index) {
      network.reset();  // the shared index replaces it
    } else {
      network->AddRules(contents);
    }
    // The regex rules are matched apart from both of the above, so they are
    // compiled whether or not the index came from the cache.
    regex = AdblockRegexMatcher::GetOrBuild(list_key, contents);
  }
  // Everything else the lists say, built once for the process: the second
  // profile to open takes the first one's copy instead of parsing the list
  // again and holding its own.
  const uint64_t page_key =
      AdblockSharedIndex::KeyFor(contents, kStarterFilterList);
  AdblockPageRules page = GetOrBuildSharedPageRules(
      page_key != 0 ? page_key : 1, [&contents, read] {
        auto cosmetic = std::make_unique<AdblockCosmeticEngine>();
        auto scriptlet = std::make_unique<AdblockScriptletEngine>();
        scriptlet->AddRules(kStarterScriptletList);
        cosmetic->AddRules(kStarterCosmeticList,
                           /*inject_generic_selectors=*/true);
        auto params = std::make_unique<AdblockParamFilter>();
        params->AddBuiltInRules();
        auto urlskip = std::make_unique<AdblockUrlSkipFilter>();
        auto csp = std::make_unique<AdblockCspFilter>();
        if (read) {
          cosmetic->AddRules(contents);
          // Per-section trust (Z-04): third-party lists in the combined file
          // may not use trusted-* scriptlets; only uBO's own sections may.
          scriptlet->AddRules(contents, /*trust_unsectioned=*/false);
          if (scriptlet->untrusted_scriptlets_dropped() > 0) {
            VLOG(1) << "[Zephyrus] dropped "
                    << scriptlet->untrusted_scriptlets_dropped()
                    << " trusted scriptlet rule(s) from third-party lists";
          }
          params->AddRules(contents);
          // Only uBlock Origin's own sections may redirect; see
          // AdblockUrlSkipFilter.
          urlskip->AddRules(contents, /*trust_unsectioned=*/false);
          csp->AddRules(contents);
        }
        AdblockPageRules rules;
        rules.cosmetic = std::move(cosmetic);
        rules.scriptlet = std::move(scriptlet);
        rules.params = std::move(params);
        rules.urlskip = std::move(urlskip);
        rules.csp = std::move(csp);
        return rules;
      });
  ZephyrusAdblockService::BuiltEngines built;
  built.csp = std::move(page.csp);
  built.urlskip = std::move(page.urlskip);
  built.params = std::move(page.params);
  built.network = std::move(network);
  built.cosmetic = std::move(page.cosmetic);
  built.scriptlet = std::move(page.scriptlet);
  built.shared_index = std::move(shared_index);
  built.regex = std::move(regex);
  built.list_key = list_key;
  return built;
}

// Builds (or shares) the index for `list_key` from an engine that already has
// the rules, and caches it on disk. Runs on a thread pool thread that may
// block.
scoped_refptr<const AdblockSharedIndex> BuildSharedIndex(
    uint64_t list_key,
    std::shared_ptr<const AdblockFilterEngine> engine) {
  return AdblockSharedIndex::GetOrBuild(list_key, *engine, IndexCachePath());
}

}  // namespace

ZephyrusAdblockService::ZephyrusAdblockService(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    PrefService* prefs,
    scoped_refptr<HostContentSettingsMap> host_content_settings_map)
    : prefs_(prefs),
      host_content_settings_map_(std::move(host_content_settings_map)),
      url_loader_factory_(std::move(url_loader_factory)) {
  // Restore persisted settings.
  if (prefs_) {
    enabled_ = prefs_->GetBoolean(kPrefEnabled);
    aggressive_popup_blocking_ = prefs_->GetBoolean(kPrefAggressivePopup);
    total_blocked_ = prefs_->GetInteger(kPrefTotalBlocked);  // lifetime count
    last_flushed_blocked_ = total_blocked_;
    ReloadAllowlistFromPrefs();
    // React live to settings-page edits.
    pref_change_registrar_.Init(prefs_);
    pref_change_registrar_.Add(
        kPrefEnabled,
        base::BindRepeating(
            [](ZephyrusAdblockService* s) {
              const bool was_enabled = s->enabled_;
              s->enabled_ = s->prefs_->GetBoolean(kPrefEnabled);
              // Updates pause while the blocker is off; catch up when it is
              // turned back on rather than at the next hourly check.
              if (s->enabled_ && !was_enabled) {
                s->MaybeStartUpdate();
              }
            },
            base::Unretained(this)));
    pref_change_registrar_.Add(
        kPrefAggressivePopup,
        base::BindRepeating(
            [](ZephyrusAdblockService* s) {
              s->aggressive_popup_blocking_ =
                  s->prefs_->GetBoolean(kPrefAggressivePopup);
            },
            base::Unretained(this)));
    pref_change_registrar_.Add(
        kPrefAllowlist,
        base::BindRepeating(&ZephyrusAdblockService::ReloadAllowlistFromPrefs,
                            base::Unretained(this)));
    // Mirror the live blocked count into prefs for the settings page.
    stats_flush_timer_.Start(FROM_HERE, kStatsFlushPeriod, this,
                             &ZephyrusAdblockService::FlushStats);

    // Zephyrus privacy defaults, applied once — only while a pref still holds
    // its Chromium default — so a user who later flips any of them (here or
    // in chrome://settings) keeps their choice. FindPreference() guards
    // against prefs that aren't registered on this platform/build.
    auto apply_privacy_default = [this](const char* name, base::Value value) {
      const PrefService::Preference* pref = prefs_->FindPreference(name);
      if (pref && pref->IsDefaultValue() &&
          pref->GetType() == value.type()) {
        prefs_->Set(name, value);
      }
    };
    // Block third-party cookies (the cross-site cookies data brokers use to
    // profile users).
    apply_privacy_default(
        prefs::kCookieControlsMode,
        base::Value(static_cast<int>(
            content_settings::CookieControlsMode::kBlockThirdParty)));
    // First-launch network audit (2026-07): search/omnibox suggestions ping
    // the search engine with keystrokes and on-focus context
    // (www.google.com/async/folae) — off by default.
    apply_privacy_default("search.suggest_enabled", base::Value(false));
    // Gaia account reconciliation (accounts.google.com/ListAccounts) — off;
    // Zephyrus has no Google sign-in surface. (Literal pref names, matching
    // components/signin/public/base/signin_pref_names.h.)
    apply_privacy_default("signin.allowed", base::Value(false));
    apply_privacy_default("signin.allowed_on_next_startup",
                          base::Value(false));
  }

  {
    auto builtin = std::make_unique<AdblockParamFilter>();
    builtin->AddBuiltInRules();
    param_filter_ = std::move(builtin);
  }

  // Requests wait for the full list (see network_rules_ready()) unless the
  // blocker is off, in which case there is nothing to wait for.
  startup_gate_ = std::make_unique<AdblockStartupGate>(
      enabled_ && base::FeatureList::IsEnabled(kZephyrusAdblockHoldUntilReady),
      kStartupHoldTimeout);

  // Immediate small coverage; the full list swaps in asynchronously.
  engine_ = std::make_shared<AdblockFilterEngine>();
  engine_->AddRules(kStarterFilterList);
  rule_count_ =
      engine_->block_rule_count() + engine_->exception_rule_count();
  // Sync-load the YouTube-critical scriptlets now so they beat the first
  // navigation (the full bundled list loads asynchronously below and races a
  // fast page load; scriptlets MUST run at document-start to work).
  {
    auto scriptlet = std::make_unique<AdblockScriptletEngine>();
    scriptlet->AddRules(kStarterScriptletList);
    scriptlet_engine_ = std::move(scriptlet);
    auto cosmetic = std::make_unique<AdblockCosmeticEngine>();
    cosmetic->AddRules(kStarterCosmeticList,
                       /*inject_generic_selectors=*/true);
    cosmetic_engine_ = std::move(cosmetic);
  }
  LoadFullFilterListAsync();
  list_updated_subscription_ = ListUpdatedCallbacks().Add(
      base::BindRepeating(&ZephyrusAdblockService::LoadFullFilterListAsync,
                          weak_factory_.GetWeakPtr()));

  // Daily filter-list auto-update (only where we have a network factory, i.e.
  // not incognito/guest). First check shortly after startup, then periodically.
  if (url_loader_factory_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusAdblockService::MaybeStartUpdate,
                       weak_factory_.GetWeakPtr()),
        kInitialUpdateDelay);
    update_timer_.Start(FROM_HERE, kUpdateCheckPeriod, this,
                        &ZephyrusAdblockService::MaybeStartUpdate);
  }
}

ZephyrusAdblockService::~ZephyrusAdblockService() = default;

std::optional<GURL> ZephyrusAdblockService::StripTrackingParams(
    const GURL& url,
    const GURL& initiator,
    ResourceType type) {
  if (!enabled_ || !param_filter_ ||
      !base::FeatureList::IsEnabled(kZephyrusAdblockStripTrackingParams)) {
    return std::nullopt;
  }
  // A page load is judged by the site being opened, a subresource by the page
  // that asked for it: the allowlist is "this site is left alone".
  if (IsAllowlisted(type == kTypeDocument ? url : initiator)) {
    return std::nullopt;
  }
  std::optional<GURL> cleaned = param_filter_->Strip(url, initiator, type);
  if (cleaned) {
    ++total_params_stripped_;
  }
  return cleaned;
}

void ZephyrusAdblockService::RunWhenNetworkRulesReady(
    base::OnceClosure callback) {
  if (!startup_gate_) {
    std::move(callback).Run();
    return;
  }
  startup_gate_->RunWhenOpen(std::move(callback));
}

void ZephyrusAdblockService::SetEnabled(bool enabled) {
  const bool was_enabled = enabled_;
  enabled_ = enabled;
  if (!enabled_ && startup_gate_) {
    startup_gate_->Open();  // nothing is blocked, so nothing to wait for
  }
  if (prefs_) {
    prefs_->SetBoolean(kPrefEnabled, enabled);
  }
  if (enabled && !was_enabled) {
    MaybeStartUpdate();
  }
}

void ZephyrusAdblockService::SetAggressivePopupBlocking(bool enabled) {
  aggressive_popup_blocking_ = enabled;
  if (prefs_) {
    prefs_->SetBoolean(kPrefAggressivePopup, enabled);
  }
}

bool ZephyrusAdblockService::third_party_cookie_blocking() const {
  return prefs_ &&
         prefs_->GetInteger(prefs::kCookieControlsMode) ==
             static_cast<int>(
                 content_settings::CookieControlsMode::kBlockThirdParty);
}

void ZephyrusAdblockService::SetThirdPartyCookieBlocking(bool enabled) {
  if (!prefs_) {
    return;
  }
  // Chromium's cookie-settings layer observes this pref and enforces it in
  // the network stack and for script cookie access; nothing else needed here.
  prefs_->SetInteger(
      prefs::kCookieControlsMode,
      static_cast<int>(enabled
                           ? content_settings::CookieControlsMode::
                                 kBlockThirdParty
                           : content_settings::CookieControlsMode::kOff));
}

void ZephyrusAdblockService::FlushStats() {
  if (!prefs_ || total_blocked_ == last_flushed_blocked_) {
    return;
  }
  prefs_->SetInteger(kPrefTotalBlocked, total_blocked_);
  last_flushed_blocked_ = total_blocked_;
}

void ZephyrusAdblockService::ReloadAllowlistFromPrefs() {
  const std::vector<std::string> old_allowlist = std::move(allowlist_);
  allowlist_.clear();
  if (!prefs_) {
    return;
  }
  for (const base::Value& v : prefs_->GetList(kPrefAllowlist)) {
    if (v.is_string()) {
      allowlist_.push_back(v.GetString());
    }
  }

  // Mirror the allowlist into third-party-cookie exceptions: allowlisting a
  // site suspends cookie blocking there too (secondary pattern "[*.]domain" =
  // any page on the site or its subdomains as the top-level frame). Diffing
  // old vs. new handles every entry point — the Shield UI, the settings page,
  // and startup (old list empty → every persisted entry is re-applied, which
  // is idempotent).
  if (!host_content_settings_map_) {
    return;
  }
  auto sync_exception = [this](const std::string& domain, bool allow) {
    ContentSettingsPattern pattern =
        ContentSettingsPattern::FromString("[*.]" + domain);
    if (!pattern.IsValid()) {
      return;
    }
    host_content_settings_map_->SetContentSettingCustomScope(
        ContentSettingsPattern::Wildcard(), pattern,
        ContentSettingsType::COOKIES,
        allow ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_DEFAULT);
  };
  for (const std::string& domain : allowlist_) {
    if (!std::ranges::contains(old_allowlist, domain)) {
      sync_exception(domain, /*allow=*/true);
    }
  }
  for (const std::string& domain : old_allowlist) {
    if (!std::ranges::contains(allowlist_, domain)) {
      sync_exception(domain, /*allow=*/false);
    }
  }
}

bool ZephyrusAdblockService::IsAllowlisted(const GURL& url) const {
  if (allowlist_.empty() || !url.has_host()) {
    return false;
  }
  const std::string host = base::ToLowerASCII(url.host());
  for (const std::string& domain : allowlist_) {
    if (domain.empty()) {
      continue;
    }
    // host == domain, or host is a subdomain of domain (".domain").
    if (host == domain ||
        (host.size() > domain.size() &&
         host.compare(host.size() - domain.size() - 1, domain.size() + 1,
                      "." + domain) == 0)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ZephyrusAdblockService::GetAllowlist() const {
  return allowlist_;
}

void ZephyrusAdblockService::AddAllowlistDomain(const std::string& domain) {
  std::string d = NormalizeDomain(domain);
  if (d.empty() || !prefs_) {
    return;
  }
  ScopedListPrefUpdate update(prefs_, kPrefAllowlist);
  for (const base::Value& v : *update) {
    if (v.is_string() && v.GetString() == d) {
      return;  // already present
    }
  }
  update->Append(d);
  // pref observer reloads allowlist_.
}

void ZephyrusAdblockService::RemoveAllowlistDomain(const std::string& domain) {
  std::string d = NormalizeDomain(domain);
  if (d.empty() || !prefs_) {
    return;
  }
  ScopedListPrefUpdate update(prefs_, kPrefAllowlist);
  update->EraseIf([&d](const base::Value& v) {
    return v.is_string() && v.GetString() == d;
  });
}

void ZephyrusAdblockService::MaybeStartUpdate() {
  // Off means off: the traffic annotation promises that turning the blocker
  // off stops these downloads, and it did not.
  if (updating_ || !url_loader_factory_ || !enabled_) {
    return;
  }
  // Check the on-disk list off the UI thread, then decide.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&DownloadedListDue),
      base::BindOnce(&ZephyrusAdblockService::OnListAgeChecked,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusAdblockService::OnListAgeChecked(std::optional<UpdateKind> due) {
  if (updating_ || !url_loader_factory_ || !enabled_ || !due) {
    return;
  }
  updating_ = true;
  updater_ = std::make_unique<ZephyrusAdblockUpdater>(
      url_loader_factory_, DownloadedListPath(), *due);
  updater_->Start(base::BindOnce(&ZephyrusAdblockService::OnUpdateFinished,
                                 weak_factory_.GetWeakPtr()));
}

void ZephyrusAdblockService::OnUpdateFinished(bool success) {
  updating_ = false;
  updater_.reset();
  if (success) {
    // Reload the engines from the freshly-downloaded list and hot-swap them
    // in -- in every profile, this one included.
    ListUpdatedCallbacks().Notify();
  }
}

void ZephyrusAdblockService::LoadFullFilterListAsync() {
  // USER_VISIBLE, not BEST_EFFORT. Best-effort work is held back until startup
  // is over, and until this lands only the small starter list is in force --
  // so every page opened in the first seconds, which is every tab a session
  // restore reopens, loaded with its ads. MEASURED: a page opened ~2 s after
  // launch got none of its site-specific hiding rules; the same page opened
  // after a 12 s wait got all of them. Parsing takes ~60 ms on a pool thread.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&BuildEnginesFromBundledList),
      base::BindOnce(&ZephyrusAdblockService::OnFullFilterListLoaded,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusAdblockService::OnFullFilterListLoaded(BuiltEngines built) {
  current_list_key_ = built.list_key;
  if (built.regex) {
    VLOG(1) << "[Zephyrus] adblock: regex rules: " << built.regex->rule_count()
            << " (" << built.regex->skipped_count() << " skipped, "
            << built.regex->unfiltered_count() << " without a prefilter)";
    regex_ = std::move(built.regex);
  }
  if (built.shared_index) {
    UseSharedIndex(std::move(built.shared_index));
  } else if (built.network) {
    rule_count_ = built.network->block_rule_count() +
                  built.network->exception_rule_count() +
                  (regex_ ? regex_->rule_count() : 0);
    engine_ = std::move(built.network);
    index_.reset();
    VLOG(1) << "[Zephyrus] adblock: network rules matched by a per-profile "
               "engine ("
            << rule_count_ << " rules)";
    if (prefs_) {
      prefs_->SetInteger(kPrefRuleCount, static_cast<int>(rule_count_));
    }
    if (built.list_key != 0) {
      // The compiled index is on but not built for this list yet. The engine
      // above covers the page loads of the next half second; the index
      // replaces it when it is ready, and is cached for the next start.
      base::ThreadPool::PostTaskAndReplyWithResult(
          FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
          base::BindOnce(&BuildSharedIndex, built.list_key, engine_),
          base::BindOnce(&ZephyrusAdblockService::OnIndexBuilt,
                         weak_factory_.GetWeakPtr(), built.list_key));
    }
  }
  if (built.urlskip) {
    VLOG(1) << "[Zephyrus] adblock: urlskip rules: "
            << built.urlskip->rule_count() << " ("
            << built.urlskip->skipped_count() << " skipped, "
            << built.urlskip->untrusted_count() << " from untrusted lists)";
    urlskip_ = std::move(built.urlskip);
  }
  if (built.csp) {
    VLOG(1) << "[Zephyrus] adblock: csp rules: " << built.csp->rule_count()
            << " (" << built.csp->skipped_count() << " skipped)";
    csp_filter_ = std::move(built.csp);
  }
  if (built.params) {
    VLOG(1) << "[Zephyrus] adblock: tracking-parameter rules: "
            << built.params->rule_count() << " ("
            << built.params->skipped_count() << " skipped)";
    param_filter_ = std::move(built.params);
  }
  if (built.cosmetic) {
    cosmetic_engine_ = std::move(built.cosmetic);
  }
  if (built.scriptlet) {
    scriptlet_engine_ = std::move(built.scriptlet);
  }
  // The full list is in force (or there is none to wait for): let the requests
  // that were held go, and check them against it.
  if (startup_gate_) {
    startup_gate_->Open();
  }
}

void ZephyrusAdblockService::OnIndexBuilt(
    uint64_t list_key,
    scoped_refptr<const AdblockSharedIndex> shared_index) {
  // A newer list may have loaded while this was building; its engine is the
  // one in force, and this index is for a list nobody is using any more.
  if (!shared_index || list_key != current_list_key_) {
    return;
  }
  UseSharedIndex(std::move(shared_index));
}

void ZephyrusAdblockService::UseSharedIndex(
    scoped_refptr<const AdblockSharedIndex> shared_index) {
  rule_count_ = shared_index->block_rule_count() +
                shared_index->exception_rule_count() +
                (regex_ ? regex_->rule_count() : 0);
  index_ = std::move(shared_index);
  engine_.reset();  // the index replaces it
  VLOG(1) << "[Zephyrus] adblock: network rules matched by the shared "
             "compiled index ("
          << rule_count_ << " rules, " << index_->size_bytes() << " bytes)";
  if (prefs_) {
    prefs_->SetInteger(kPrefRuleCount, static_cast<int>(rule_count_));
  }
}

uint32_t ZephyrusAdblockService::DocumentExceptions(const GURL& url) const {
  if (index_) {
    return index_->GetDocumentExceptions(url);
  }
  return engine_ ? engine_->GetDocumentExceptions(url) : 0;
}

std::vector<std::string> ZephyrusAdblockService::GetCosmeticSelectors(
    const GURL& url) const {
  if (!enabled_ || !cosmetic_engine_ || IsAllowlisted(url)) {
    return {};
  }
  const uint32_t exceptions = DocumentExceptions(url);
  if (exceptions & (kExceptDocument | kExceptElemHide)) {
    return {};
  }
  return cosmetic_engine_->GetSelectorsForUrl(
      url, !(exceptions & kExceptGenericHide),
      !(exceptions & kExceptSpecificHide));
}

namespace {

// One selector list per rule would mean a single malformed selector — the
// lists are community-maintained and occasionally carry one — silently voiding
// every other selector in the same rule. Chunking caps that blast radius while
// keeping the stylesheet compact.
constexpr size_t kSelectorsPerRule = 64;

// Splits a filter's selector at its top-level commas: "a, b:is(c, d)" -> "a",
// "b:is(c, d)". Commas inside (), [] or quotes belong to the selector.
std::vector<std::string_view> SplitSelectorList(std::string_view selector) {
  std::vector<std::string_view> parts;
  int depth = 0;
  char quote = 0;
  size_t start = 0;
  for (size_t i = 0; i < selector.size(); ++i) {
    const char c = selector[i];
    if (quote) {
      if (c == quote) {
        quote = 0;
      }
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '(' || c == '[') {
      ++depth;
    } else if ((c == ')' || c == ']') && depth > 0) {
      --depth;
    } else if (c == ',' && depth == 0) {
      parts.push_back(selector.substr(start, i - start));
      start = i + 1;
    }
  }
  parts.push_back(selector.substr(start));
  std::vector<std::string_view> trimmed;
  for (std::string_view part : parts) {
    part = base::TrimWhitespaceASCII(part, base::TRIM_ALL);
    if (!part.empty()) {
      trimmed.push_back(part);
    }
  }
  return trimmed;
}

// Whether a selector part starts at the root of the document ("html.x .ad",
// "body > .ad", ":root .ad"), which the body-scoping guard cannot simply be
// put in front of.
bool StartsAtRoot(std::string_view part) {
  for (std::string_view root : {"html", "body", ":root"}) {
    if (part.size() >= root.size() &&
        base::EqualsCaseInsensitiveASCII(part.substr(0, root.size()), root) &&
        (part.size() == root.size() ||
         !(base::IsAsciiAlphaNumeric(part[root.size()]) ||
           part[root.size()] == '-' || part[root.size()] == '_'))) {
      return true;
    }
  }
  return false;
}

std::string BuildHideCss(const std::vector<std::string>& selectors) {
  std::string css;
  for (size_t i = 0; i < selectors.size(); ++i) {
    if (i % kSelectorsPerRule == 0) {
      if (i) {
        css += "{display:none !important;}";
      }
    } else {
      css += ',';
    }
    // Every hide is scoped under body, so no rule can ever match <html> or
    // <body> and blank the page. A state class on the root — Sourcepoint's
    // `sp-message-open` — is indistinguishable from a banner's own class in a
    // filter list, and one such entry took theguardian.com down to a white
    // page. :where() keeps the guard out of the specificity calculation.
    //
    // The guard goes in front of EACH comma-separated part. Prefixed to the
    // whole of "a, body.x" it covered only "a" and left "body.x" free to hide
    // the page. A part that itself starts at the root ("body > .ad") would
    // never match with the guard written before it, so it is wrapped in
    // :is() instead; :is() is forgiving, so a bad part only drops itself.
    bool first = true;
    for (std::string_view part : SplitSelectorList(selectors[i])) {
      if (!first) {
        css += ',';
      }
      first = false;
      if (StartsAtRoot(part)) {
        css += ":where(body) :is(";
        css += part;
        css += ')';
      } else {
        css += ":where(body) ";
        css += part;
      }
    }
    if (first) {
      // Nothing but separators: keep the list well-formed with a selector
      // that matches nothing.
      css += ":where(body) :not(*)";
    }
  }
  if (!selectors.empty()) {
    css += "{display:none !important;}";
  }
  return css;
}

}  // namespace

std::string ZephyrusAdblockService::GetCosmeticCss(const GURL& url) const {
  std::vector<std::string> selectors = GetCosmeticSelectors(url);
  if (selectors.empty()) {
    return std::string();
  }
  return BuildHideCss(selectors);
}

std::string ZephyrusAdblockService::GetCosmeticStyleCss(const GURL& url) const {
  if (!enabled_ || !cosmetic_engine_ || IsAllowlisted(url)) {
    return std::string();
  }
  const uint32_t exceptions = DocumentExceptions(url);
  if (exceptions & (kExceptDocument | kExceptElemHide)) {
    return std::string();
  }
  std::string css;
  for (const std::string& rule : cosmetic_engine_->GetStyleRulesForUrl(
           url, !(exceptions & kExceptGenericHide),
           !(exceptions & kExceptSpecificHide))) {
    css += rule;
  }
  return css;
}

std::string ZephyrusAdblockService::GetGenericCosmeticCss(
    const GURL& url,
    const std::vector<std::string>& tokens) const {
  if (!enabled_ || !cosmetic_engine_ || IsAllowlisted(url) || tokens.empty()) {
    return std::string();
  }
  if (DocumentExceptions(url) &
      (kExceptDocument | kExceptElemHide | kExceptGenericHide)) {
    return std::string();
  }
  return BuildHideCss(
      cosmetic_engine_->GetGenericSelectorsForTokens(url, tokens));
}

std::string ZephyrusAdblockService::GetScriptletInjection(
    const GURL& url) const {
  if (!enabled_ || IsAllowlisted(url)) {
    return std::string();
  }
  const uint32_t exceptions = DocumentExceptions(url);
  if (exceptions & kExceptDocument) {
    return std::string();
  }
  std::string script;
  if (scriptlet_engine_) {
    script = scriptlet_engine_->BuildInjectionScriptForUrl(url);
  }
  // Procedural cosmetic filters run as page script too: CSS cannot say
  // "the container whose text mentions Sponsored".
  if (cosmetic_engine_ && !(exceptions & (kExceptElemHide | kExceptSpecificHide)) &&
      base::FeatureList::IsEnabled(kZephyrusAdblockProceduralCosmetic)) {
    script += BuildProceduralScript(
        cosmetic_engine_->GetProceduralSelectorsForUrl(url));
  }
  return script;
}

bool ZephyrusAdblockService::WouldBlockRequest(const GURL& url,
                                               const GURL& initiator,
                                               ResourceType type) const {
  if (!enabled_ || !(index_ || engine_) || IsAllowlisted(initiator)) {
    return false;
  }
  RegexHits regex_hits;
  if (regex_) {
    regex_hits = regex_->Match(url, initiator, type);
  }
  return index_ ? index_->ShouldBlock(url, initiator, type, &regex_hits)
                : engine_->ShouldBlock(url, initiator, type, &regex_hits);
}

std::optional<uint16_t> ZephyrusAdblockService::StandInFor(
    const GURL& url,
    const GURL& initiator,
    ResourceType type) const {
  if (!base::FeatureList::IsEnabled(kZephyrusAdblockStandIns)) {
    return std::nullopt;
  }
  return index_ ? index_->ResolveRedirect(url, initiator, type)
                : engine_ ? engine_->ResolveRedirect(url, initiator, type)
                          : std::nullopt;
}

bool ZephyrusAdblockService::ShouldBlockRequest(const GURL& url,
                                                const GURL& initiator,
                                                ResourceType type) {
  if (WouldBlockRequest(url, initiator, type)) {
    ++total_blocked_;
    return true;
  }
  return false;
}

std::optional<GURL> ZephyrusAdblockService::SkipUrl(const GURL& url,
                                                    const GURL& initiator,
                                                    ResourceType type,
                                                    bool blocked) {
  if (!enabled_ || !urlskip_ ||
      !base::FeatureList::IsEnabled(kZephyrusAdblockUrlSkip)) {
    return std::nullopt;
  }
  // As for tracking parameters: a page load is judged by the site being opened,
  // anything else by the page that asked for it.
  if (IsAllowlisted(type == kTypeDocument ? url : initiator)) {
    return std::nullopt;
  }
  std::optional<GURL> target = urlskip_->Skip(url, initiator, type, blocked);
  if (target) {
    ++total_urls_skipped_;
  }
  return target;
}

std::vector<std::string> ZephyrusAdblockService::CspPoliciesFor(
    const GURL& url,
    const GURL& initiator,
    ResourceType type) const {
  if (!enabled_ || !csp_filter_ ||
      !base::FeatureList::IsEnabled(kZephyrusAdblockCsp)) {
    return {};
  }
  if (IsAllowlisted(type == kTypeDocument ? url : initiator)) {
    return {};
  }
  return csp_filter_->PoliciesFor(url, initiator, type);
}

bool ZephyrusAdblockService::ShouldBlockPopup(const GURL& url,
                                              const GURL& opener) {
  if (!enabled_ || !(index_ || engine_) || IsAllowlisted(opener)) {
    return false;
  }
  RegexHits regex_hits;
  if (regex_) {
    regex_hits = regex_->Match(url, opener, kTypePopup);
  }
  if (index_ ? index_->ShouldBlock(url, opener, kTypePopup, &regex_hits)
             : engine_->ShouldBlock(url, opener, kTypePopup, &regex_hits)) {
    ++total_blocked_;
    return true;
  }
  return false;
}

}  // namespace zephyrus_adblock

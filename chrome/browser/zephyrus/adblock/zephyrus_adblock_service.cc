// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include <optional>
#include "base/logging.h"

#include <utility>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/path_service.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
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

// How often the browser refreshes the filter lists, and the initial delay after
// startup before the first staleness check (so it doesn't compete with launch).
constexpr base::TimeDelta kUpdateInterval = base::Hours(24);
constexpr base::TimeDelta kUpdateCheckPeriod = base::Hours(6);
constexpr base::TimeDelta kInitialUpdateDelay = base::Seconds(45);

// Combined auto-updated list location (shared across profiles).
base::FilePath DownloadedListPath() {
  base::FilePath dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &dir)) {
    return base::FilePath();
  }
  return dir.AppendASCII("ZephyrusAdBlock").AppendASCII("filters.txt");
}

// Age of the downloaded list, or a very large value if it doesn't exist. Runs
// on a background thread (touches the filesystem).
base::TimeDelta DownloadedListAge() {
  base::FilePath path = DownloadedListPath();
  base::File::Info info;
  if (path.empty() || !base::GetFileInfo(path, &info) || info.size == 0) {
    return base::TimeDelta::Max();
  }
  return base::Time::Now() - info.last_modified;
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
std::tuple<std::unique_ptr<AdblockFilterEngine>,
           std::unique_ptr<AdblockCosmeticEngine>,
           std::unique_ptr<AdblockScriptletEngine>>
BuildEnginesFromBundledList() {
  auto network = std::make_unique<AdblockFilterEngine>();
  auto cosmetic = std::make_unique<AdblockCosmeticEngine>();
  auto scriptlet = std::make_unique<AdblockScriptletEngine>();
  network->AddRules(kStarterFilterList);
  scriptlet->AddRules(kStarterScriptletList);
  cosmetic->AddRules(kStarterCosmeticList, /*inject_generic_selectors=*/true);

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
    network->AddRules(contents);
    cosmetic->AddRules(contents);
    scriptlet->AddRules(contents);
  }
  return {std::move(network), std::move(cosmetic), std::move(scriptlet)};
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
              s->enabled_ = s->prefs_->GetBoolean(kPrefEnabled);
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

  // Immediate small coverage; the full list swaps in asynchronously.
  engine_ = std::make_unique<AdblockFilterEngine>();
  engine_->AddRules(kStarterFilterList);
  rule_count_ =
      engine_->block_rule_count() + engine_->exception_rule_count();
  // Sync-load the YouTube-critical scriptlets now so they beat the first
  // navigation (the full bundled list loads asynchronously below and races a
  // fast page load; scriptlets MUST run at document-start to work).
  scriptlet_engine_ = std::make_unique<AdblockScriptletEngine>();
  scriptlet_engine_->AddRules(kStarterScriptletList);
  cosmetic_engine_ = std::make_unique<AdblockCosmeticEngine>();
  cosmetic_engine_->AddRules(kStarterCosmeticList,
                             /*inject_generic_selectors=*/true);
  LoadFullFilterListAsync();

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

void ZephyrusAdblockService::SetEnabled(bool enabled) {
  enabled_ = enabled;
  if (prefs_) {
    prefs_->SetBoolean(kPrefEnabled, enabled);
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
  if (updating_ || !url_loader_factory_) {
    return;
  }
  // Check the on-disk list's age off the UI thread, then decide.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&DownloadedListAge),
      base::BindOnce(&ZephyrusAdblockService::OnListAgeChecked,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusAdblockService::OnListAgeChecked(base::TimeDelta age) {
  if (updating_ || !url_loader_factory_ || age < kUpdateInterval) {
    return;
  }
  updating_ = true;
  updater_ = std::make_unique<ZephyrusAdblockUpdater>(url_loader_factory_,
                                                      DownloadedListPath());
  updater_->Start(base::BindOnce(&ZephyrusAdblockService::OnUpdateFinished,
                                 weak_factory_.GetWeakPtr()));
}

void ZephyrusAdblockService::OnUpdateFinished(bool success) {
  updating_ = false;
  updater_.reset();
  if (success) {
    // Reload the engines from the freshly-downloaded list and hot-swap them in.
    LoadFullFilterListAsync();
  }
}

void ZephyrusAdblockService::LoadFullFilterListAsync() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&BuildEnginesFromBundledList),
      base::BindOnce(&ZephyrusAdblockService::OnFullFilterListLoaded,
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusAdblockService::OnFullFilterListLoaded(
    std::unique_ptr<AdblockFilterEngine> network,
    std::unique_ptr<AdblockCosmeticEngine> cosmetic,
    std::unique_ptr<AdblockScriptletEngine> scriptlet) {
  if (network) {
    rule_count_ =
        network->block_rule_count() + network->exception_rule_count();
    engine_ = std::move(network);
    if (prefs_) {
      prefs_->SetInteger(kPrefRuleCount, static_cast<int>(rule_count_));
    }
  }
  if (cosmetic) {
    cosmetic_engine_ = std::move(cosmetic);
  }
  if (scriptlet) {
    scriptlet_engine_ = std::move(scriptlet);
  }
}

std::vector<std::string> ZephyrusAdblockService::GetCosmeticSelectors(
    const GURL& url) const {
  if (!enabled_ || !cosmetic_engine_ || IsAllowlisted(url)) {
    return {};
  }
  return cosmetic_engine_->GetSelectorsForUrl(url);
}

namespace {

// One selector list per rule would mean a single malformed selector — the
// lists are community-maintained and occasionally carry one — silently voiding
// every other selector in the same rule. Chunking caps that blast radius while
// keeping the stylesheet compact.
constexpr size_t kSelectorsPerRule = 64;

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
    css += ":where(body) ";
    css += selectors[i];
  }
  if (!selectors.empty()) {
    css += "{display:none !important;}";
  }
  return css;
}

}  // namespace

std::string ZephyrusAdblockService::GetCosmeticCss(const GURL& url) const {
  if (!enabled_ || !cosmetic_engine_ || IsAllowlisted(url)) {
    return std::string();
  }
  std::vector<std::string> selectors = cosmetic_engine_->GetSelectorsForUrl(url);
  if (selectors.empty()) {
    return std::string();
  }
  std::string css = BuildHideCss(selectors);
  // Style overrides last, so a `:style()` rule releasing an overlay's
  // scroll-lock is not itself outranked by an earlier rule.
  for (const std::string& rule : cosmetic_engine_->GetStyleRulesForUrl(url)) {
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
  return BuildHideCss(
      cosmetic_engine_->GetGenericSelectorsForTokens(url, tokens));
}

std::string ZephyrusAdblockService::GetScriptletInjection(
    const GURL& url) const {
  if (!enabled_ || !scriptlet_engine_ || IsAllowlisted(url)) {
    return std::string();
  }
  return scriptlet_engine_->BuildInjectionScriptForUrl(url);
}

bool ZephyrusAdblockService::ShouldBlockRequest(const GURL& url,
                                                const GURL& initiator,
                                                ResourceType type) {
  if (!enabled_ || !engine_ || IsAllowlisted(initiator)) {
    return false;
  }
  if (engine_->ShouldBlock(url, initiator, type)) {
    ++total_blocked_;
    return true;
  }
  return false;
}

bool ZephyrusAdblockService::ShouldBlockPopup(const GURL& url,
                                              const GURL& opener) {
  if (!enabled_ || !engine_ || IsAllowlisted(opener)) {
    return false;
  }
  if (engine_->ShouldBlock(url, opener, kTypePopup)) {
    ++total_blocked_;
    return true;
  }
  return false;
}

}  // namespace zephyrus_adblock

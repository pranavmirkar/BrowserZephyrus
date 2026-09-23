// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SERVICE_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/zephyrus/adblock/adblock_cosmetic_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"
#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_updater.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"

class GURL;
class HostContentSettingsMap;
class PrefService;

namespace network {
class SharedURLLoaderFactory;
}

namespace zephyrus_adblock {
class ZephyrusAdblockUpdater;
}

namespace zephyrus_adblock {

// Per-profile owner of the ad-block filter engine and statistics. Lives on the
// UI thread; the URL-loader factory proxy (also UI thread) queries it per
// request. A small starter list is active immediately at construction; the full
// bundled EasyList + EasyPrivacy is parsed on a background thread and swapped in
// when ready.
class ZephyrusAdblockService : public KeyedService {
 public:
  // `url_loader_factory` is used for daily background filter-list updates; pass
  // null (e.g. for incognito/guest) to disable updating for this instance.
  // `prefs` persists the user-facing settings (toggles + allowlist).
  // `host_content_settings_map` (may be null in tests) receives third-party
  // cookie exceptions mirroring the allowlist.
  ZephyrusAdblockService(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      PrefService* prefs,
      scoped_refptr<HostContentSettingsMap> host_content_settings_map);
  ZephyrusAdblockService(const ZephyrusAdblockService&) = delete;
  ZephyrusAdblockService& operator=(const ZephyrusAdblockService&) = delete;
  ~ZephyrusAdblockService() override;

  bool enabled() const { return enabled_; }
  void SetEnabled(bool enabled);

  // Aggressive pop-under blocking: block cross-site script-opened popups even
  // when their destination isn't in any filter list (catches piracy-site
  // redirect ads to randomized domains). On by default.
  bool aggressive_popup_blocking() const { return aggressive_popup_blocking_; }
  void SetAggressivePopupBlocking(bool enabled);

  // Third-party cookie blocking (the cross-site cookies data brokers use to
  // track users). Backed by Chromium's own kCookieControlsMode enforcement —
  // the Shield just owns the setting and defaults it ON for Zephyrus.
  bool third_party_cookie_blocking() const;
  void SetThirdPartyCookieBlocking(bool enabled);

  // Per-site allowlist: domains on which all blocking (network, popup, cosmetic,
  // scriptlet) is suppressed. Matching is host-or-parent-domain.
  bool IsAllowlisted(const GURL& url) const;
  std::vector<std::string> GetAllowlist() const;
  void AddAllowlistDomain(const std::string& domain);
  void RemoveAllowlistDomain(const std::string& domain);

  // Increments the blocked counter (used by the browser client's heuristic
  // pop-under blocking, which decides outside the engine).
  void RecordBlockedPopup() { ++total_blocked_; }

  // Returns true if the request should be blocked; increments the total counter
  // when it does. Always returns false when disabled.
  bool ShouldBlockRequest(const GURL& url,
                          const GURL& initiator,
                          ResourceType type);

  // Returns true if a pop-up/pop-under to `url` opened from `opener` should be
  // blocked (matched against $popup and general block rules).
  bool ShouldBlockPopup(const GURL& url, const GURL& opener);

  // Cosmetic filtering: CSS selectors to hide on `url`. Empty until the full
  // list finishes loading, or when cosmetic filtering is disabled.
  std::vector<std::string> GetCosmeticSelectors(const GURL& url) const;

  // Cosmetic filtering as a ready-to-inject stylesheet body
  // ("sel1,sel2{display:none !important;}"), or empty if no selectors apply.
  // Hiding rules only; every declaration is !important, so the renderer
  // injects this at USER origin, where it outranks the page's own !important.
  std::string GetCosmeticCss(const GURL& url) const;

  // The `:style()` rules for `url` -- declarations that restyle rather than
  // hide, such as releasing a scroll-lock. Kept apart from GetCosmeticCss
  // because not every list author marks these !important, and at user origin
  // a declaration without it loses to the page. Injected at author origin,
  // where they keep the cascade position they always had.
  std::string GetCosmeticStyleCss(const GURL& url) const;

  // The generic element-hiding CSS unlocked by the ids/classes the renderer
  // surveyed in the live document. See AdblockCosmeticEngine.
  std::string GetGenericCosmeticCss(const GURL& url,
                                    const std::vector<std::string>& tokens)
      const;

  // Scriptlet injection: the main-world JS to run at document-start on `url`
  // (empty if none / disabled). Neutralizes ad logic on hard sites (YouTube).
  std::string GetScriptletInjection(const GURL& url) const;

  int total_blocked() const { return total_blocked_; }
  size_t rule_count() const { return rule_count_; }

  base::WeakPtr<ZephyrusAdblockService> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  void LoadFullFilterListAsync();
  // Called with the freshly-built engines (the returned tuple is unpacked into
  // these args by PostTaskAndReplyWithResult).
  void OnFullFilterListLoaded(
      std::unique_ptr<AdblockFilterEngine> network,
      std::unique_ptr<AdblockCosmeticEngine> cosmetic,
      std::unique_ptr<AdblockScriptletEngine> scriptlet);

  // Filter-list auto-update. No-op when `url_loader_factory_` is null (off the
  // record) or the blocker is off.
  void MaybeStartUpdate();
  void OnListAgeChecked(std::optional<UpdateKind> due);
  void OnUpdateFinished(bool success);

  // The page-level exceptions ($document, $generichide...) the lists grant
  // `url`. See AdblockFilterEngine::GetDocumentExceptions.
  uint32_t DocumentExceptions(const GURL& url) const;

  // Reloads the in-memory allowlist set from prefs (on startup + pref change).
  void ReloadAllowlistFromPrefs();

  // Periodically mirrors the running blocked count into prefs for the settings
  // page (written only when it changes).
  void FlushStats();

  raw_ptr<PrefService> prefs_ = nullptr;
  PrefChangeRegistrar pref_change_registrar_;
  scoped_refptr<HostContentSettingsMap> host_content_settings_map_;
  std::vector<std::string> allowlist_;

  scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory_;
  std::unique_ptr<ZephyrusAdblockUpdater> updater_;
  base::RepeatingTimer update_timer_;
  base::RepeatingTimer stats_flush_timer_;
  int last_flushed_blocked_ = -1;
  bool updating_ = false;
  base::CallbackListSubscription list_updated_subscription_;

  std::unique_ptr<AdblockFilterEngine> engine_;
  std::unique_ptr<AdblockCosmeticEngine> cosmetic_engine_;
  std::unique_ptr<AdblockScriptletEngine> scriptlet_engine_;
  bool enabled_ = true;
  bool aggressive_popup_blocking_ = true;
  int total_blocked_ = 0;
  size_t rule_count_ = 0;

  base::WeakPtrFactory<ZephyrusAdblockService> weak_factory_{this};
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SERVICE_H_

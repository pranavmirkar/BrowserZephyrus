// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_private_workspace.h"

#include <memory>

#include "base/functional/bind.h"
#include "chrome/browser/preloading/preloading_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/content_settings/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_window_swap.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {
// Key for attaching the controller to the regular Profile.
constexpr char kZephyrusPrivateWorkspaceKey[] = "zephyrus_private_workspace";
// Regular-profile pref: does entering require an OS unlock?
constexpr char kLockEnabledPref[] = "zephyrus.private_workspace.lock_enabled";

// Privacy defaults applied to the private profile (spec items 5, 14, 17).
// These are set on the OTR PrefService, which is an in-memory overlay — the
// regular profile's settings are untouched, and these die with the session.
void ApplyPrivateDefaults(Profile* otr) {
  PrefService* prefs = otr ? otr->GetPrefs() : nullptr;
  if (!prefs) {
    return;
  }
  // IMPORTANT: only write prefs that stay in the OTR overlay. Prefs on the
  // incognito persistence allowlist (pref_service_incognito_allowlist.cc)
  // write THROUGH to the regular profile — setting one here silently changes
  // normal browsing. kCookieControlsMode is on that list, so it must NOT be
  // written here (it was, and it flipped normal browsing to block third-party
  // cookies). OTR already blocks third-party cookies by default, so nothing is
  // lost. kHttpsOnlyModeEnabled and kNetworkPredictionOptions are NOT on the
  // list — overlay-only, safe to set.

  // Never silently fall back to plaintext HTTP.
  prefs->SetBoolean(prefs::kHttpsOnlyModeEnabled, true);
  // NOT DNS-over-HTTPS: kDnsOverHttpsMode lives in LOCAL STATE, not profile
  // prefs, so writing it here would crash on an unregistered pref. DoH is
  // browser-wide by nature (shared resolver), handled via the Shield toggle.
  // No speculative connections. Prefetch and DNS-preconnect reach out to hosts
  // you have not chosen to visit, which is exactly the background chatter spec
  // item 17 rules out.
  prefs->SetInteger(
      prefs::kNetworkPredictionOptions,
      static_cast<int>(prefetch::NetworkPredictionOptions::kDisabled));
  // Search suggestions are cut for OTR by ChromeAutocompleteProviderClient's
  // per-query gate — no pref write needed, and kSearchSuggestEnabled being
  // overlay-only means writing it here would do nothing useful anyway.
}

views::Widget* WidgetFor(Browser* browser) {
  if (!browser) {
    return nullptr;
  }
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
  return view ? view->GetWidget() : nullptr;
}
}  // namespace

// static
ZephyrusPrivateWorkspace* ZephyrusPrivateWorkspace::GetForProfile(
    Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  // Always anchor to the regular profile: entering from a private window must
  // find the same controller.
  profile = profile->GetOriginalProfile();
  auto* controller = static_cast<ZephyrusPrivateWorkspace*>(
      profile->GetUserData(kZephyrusPrivateWorkspaceKey));
  if (!controller) {
    auto owned = std::make_unique<ZephyrusPrivateWorkspace>(profile);
    controller = owned.get();
    profile->SetUserData(kZephyrusPrivateWorkspaceKey, std::move(owned));
  }
  return controller;
}

ZephyrusPrivateWorkspace::ZephyrusPrivateWorkspace(Profile* profile)
    : profile_(profile) {}

ZephyrusPrivateWorkspace::~ZephyrusPrivateWorkspace() {
  // The Profile is going away; drop observations so widget teardown never
  // calls back into a freed controller.
  StopObserving(WidgetFor(private_browser_));
  StopObserving(WidgetFor(return_to_));
}

// static
bool ZephyrusPrivateWorkspace::IsPrivate(const Browser* browser) {
  return browser && browser->profile() && browser->profile()->IsOffTheRecord();
}

void ZephyrusPrivateWorkspace::StopObserving(views::Widget* widget) {
  if (widget && widget->HasObserver(this)) {
    widget->RemoveObserver(this);
  }
}

// static
bool ZephyrusPrivateWorkspace::IsLockEnabled(Profile* profile) {
  if (!profile) {
    return false;
  }
  PrefService* prefs = profile->GetOriginalProfile()->GetPrefs();
  return prefs && prefs->GetBoolean(kLockEnabledPref);
}

// static
void ZephyrusPrivateWorkspace::SetLockEnabled(Profile* profile, bool enabled) {
  if (!profile) {
    return;
  }
  if (PrefService* prefs = profile->GetOriginalProfile()->GetPrefs()) {
    prefs->SetBoolean(kLockEnabledPref, enabled);
  }
}

void ZephyrusPrivateWorkspace::Enter(Browser* from) {
  if (!WidgetFor(from)) {
    return;
  }
  RunWhenUnlocked(from,
                  base::BindOnce(&ZephyrusPrivateWorkspace::EnterUnlocked,
                                 weak_factory_.GetWeakPtr()));
}

void ZephyrusPrivateWorkspace::RunWhenUnlocked(
    Browser* from,
    base::OnceCallback<void(Browser*)> action) {
  // Unlocked already, or the lock is off: go straight through.
  if (!locked_ || !IsLockEnabled(profile_)) {
    std::move(action).Run(from);
    return;
  }
  if (authenticating_) {
    return;  // A prompt is already up; don't stack a second one.
  }
  if (!authenticator_) {
    authenticator_ = std::make_unique<AuthenticatorWin>();
  }
  // No screen lock configured means there is nothing to authenticate against.
  // Letting the user in is the right call — the alternative is locking them out
  // of their own workspace with no way to get back in.
  if (!authenticator_->CanAuthenticateWithScreenLock()) {
    std::move(action).Run(from);
    return;
  }
  authenticating_ = true;
  authenticator_->AuthenticateUser(
      l10n_util::GetStringUTF16(IDS_ZEPHYRUS_UNLOCK_PRIVATE_WORKSPACE),
      base::BindOnce(&ZephyrusPrivateWorkspace::OnAuthComplete,
                     weak_factory_.GetWeakPtr(), from->AsWeakPtr(),
                     std::move(action)));
}

void ZephyrusPrivateWorkspace::OnAuthComplete(
    base::WeakPtr<Browser> from,
    base::OnceCallback<void(Browser*)> action,
    bool success) {
  authenticating_ = false;
  // On failure or cancel: return without creating or showing anything, so a
  // refused prompt leaks nothing about the workspace. `from` is weak because
  // the window behind the modal prompt can be closed while it is up.
  if (!success || !from) {
    return;
  }
  locked_ = false;
  std::move(action).Run(from.get());
}

void ZephyrusPrivateWorkspace::EnterUnlocked(Browser* from) {
  views::Widget* from_widget = WidgetFor(from);
  if (!from_widget) {
    return;
  }
  const gfx::Rect bounds = from_widget->GetWindowBoundsInScreen();
  const bool maximized = from_widget->IsMaximized();

  if (!private_browser_) {
    Profile* otr = profile_->GetPrimaryOTRProfile(/*create_if_needed=*/true);
    if (!otr) {
      return;
    }
    ApplyPrivateDefaults(otr);
    // Hand the geometry to Browser::Create rather than fixing it up after.
    // A new Browser restores its own saved window placement during creation,
    // which lands AFTER any SetBounds() we do here — so the private window came
    // up at its own remembered size instead of taking over this one's.
    Browser::CreateParams params(otr, /*user_gesture=*/true);
    params.initial_bounds = bounds;
    params.initial_show_state =
        maximized ? ui::mojom::WindowShowState::kMaximized
                  : ui::mojom::WindowShowState::kNormal;
    private_browser_ = Browser::Create(params);
    if (!private_browser_) {
      return;
    }
    chrome::AddTabAt(private_browser_, GURL(), -1, /*foreground=*/true);
    if (views::Widget* pw = WidgetFor(private_browser_)) {
      pw->AddObserver(this);
    }
  }

  // Track the window we're hiding so closing the private one can restore it,
  // and so we notice if it is destroyed while off screen.
  if (return_to_ != from) {
    StopObserving(WidgetFor(return_to_));
    return_to_ = from;
    from_widget->AddObserver(this);
  }

  // Seamless swap: show the private window taking over `from`'s footprint, then
  // hide `from`. Shared with the in-window Profile switcher.
  zephyrus::SwapWindows(private_browser_, from, bounds, maximized);
}

void ZephyrusPrivateWorkspace::Leave() {
  views::Widget* private_widget = WidgetFor(private_browser_);
  if (!private_widget || !return_to_) {
    return;
  }
  const gfx::Rect bounds = private_widget->GetWindowBoundsInScreen();
  const bool maximized = private_widget->IsMaximized();
  zephyrus::SwapWindows(return_to_, private_browser_, bounds, maximized);
  // Off screen means locked again: coming back costs another unlock.
  locked_ = true;
}

void ZephyrusPrivateWorkspace::OnWidgetDestroying(views::Widget* widget) {
  widget->RemoveObserver(this);

  if (widget == WidgetFor(private_browser_)) {
    // The private window was closed. Its OTR profile tears itself down, which
    // is the spec's automatic session cleanup. Bring back whatever we hid, or
    // the user is left staring at no window at all.
    private_browser_ = nullptr;
    locked_ = true;
    if (return_to_) {
      if (views::Widget* back = WidgetFor(return_to_); back && !back->IsVisible()) {
        back->Show();
        back->Activate();
      }
      StopObserving(WidgetFor(return_to_));
      return_to_ = nullptr;
    }
    return;
  }

  if (widget == WidgetFor(return_to_)) {
    // The window we hid was closed underneath us; nothing to return to.
    return_to_ = nullptr;
  }
}

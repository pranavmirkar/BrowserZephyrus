// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ---------------------------------------------------------------------------
// ZEPHYRUS PROFILES BACKEND - RETAINED, NOT BUILT
//
// The profiles feature's user interface was withdrawn pending Google auth. This
// file is the BACKEND: profile creation, naming, and the in-window swap. It is
// deliberately kept in the tree and removed from chrome/browser/ui/BUILD.gn so
// it does not compile, rather than deleted -- it is the logic the Google auth
// work will build on.
//
// To bring the feature back: restore the four
// views/frame/zephyrus_profile_{switcher,dialog}.{h,cc} entries in
// chrome/browser/ui/BUILD.gn, then remove the `#if 0` blocks marked
// "ZEPHYRUS PROFILES FRONTEND - DISABLED" in views/toolbar/toolbar_view.{h,cc}
// and views/frame/browser_view.cc.
//
// Known caveat for whoever revives it: PW-6 is still open, and
// [[zephyrus-mixed-profile-window]] records that the session service tracks
// state per WINDOW, so an in-window profile swap can write private-workspace
// tabs to disk. Do not re-enable the swap path without resolving that first.
// ---------------------------------------------------------------------------

#include "chrome/browser/ui/views/frame/zephyrus_profile_switcher.h"

#include "base/functional/bind.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_profile_dialog.h"
#include "chrome/browser/ui/views/frame/zephyrus_window_swap.h"
#include "components/prefs/pref_service.h"
#include "ui/base/mojom/window_show_state.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

// A live (visible or hidden) tabbed window hosting `target`, or null. This tree
// replaced the global BrowserList with a per-profile collection; a hidden window
// still belongs to its profile's collection, so this doubles as the "warm
// window" lookup for instant switch-back.
Browser* FindWindowForProfile(Profile* target) {
  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(target);
  if (!collection) {
    return nullptr;
  }
  BrowserWindowInterface* window =
      collection->FindTabbedBrowser(/*match_original_profiles=*/false);
  return window ? window->GetBrowserForMigrationOnly() : nullptr;
}

}  // namespace

// static
ZephyrusProfileSwitcher* ZephyrusProfileSwitcher::GetInstance() {
  static base::NoDestructor<ZephyrusProfileSwitcher> instance;
  return instance.get();
}

ZephyrusProfileSwitcher::ZephyrusProfileSwitcher() = default;
ZephyrusProfileSwitcher::~ZephyrusProfileSwitcher() = default;

void ZephyrusProfileSwitcher::SwitchTo(const base::FilePath& profile_path,
                                       Browser* from) {
  if (!from || profile_path.empty()) {
    return;
  }
  // Already on this profile: nothing to do.
  if (from->profile() && from->profile()->GetPath() == profile_path) {
    return;
  }
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile_manager) {
    return;
  }
  // Loaded already: swap synchronously (this is the common, instant path).
  if (Profile* target = profile_manager->GetProfileByPath(profile_path)) {
    SwapInto(target, from);
    return;
  }
  // Cold profile: load it, then swap. `from` is weak — loading is async.
  profiles::LoadProfileAsync(
      profile_path,
      base::BindOnce(&ZephyrusProfileSwitcher::OnProfileReady,
                     base::Unretained(this), from->AsWeakPtr()));
}

void ZephyrusProfileSwitcher::AddProfile(Browser* from) {
  if (!from) {
    return;
  }
  // Ask for a name FIRST. Nothing is created if the user dismisses the dialog —
  // an unnamed profile appearing from a stray click is exactly what we don't
  // want.
  ZephyrusProfileDialog::Show(
      ZephyrusProfileDialog::Mode::kAddProfile, from,
      base::BindOnce(&ZephyrusProfileSwitcher::CreateProfileNamed,
                     base::Unretained(this), from->AsWeakPtr()));
}

void ZephyrusProfileSwitcher::MaybeShowFirstRunSetup(Browser* browser) {
  if (!browser || browser->profile()->IsOffTheRecord()) {
    return;
  }
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state ||
      local_state->GetBoolean(zephyrus::kProfileSetupCompletePref)) {
    return;
  }
  // Chromium always ships a "Default" profile, so there is nothing to create on
  // first run — we name the one the user is already in.
  ZephyrusProfileDialog::Show(
      ZephyrusProfileDialog::Mode::kFirstRun, browser,
      base::BindOnce(&ZephyrusProfileSwitcher::NameExistingProfile,
                     base::Unretained(this), browser->AsWeakPtr()));
}

void ZephyrusProfileSwitcher::CreateProfileNamed(base::WeakPtr<Browser> from,
                                                 const std::u16string& name,
                                                 size_t avatar_index) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile_manager || name.empty()) {
    return;
  }
  const base::FilePath path =
      profile_manager->GenerateNextProfileDirectoryPath();
  profile_manager->CreateProfileAsync(
      path, base::BindOnce(&ZephyrusProfileSwitcher::OnCreatedProfileReady,
                           base::Unretained(this), name, avatar_index, from));
}

void ZephyrusProfileSwitcher::NameExistingProfile(
    base::WeakPtr<Browser> browser,
    const std::u16string& name,
    size_t avatar_index) {
  if (!browser || name.empty()) {
    return;
  }
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (profile_manager) {
    ProfileAttributesEntry* entry =
        profile_manager->GetProfileAttributesStorage()
            .GetProfileAttributesWithPath(browser->profile()->GetPath());
    if (entry) {
      entry->SetLocalProfileName(name, /*is_default_name=*/false);
      entry->SetAvatarIconIndex(avatar_index);
    }
  }
  if (PrefService* local_state = g_browser_process->local_state()) {
    local_state->SetBoolean(zephyrus::kProfileSetupCompletePref, true);
  }
}

void ZephyrusProfileSwitcher::OnCreatedProfileReady(std::u16string name,
                                                    size_t avatar_index,
                                                    base::WeakPtr<Browser> from,
                                                    Profile* profile) {
  if (!profile) {
    return;
  }
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (profile_manager) {
    ProfileAttributesEntry* entry =
        profile_manager->GetProfileAttributesStorage()
            .GetProfileAttributesWithPath(profile->GetPath());
    if (entry) {
      entry->SetLocalProfileName(name, /*is_default_name=*/false);
      entry->SetAvatarIconIndex(avatar_index);
    }
  }
  if (from) {
    SwapInto(profile, from.get());
  }
}

void ZephyrusProfileSwitcher::OnProfileReady(base::WeakPtr<Browser> from,
                                             Profile* profile) {
  if (!from || !profile) {
    return;
  }
  SwapInto(profile, from.get());
}

void ZephyrusProfileSwitcher::SwapInto(Profile* target, Browser* from) {
  if (!target || !from) {
    return;
  }
  BrowserView* from_view = BrowserView::GetBrowserViewForBrowser(from);
  views::Widget* from_widget = from_view ? from_view->GetWidget() : nullptr;
  if (!from_widget) {
    return;
  }
  const gfx::Rect bounds = from_widget->GetWindowBoundsInScreen();
  const bool maximized = from_widget->IsMaximized();

  Browser* target_browser = FindWindowForProfile(target);
  if (!target_browser) {
    // Cold profile with no window yet: create one that comes up already sized to
    // this window's footprint (Browser::CreateParams restores placement during
    // construction, which would otherwise override a later SetBounds()).
    Browser::CreateParams params(target, /*user_gesture=*/true);
    params.initial_bounds = bounds;
    params.initial_show_state =
        maximized ? ui::mojom::WindowShowState::kMaximized
                  : ui::mojom::WindowShowState::kNormal;
    target_browser = Browser::Create(params);
    if (!target_browser) {
      return;
    }
    chrome::AddTabAt(target_browser, GURL(), -1, /*foreground=*/true);
  }
  if (target_browser == from) {
    return;
  }
  zephyrus::SwapWindows(target_browser, from, bounds, maximized);
}

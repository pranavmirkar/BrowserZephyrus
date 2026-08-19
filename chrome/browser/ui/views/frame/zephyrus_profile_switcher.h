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

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PROFILE_SWITCHER_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PROFILE_SWITCHER_H_

#include <stddef.h>

#include <string>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"

class Browser;
class Profile;

namespace zephyrus {

// Local-state pref: whether the one-time profile setup has been completed.
// Registered by the same literal in browser_prefs.cc to avoid a
// //chrome/browser -> views layering dependency there.
inline constexpr char kProfileSetupCompletePref[] =
    "zephyrus.profile.setup_complete";

}  // namespace zephyrus

// Zephyrus in-window profile switcher.
//
// Chrome switches profiles by opening a whole separate window; Zephyrus switches
// the *current* window to another profile in place, using the shared seamless
// window swap (see zephyrus_window_swap.h) — the same mechanism the Private
// Workspace uses. A `Browser` is permanently bound to one `Profile`, so
// "in-window" means swapping which per-profile window occupies the footprint,
// not one window changing profile.
//
// Hidden profile windows are left alive in the BrowserList, so switching back is
// instant and lands exactly where you left off. (An idle-discard policy to
// reclaim their memory is a later phase.)
//
// Process-wide singleton — profiles are a global concept.
class ZephyrusProfileSwitcher {
 public:
  static ZephyrusProfileSwitcher* GetInstance();

  ZephyrusProfileSwitcher(const ZephyrusProfileSwitcher&) = delete;
  ZephyrusProfileSwitcher& operator=(const ZephyrusProfileSwitcher&) = delete;

  // Switches window `from` to the profile at `profile_path`: loads the profile
  // if needed, reuses its existing (possibly hidden) window or creates one at
  // `from`'s footprint, then swaps. No-op if `from` is already that profile.
  void SwitchTo(const base::FilePath& profile_path, Browser* from);

  // Asks for a name first (ZephyrusProfileDialog), then creates the profile and
  // switches `from` into it. Dismissing the dialog creates nothing.
  void AddProfile(Browser* from);

  // On the very first launch, asks the user to name their profile. No-op once
  // setup has been completed (tracked in local state), so this runs once per
  // install rather than on every startup — Zephyrus deliberately has no
  // profile picker on launch.
  void MaybeShowFirstRunSetup(Browser* browser);

 private:
  friend class base::NoDestructor<ZephyrusProfileSwitcher>;
  ZephyrusProfileSwitcher();
  ~ZephyrusProfileSwitcher();

  // Creates a profile named `name` and switches `from` into it. Called only
  // after the user confirms the dialog. `name` comes last: it is the value the
  // dialog supplies, with `from` bound up front.
  void CreateProfileNamed(base::WeakPtr<Browser> from,
                          const std::u16string& name,
                          size_t avatar_index);

  // Applies `name` to the profile `browser` is already in (first-run setup) and
  // marks setup complete.
  void NameExistingProfile(base::WeakPtr<Browser> browser,
                           const std::u16string& name,
                           size_t avatar_index);

  // Continuation once the target profile object is loaded/created. `from` is
  // weak because loading is async and the originating window can close.
  void OnProfileReady(base::WeakPtr<Browser> from, Profile* profile);

  // As OnProfileReady, but also labels the freshly created profile.
  void OnCreatedProfileReady(std::u16string name,
                             size_t avatar_index,
                             base::WeakPtr<Browser> from,
                             Profile* profile);
  // Reuse-or-create `target`'s window at `from`'s footprint, then swap.
  void SwapInto(Profile* target, Browser* from);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PROFILE_SWITCHER_H_

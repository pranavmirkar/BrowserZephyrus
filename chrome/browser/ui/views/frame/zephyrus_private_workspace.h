// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PRIVATE_WORKSPACE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PRIVATE_WORKSPACE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "chrome/browser/device_reauth/win/authenticator_win.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class Profile;

namespace content {
class WebContents;
}

// Zephyrus Private Workspace: an isolated browsing environment that replaces
// Incognito Mode.
//
// It is backed by Chromium's off-the-record Profile, which already provides
// the separate cookie jar, storage, service workers, absent history, and
// destroy-on-close cleanup the feature spec asks for. What this class adds is
// the *workspace* illusion: the private window takes over the originating
// window's exact bounds while that window hides, so switching reads as an
// in-place workspace change rather than opening a second window.
//
// Why two windows rather than private tabs inside one window: a Browser (and
// so its TabStripModel) is bound to a single Profile, and window-scoped UI is
// bound with it. LocationBarView in particular caches `profile_` at
// construction, so a private tab in a regular window would autocomplete
// against the REGULAR profile — leaking private typing into normal history and
// search suggestions. Two Browsers keeps the omnibox, downloads, permissions,
// and extensions each bound to the correct profile.
//
// Owned by the regular Profile; there is at most one private window per
// profile.
class ZephyrusPrivateWorkspace : public base::SupportsUserData::Data,
                                 public views::WidgetObserver {
 public:
  // `profile` must be the regular (non-OTR) profile. Creates the controller on
  // first use.
  static ZephyrusPrivateWorkspace* GetForProfile(Profile* profile);

  explicit ZephyrusPrivateWorkspace(Profile* profile);
  ZephyrusPrivateWorkspace(const ZephyrusPrivateWorkspace&) = delete;
  ZephyrusPrivateWorkspace& operator=(const ZephyrusPrivateWorkspace&) = delete;
  ~ZephyrusPrivateWorkspace() override;

  // True if `browser` is the private window.
  static bool IsPrivate(const Browser* browser);

  // Whether entering requires an OS unlock (Windows Hello / screen lock).
  // Stored on the regular profile — the private one does not survive a session.
  static bool IsLockEnabled(Profile* profile);
  static void SetLockEnabled(Profile* profile, bool enabled);

  // Shows the private window at `from`'s bounds and hides `from`. Creates the
  // private window (and its first tab) on first call.
  //
  // If the lock is on and the workspace is currently locked, this prompts for
  // authentication FIRST and does nothing unless it succeeds — the window is
  // not created, so a failed or cancelled prompt reveals nothing.
  void Enter(Browser* from);

  // Returns to the window we came from, hiding the private window. The private
  // window keeps its tabs — this is a switch, not a close.
  void Leave();

  // PW-6: opens an off-the-record tab inside `browser`'s own tab strip, rather
  // than in a separate private window. This is the real in-window mechanism —
  // one window holding tabs from two profiles.
  //
  // Goes through the same unlock gate as Enter(): both are doors into private
  // data, and a lock that only guards one of two doors is decorative.
  //
  // Safe to call only once the per-tab guards are in place: the session service
  // skips OTR tabs (session_service_base.cc) and the omnibox resolves its
  // profile from the active tab (ChromeAutocompleteProviderClient).
  void OpenPrivateTabIn(Browser* browser);

  // True while the private window exists (whether or not it is on screen).
  bool IsOpen() const { return private_browser_ != nullptr; }

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void StopObserving(views::Widget* widget);

  // The single unlock gate. Runs `action` immediately when no unlock is
  // required, otherwise prompts and runs it only on success. Every door into
  // private data goes through here — Enter() and OpenPrivateTabIn() both.
  void RunWhenUnlocked(Browser* from,
                       base::OnceCallback<void(Browser*)> action);
  // The actual swap, once any required unlock has succeeded.
  void EnterUnlocked(Browser* from);
  // In-window private tab, once any required unlock has succeeded.
  void OpenPrivateTabUnlocked(Browser* browser);
  // `from` is weak: the prompt is modal and async, and the window behind it can
  // be closed while it is up.
  void OnAuthComplete(base::WeakPtr<Browser> from,
                      base::OnceCallback<void(Browser*)> action,
                      bool success);

  raw_ptr<Profile> profile_;
  std::unique_ptr<AuthenticatorWinInterface> authenticator_;
  // Re-armed whenever the private window is hidden or closed, so coming back
  // always costs another unlock.
  bool locked_ = true;
  // Guards against a second prompt while one is already on screen.
  bool authenticating_ = false;
  raw_ptr<Browser> private_browser_ = nullptr;
  // The window we hid on Enter(), to be re-shown on Leave() or when the
  // private window is closed. Cleared if it is destroyed while hidden.
  raw_ptr<Browser> return_to_ = nullptr;
  base::WeakPtrFactory<ZephyrusPrivateWorkspace> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_PRIVATE_WORKSPACE_H_

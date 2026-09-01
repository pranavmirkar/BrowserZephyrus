// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_ZEPHYRUS_SETUP_INSTALLER_RUNNER_H_
#define CHROME_INSTALLER_ZEPHYRUS_SETUP_INSTALLER_RUNNER_H_

#include <windows.h>

#include <atomic>
#include <string>
#include <thread>

namespace zephyrus_setup {

// Runs the real installer underneath the window.
//
// SHAPE
// -----
// zephyrus_setup.exe carries Chromium's mini_installer.exe as a resource,
// injected after both are built (see tools/package_installer.py -- the payload
// is a build output, so it cannot be named by the .rc at compile time). At run
// time it is written to a temp directory and launched with the flags that keep
// it silent, and this class watches it.
//
// PROGRESS IS REPORTED, NOT INVENTED
// ----------------------------------
// setup.exe emits no percentage, no file counts, nothing. The honest options
// were a marquee that says nothing or a bar driven by things we can actually
// observe, and this takes the second: bytes written while unpacking, then
// filesystem milestones appearing on disk (install directory, versioned
// directory, chrome.exe), then the process exit code.
//
// Between milestones the bar creeps toward the next one but never reaches it,
// so it always trails what is known rather than leading it. That is the whole
// discipline: the bar may be vague, but it is never ahead of the truth.
class InstallerRunner {
 public:
  enum class Phase {
    kIdle,
    kExtracting,   // writing the payload out of our own resources
    kInstalling,   // setup.exe is running
    kFinishing,    // it exited; confirming the browser is actually there
    kDone,
    kFailed,
  };

  InstallerRunner();
  InstallerRunner(const InstallerRunner&) = delete;
  InstallerRunner& operator=(const InstallerRunner&) = delete;
  ~InstallerRunner();

  // Begins on a worker thread. The UI thread polls the getters below; nothing
  // blocks the message loop, because a frozen installer window is how a healthy
  // install gets killed by an impatient user.
  void Start(HINSTANCE instance);

  Phase phase() const { return phase_.load(std::memory_order_relaxed); }
  float progress() const { return progress_.load(std::memory_order_relaxed); }

  // Why it failed, in words a person can act on. Empty until kFailed.
  std::wstring error() const;

  // Full path to the installed browser. Valid once kDone.
  static std::wstring InstalledExePath();

  // Post-install actions, both driven from the finished-state buttons.
  static bool Launch();
  static bool CreateDesktopShortcut();

 private:
  void Run(HINSTANCE instance);
  bool ExtractPayload(HINSTANCE instance, std::wstring* payload_path);
  bool WriteInitialPreferences(std::wstring* prefs_path);
  void Fail(const wchar_t* message);

  std::atomic<Phase> phase_{Phase::kIdle};
  std::atomic<float> progress_{0.f};
  std::wstring error_;
  mutable CRITICAL_SECTION error_lock_;
  std::wstring work_dir_;
  std::thread thread_;
};

}  // namespace zephyrus_setup

#endif  // CHROME_INSTALLER_ZEPHYRUS_SETUP_INSTALLER_RUNNER_H_

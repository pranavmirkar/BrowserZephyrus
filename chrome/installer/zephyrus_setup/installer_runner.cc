// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/zephyrus_setup/installer_runner.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <span>
#include <vector>

#include "chrome/installer/zephyrus_setup/setup_resource_ids.h"

namespace zephyrus_setup {

namespace {

using Microsoft::WRL::ComPtr;

// Where mini_installer puts a per-user install, and where we therefore look to
// confirm the install actually happened.
constexpr wchar_t kInstallSubdir[] = L"\\Zephyrus\\Application";
constexpr wchar_t kExeName[] = L"\\chrome.exe";
constexpr wchar_t kShortcutName[] = L"\\Zephyrus.lnk";

std::wstring LocalAppData() {
  PWSTR path = nullptr;
  if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                    &path))) {
    return std::wstring();
  }
  std::wstring result(path);
  ::CoTaskMemFree(path);
  return result;
}

std::wstring DesktopDir() {
  PWSTR path = nullptr;
  if (FAILED(::SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) {
    return std::wstring();
  }
  std::wstring result(path);
  ::CoTaskMemFree(path);
  return result;
}

bool Exists(const std::wstring& path) {
  return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// True when `dir` contains a subdirectory that looks like a version number.
// mini_installer creates Application\<version>\ as one of its last steps, so
// its appearance is a real signal that the copy is well underway.
bool HasVersionedDir(const std::wstring& dir) {
  WIN32_FIND_DATAW found = {};
  const std::wstring pattern = dir + L"\\*";
  HANDLE handle = ::FindFirstFileW(pattern.c_str(), &found);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  bool result = false;
  do {
    if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      continue;
    }
    // A version directory begins with a digit; "." and ".." do not, and neither
    // does anything else mini_installer leaves here.
    if (found.cFileName[0] >= L'0' && found.cFileName[0] <= L'9') {
      result = true;
      break;
    }
  } while (::FindNextFileW(handle, &found));
  ::FindClose(handle);
  return result;
}

}  // namespace

InstallerRunner::InstallerRunner() {
  ::InitializeCriticalSection(&error_lock_);
}

InstallerRunner::~InstallerRunner() {
  if (thread_.joinable()) {
    thread_.join();
  }
  ::DeleteCriticalSection(&error_lock_);
}

std::wstring InstallerRunner::error() const {
  ::EnterCriticalSection(&error_lock_);
  const std::wstring copy = error_;
  ::LeaveCriticalSection(&error_lock_);
  return copy;
}

void InstallerRunner::Fail(const wchar_t* message) {
  ::EnterCriticalSection(&error_lock_);
  error_ = message;
  ::LeaveCriticalSection(&error_lock_);
  phase_.store(Phase::kFailed, std::memory_order_relaxed);
}

// static
std::wstring InstallerRunner::InstalledExePath() {
  const std::wstring base = LocalAppData();
  if (base.empty()) {
    return std::wstring();
  }
  return base + kInstallSubdir + kExeName;
}

void InstallerRunner::Start(HINSTANCE instance) {
  thread_ = std::thread(&InstallerRunner::Run, this, instance);
}

bool InstallerRunner::ExtractPayload(HINSTANCE instance,
                                     std::wstring* payload_path) {
  HRSRC found = ::FindResource(
      instance, MAKEINTRESOURCE(IDR_PAYLOAD_MINI_INSTALLER), RT_RCDATA);
  if (!found) {
    // The UI runs standalone during development; only a packaged build carries
    // the payload. Saying so plainly beats a generic failure.
    Fail(L"This copy of the installer has no program files inside it.");
    return false;
  }
  HGLOBAL handle = ::LoadResource(instance, found);
  // Held as a span rather than a bare pointer: Chromium builds with
  // -Wunsafe-buffer-usage, and `data + offset` on a raw pointer is exactly the
  // arithmetic it rejects. The bound travels with the data instead.
  const void* raw = handle ? ::LockResource(handle) : nullptr;
  const DWORD size = ::SizeofResource(instance, found);
  if (!raw || size == 0) {
    Fail(L"The program files inside this installer could not be read.");
    return false;
  }
  const std::span<const BYTE> data(static_cast<const BYTE*>(raw), size);

  wchar_t temp[MAX_PATH] = {};
  if (!::GetTempPathW(MAX_PATH, temp)) {
    Fail(L"Windows did not provide a temporary folder to unpack into.");
    return false;
  }
  work_dir_ = std::wstring(temp) + L"zephyrus_setup_" +
              std::to_wstring(::GetCurrentProcessId());
  ::CreateDirectoryW(work_dir_.c_str(), nullptr);

  *payload_path = work_dir_ + L"\\mini_installer.exe";
  HANDLE file =
      ::CreateFileW(payload_path->c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    Fail(L"The installer could not write to the temporary folder.");
    return false;
  }

  // Written in chunks so the bar can move on real bytes. This is the only
  // phase where genuine fine-grained progress exists, so it is worth using.
  constexpr DWORD kChunk = 1u << 20;
  DWORD written_total = 0;
  bool ok = true;
  while (written_total < size) {
    const DWORD chunk = std::min(kChunk, size - written_total);
    DWORD written = 0;
    const std::span<const BYTE> piece = data.subspan(written_total, chunk);
    if (!::WriteFile(file, piece.data(), chunk, &written, nullptr) ||
        written != chunk) {
      ok = false;
      break;
    }
    written_total += written;
    progress_.store(0.20f * static_cast<float>(written_total) /
                        static_cast<float>(size),
                    std::memory_order_relaxed);
  }
  ::CloseHandle(file);
  if (!ok) {
    Fail(L"The installer ran out of space while unpacking.");
    return false;
  }
  return true;
}

bool InstallerRunner::WriteInitialPreferences(std::wstring* prefs_path) {
  // The desktop shortcut is suppressed here and offered as a button after the
  // install instead. Chromium creates it during setup, so the only way to make
  // the offer honest -- rather than a checkbox toggling something that already
  // happened -- is to turn it off now and create it ourselves on request.
  static constexpr char kPrefs[] =
      "{\"distribution\":{"
      "\"do_not_create_desktop_shortcut\":true,"
      "\"do_not_create_quick_launch_shortcut\":true,"
      "\"do_not_launch_chrome\":true,"
      "\"make_chrome_default\":false,"
      "\"suppress_first_run_bubble\":true"
      "}}";

  *prefs_path = work_dir_ + L"\\initial_preferences";
  HANDLE file = ::CreateFileW(prefs_path->c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const bool ok =
      ::WriteFile(file, kPrefs, sizeof(kPrefs) - 1, &written, nullptr) != 0;
  ::CloseHandle(file);
  return ok && written == sizeof(kPrefs) - 1;
}

void InstallerRunner::Run(HINSTANCE instance) {
  phase_.store(Phase::kExtracting, std::memory_order_relaxed);

  std::wstring payload;
  if (!ExtractPayload(instance, &payload)) {
    return;
  }

  std::wstring prefs;
  if (!WriteInitialPreferences(&prefs)) {
    Fail(L"The installer could not prepare its settings file.");
    return;
  }

  // mini_installer forwards every flag after its own program name straight to
  // setup.exe, which is how these reach the code that acts on them.
  std::wstring command = L"\"" + payload +
                         L"\" --do-not-launch-chrome --installerdata=\"" +
                         prefs + L"\"";

  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');

  if (!::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    Fail(L"Windows would not start the installer.");
    return;
  }

  phase_.store(Phase::kInstalling, std::memory_order_relaxed);
  const std::wstring app_dir = LocalAppData() + kInstallSubdir;
  const std::wstring exe = InstalledExePath();

  // Milestones, each a thing that is actually on disk. `ceiling` is what the
  // bar is allowed to approach; it only rises when one of them is reached, so
  // the creep between them can never overtake what is known.
  float floor_value = 0.20f;
  float ceiling = 0.42f;
  bool seen_dir = false, seen_version = false, seen_exe = false;

  for (;;) {
    const DWORD wait = ::WaitForSingleObject(pi.hProcess, 200);

    if (!seen_dir && Exists(app_dir)) {
      seen_dir = true;
      floor_value = 0.45f;
      ceiling = 0.62f;
    }
    if (!seen_version && seen_dir && HasVersionedDir(app_dir)) {
      seen_version = true;
      floor_value = 0.65f;
      ceiling = 0.82f;
    }
    if (!seen_exe && Exists(exe)) {
      seen_exe = true;
      floor_value = 0.85f;
      ceiling = 0.94f;
    }

    // Asymptotic creep: closes a fraction of the remaining gap each tick, so it
    // slows as it approaches and never arrives. A bar that sits still looks
    // hung; one that reaches 100% early and waits looks like a lie.
    const float shown = progress_.load(std::memory_order_relaxed);
    const float next = std::max(floor_value, shown + (ceiling - shown) * 0.06f);
    progress_.store(std::min(next, ceiling), std::memory_order_relaxed);

    if (wait == WAIT_OBJECT_0) {
      break;
    }
  }

  DWORD exit_code = 1;
  ::GetExitCodeProcess(pi.hProcess, &exit_code);
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);

  phase_.store(Phase::kFinishing, std::memory_order_relaxed);

  // Trust the filesystem over the exit code. Chromium's installer returns a
  // range of non-zero "already up to date" style results that are not failures,
  // and the question a user actually has is whether the browser is there.
  if (!Exists(exe)) {
    Fail(exit_code == 0
             ? L"The installer finished but Zephyrus is not on this PC."
             : L"Zephyrus could not be installed. Your PC has not been "
               L"changed.");
    return;
  }

  progress_.store(1.f, std::memory_order_relaxed);
  phase_.store(Phase::kDone, std::memory_order_relaxed);

  // The unpacked payload is several hundred megabytes of temp file that nothing
  // will ever clean up otherwise.
  ::DeleteFileW(payload.c_str());
  ::DeleteFileW(prefs.c_str());
  ::RemoveDirectoryW(work_dir_.c_str());
}

// static
bool InstallerRunner::Launch() {
  const std::wstring exe = InstalledExePath();
  if (exe.empty() || !Exists(exe)) {
    return false;
  }
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  std::wstring command = L"\"" + exe + L"\"";
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  if (!::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr,
                        FALSE, 0, nullptr, nullptr, &si, &pi)) {
    return false;
  }
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);
  return true;
}

// static
bool InstallerRunner::CreateDesktopShortcut() {
  const std::wstring exe = InstalledExePath();
  const std::wstring desktop = DesktopDir();
  if (exe.empty() || desktop.empty() || !Exists(exe)) {
    return false;
  }

  ComPtr<IShellLinkW> link;
  if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) {
    return false;
  }
  link->SetPath(exe.c_str());
  link->SetDescription(L"Zephyrus");
  // Working directory set to the install folder: without it a shortcut
  // inherits whatever directory happened to be current when it was launched.
  const size_t slash = exe.find_last_of(L'\\');
  if (slash != std::wstring::npos) {
    link->SetWorkingDirectory(exe.substr(0, slash).c_str());
  }

  ComPtr<IPersistFile> file;
  if (FAILED(link.As(&file))) {
    return false;
  }
  const std::wstring path = desktop + kShortcutName;
  return SUCCEEDED(file->Save(path.c_str(), TRUE));
}

}  // namespace zephyrus_setup

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

// Installs Inter for the CURRENT USER, so the browser can use it too.
//
// The setup window already renders itself in Inter, but privately: it builds an
// in-memory DirectWrite font set that lives and dies with this process. Nothing
// else on the machine can see those bytes, which is why the browser could only
// use Inter on machines where the user happened to have installed it.
//
// PER-USER, deliberately. A machine-wide font needs elevation, and this
// installer does not have it -- mini_installer puts Zephyrus under
// LocalAppData. A per-user font needs no rights beyond writing to the user's
// own profile, and Windows has supported it since 1809.
//
// Three steps, all of them required:
//   1. the .ttf lands in the user's Fonts directory,
//   2. a registry value under HKCU names it -- and unlike the machine-wide
//      hive this one must hold the FULL PATH, not a bare filename,
//   3. WM_FONTCHANGE tells already-running programs to re-enumerate, so the
//      font works without a sign-out.
//
// BEST EFFORT. Every failure path here returns quietly: a browser that
// installed correctly must not be reported as failed because a font did not
// copy. Zephyrus falls back to the platform UI font on its own.
bool WriteResourceTo(HINSTANCE instance, int id, const std::wstring& path) {
  HRSRC found = ::FindResource(instance, MAKEINTRESOURCE(id), RT_RCDATA);
  HGLOBAL handle = found ? ::LoadResource(instance, found) : nullptr;
  const void* data = handle ? ::LockResource(handle) : nullptr;
  const DWORD size = found ? ::SizeofResource(instance, found) : 0;
  if (!data || size == 0) {
    return false;
  }
  HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const bool ok =
      ::WriteFile(file, data, size, &written, nullptr) != 0 && written == size;
  ::CloseHandle(file);
  return ok;
}

void InstallInterForCurrentUser(HINSTANCE instance) {
  const std::wstring local = LocalAppData();
  if (local.empty()) {
    return;
  }
  const std::wstring dir = local + L"\\Microsoft\\Windows\\Fonts";
  ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);

  HKEY key = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
    return;
  }

  struct Face {
    int resource;
    const wchar_t* file;
    // The registry NAME is what applications see in a font list. It must carry
    // the "(TrueType)" suffix or Windows treats the entry as malformed.
    const wchar_t* value;
  };
  static constexpr Face kFaces[] = {
      {IDR_FONT_INTER_REGULAR, L"Inter-Regular.ttf", L"Inter (TrueType)"},
      {IDR_FONT_INTER_SEMIBOLD, L"Inter-SemiBold.ttf",
       L"Inter SemiBold (TrueType)"},
  };

  bool any = false;
  for (const Face& face : kFaces) {
    const std::wstring path = dir + L"\\" + face.file;
    if (!WriteResourceTo(instance, face.resource, path)) {
      continue;
    }
    // Full path, because HKCU font entries are not resolved against the
    // system Fonts directory the way HKLM entries are.
    ::RegSetValueExW(key, face.value, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(path.c_str()),
                     static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
    ::AddFontResourceExW(path.c_str(), FR_NOT_ENUM, nullptr);
    any = true;
  }
  ::RegCloseKey(key);

  // The OFL requires the licence to accompany the font. It travelled inside
  // this .exe until now; once the .ttf files live on disk on their own, the
  // licence has to sit beside them or the obligation is no longer met.
  if (any) {
    WriteResourceTo(instance, IDR_LICENSE_INTER_OFL,
                    dir + L"\\Inter-OFL.txt");
    ::SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG,
                          1000, nullptr);
  }
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

  // Inter, now that the browser is definitely on disk. AFTER the success check
  // on purpose: a font installed beside a browser that failed to install is
  // litter in the user's profile.
  //
  // Not checked, not reported. See InstallInterForCurrentUser -- the browser
  // works without it.
  InstallInterForCurrentUser(instance);

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

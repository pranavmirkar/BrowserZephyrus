// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Zephyrus installer window.
//
// THE DESIGN, AND WHY IT IS DELIBERATELY ORDINARY
// ----------------------------------------------
// This is the installer layout everyone already knows: a system title bar, an
// icon on the left, a line of text beside it, a progress bar along the bottom.
// It went through a far more elaborate iteration -- frameless, dark, circular
// progress, CRT texture -- and that version was more striking and worse. An
// installer's whole job is to be over quickly and to worry nobody. A window
// that asks to be admired is asking for attention it has not earned.
//
// So the frame is conventional on purpose. The one thing that is ours is the
// mascot: where this window would normally show a static product logo, the
// character works, carries, thinks, and finishes. That single substitution is
// the entire idea, and it only reads because everything around it is quiet.
//
// STAGE 1 -- PRESENTATION ONLY. States are driven by a demo clock; the
// bootstrapper lands next. Run with --demo to watch every state.

#include <windows.h>

#include <math.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <d2d1.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "chrome/installer/zephyrus_setup/installer_runner.h"
#include "chrome/installer/zephyrus_setup/mascot.h"
#include "chrome/installer/zephyrus_setup/setup_resource_ids.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace zephyrus_setup {
namespace {

using Microsoft::WRL::ComPtr;

// --- Layout, in DIPs -------------------------------------------------------
constexpr float kWindowW = 520.f;
constexpr float kWindowH = 190.f;

// The mascot's panel, where a static product icon would normally sit.
constexpr float kArtBoxX = 26.f;
constexpr float kArtBoxY = 24.f;
constexpr float kArtBoxSize = 76.f;
// The mascot's own coordinate space (its source viewBox), scaled into the box.
constexpr float kArtW = 160.f;
constexpr float kArtH = 150.f;

// Text beside the character, near the top -- the same place "Installing,
// please wait..." has lived in installers for decades.
constexpr float kTextX = 124.f;
constexpr float kTitleY = 36.f;
constexpr float kStatusY = 64.f;
constexpr float kTextRight = kWindowW - 26.f;

// The bar: full width, along the bottom.
constexpr float kBarX = 26.f;
constexpr float kBarY = 134.f;
constexpr float kBarW = kWindowW - 52.f;
constexpr float kBarH = 20.f;

// Buttons take the bar's row once the install is done.
constexpr float kButtonW = 134.f;
constexpr float kButtonH = 30.f;
constexpr float kButtonGap = 8.f;
constexpr float kButtonsY = 129.f;

// --- Palette ---------------------------------------------------------------
// A light surface, because that is what this kind of window is. The mascot's
// blue was chosen against dark and holds up here too: it is a mid-tone, so it
// has contrast against both.
constexpr uint32_t kSurface = 0xF2F3F5;
constexpr uint32_t kFieldBg = 0xFFFFFF;
constexpr uint32_t kBorderCol = 0xC8CCD2;
constexpr uint32_t kTextPrimary = 0x14161A;
constexpr uint32_t kTextSecondary = 0x5C636E;

D2D1_COLOR_F Rgb(uint32_t rgb, float a = 1.f) {
  return D2D1::ColorF(rgb, a);
}

// --- Easing ----------------------------------------------------------------
// Approximates cubic-bezier(0.23, 1, 0.32, 1). ease-IN is deliberately absent:
// it delays movement at the moment the eye is most attentive, so the same
// duration feels longer than it is.
float EaseOut(float t) {
  t = std::clamp(t, 0.f, 1.f);
  const float inv = 1.f - t;
  return 1.f - inv * inv * inv * inv * inv;
}

// Frame-rate independent. `v += (target - v) * k` moves at a speed that depends
// on how many frames arrived -- during an install, always the wrong assumption.
float Approach(float value, float target, float rate, float dt) {
  const float t = 1.f - expf(-rate * dt);
  return value + (target - value) * t;
}

struct Button {
  D2D1_RECT_F bounds;
  std::wstring label;
  bool primary = false;
  bool hot = false;
  bool pressed = false;
  bool done = false;
};

class SetupWindow {
 public:
  bool Create(HINSTANCE instance, bool demo);
  int RunMessageLoop();

 private:
  static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
  LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

  bool LoadEmbeddedFonts(HINSTANCE instance);
  bool EnsureDeviceResources();
  void DiscardDeviceResources();
  void Render();
  void DrawMascot();
  void DrawBar(float progress);
  void DrawButton(const Button& b);
  void DrawLabel(const std::wstring& text, IDWriteTextFormat* format,
                 D2D1_RECT_F rect, D2D1_COLOR_F color);
  void LayoutButtons();
  void OnClick(float x, float y);
  void Tick();
  void TickDemo();
  void TickInstall();

  float Snap(float dip) const { return floorf(dip * dpi_scale_) / dpi_scale_; }
  // Mouse messages arrive in PHYSICAL pixels; every bound here is in DIPs. At
  // 100% scaling they coincide, which is exactly why forgetting this conversion
  // once produced buttons that silently did nothing on a scaled display.
  float ToDip(int physical) const {
    return static_cast<float>(physical) / dpi_scale_;
  }

  HWND hwnd_ = nullptr;
  bool demo_ = false;
  HINSTANCE instance_ = nullptr;
  InstallerRunner runner_;

  ComPtr<ID2D1Factory> d2d_;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDWriteInMemoryFontFileLoader> font_loader_;
  ComPtr<IDWriteFontCollection1> fonts_;
  ComPtr<IDWriteTextFormat> title_format_;
  ComPtr<IDWriteTextFormat> body_format_;
  ComPtr<IDWriteTextFormat> button_format_;

  // Nothing happens until the user says so. The first version started the
  // install the instant the window appeared, on the reasoning that running the
  // file was already consent -- but that leaves no moment to change your mind
  // and no way to cancel, and "I double-clicked it to see what it was" is a
  // real thing people do with an installer.
  bool started_ = false;
  State state_ = State::kIdle;
  double state_started_ = 0.0;
  double now_ = 0.0;
  double opened_ = 0.0;
  float progress_ = 0.f;
  float progress_shown_ = 0.f;
  std::wstring status_ = L"Preparing";
  bool finished_ = false;
  bool failed_ = false;
  double finished_at_ = -1.0;
  bool reduced_motion_ = false;
  std::vector<Button> buttons_;
  ULONGLONG start_ticks_ = 0;
  float width_ = kWindowW;
  float height_ = kWindowH;
  float dpi_scale_ = 1.f;
};

// ---------------------------------------------------------------------------

bool SetupWindow::LoadEmbeddedFonts(HINSTANCE instance) {
  ComPtr<IDWriteFactory5> factory5;
  if (FAILED(dwrite_.As(&factory5))) {
    return false;
  }
  if (FAILED(
          factory5->CreateInMemoryFontFileLoader(font_loader_.GetAddressOf()))) {
    return false;
  }
  if (FAILED(factory5->RegisterFontFileLoader(font_loader_.Get()))) {
    return false;
  }
  ComPtr<IDWriteFontSetBuilder1> builder;
  if (FAILED(factory5->CreateFontSetBuilder(builder.GetAddressOf()))) {
    return false;
  }
  auto add = [&](int resource_id) -> bool {
    HRSRC found =
        ::FindResource(instance, MAKEINTRESOURCE(resource_id), RT_RCDATA);
    if (!found) {
      return false;
    }
    HGLOBAL handle = ::LoadResource(instance, found);
    const void* data = handle ? ::LockResource(handle) : nullptr;
    const DWORD size = ::SizeofResource(instance, found);
    if (!data || size == 0) {
      return false;
    }
    ComPtr<IDWriteFontFile> file;
    if (FAILED(font_loader_->CreateInMemoryFontFileReference(
            factory5.Get(), data, size, nullptr, file.GetAddressOf()))) {
      return false;
    }
    return SUCCEEDED(builder->AddFontFile(file.Get()));
  };
  if (!add(IDR_FONT_INTER_REGULAR) || !add(IDR_FONT_INTER_SEMIBOLD)) {
    return false;
  }
  ComPtr<IDWriteFontSet> set;
  if (FAILED(builder->CreateFontSet(set.GetAddressOf()))) {
    return false;
  }
  return SUCCEEDED(
      factory5->CreateFontCollectionFromFontSet(set.Get(), fonts_.GetAddressOf()));
}

bool SetupWindow::Create(HINSTANCE instance, bool demo) {
  demo_ = demo;

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = &SetupWindow::WndProcThunk;
  wc.hInstance = instance;
  wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"ZephyrusSetupWindow";
  wc.hbrBackground = nullptr;  // Direct2D paints every pixel.
  ::RegisterClassExW(&wc);

  // A real system title bar. The frameless version replaced it with a custom
  // strip, which meant reimplementing dragging, minimise and close -- three
  // things Windows already does correctly, for a window that lives twenty
  // seconds.
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

  const UINT dpi = ::GetDpiForSystem();
  RECT r = {0, 0, ::MulDiv(static_cast<int>(kWindowW), dpi, 96),
            ::MulDiv(static_cast<int>(kWindowH), dpi, 96)};
  ::AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);

  RECT work = {};
  ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int w = r.right - r.left;
  const int h = r.bottom - r.top;
  const int x = work.left + ((work.right - work.left) - w) / 2;
  // A third of the way down rather than centred: a small dialog placed dead
  // centre sits lower than the eye expects.
  const int y = work.top + ((work.bottom - work.top) - h) / 3;

  hwnd_ = ::CreateWindowExW(0, wc.lpszClassName, L"Zephyrus Setup", style, x, y,
                            w, h, nullptr, nullptr, instance, this);
  if (!hwnd_) {
    return false;
  }

  if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2d_.GetAddressOf()))) {
    return false;
  }
  if (FAILED(::DWriteCreateFactory(
          DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
          reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
    return false;
  }

  // Inter if it loaded from our own resources, Segoe UI if not. The family name
  // has to change with the collection: a private collection has no "Segoe UI",
  // and the system collection only has "Inter" on machines that happen to have
  // it installed.
  const bool have_inter = LoadEmbeddedFonts(instance);
  IDWriteFontCollection* const collection = have_inter ? fonts_.Get() : nullptr;
  const wchar_t* const family = have_inter ? L"Inter" : L"Segoe UI";

  dwrite_->CreateTextFormat(family, collection, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                            DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, 16.f, L"en-us",
                            title_format_.GetAddressOf());
  dwrite_->CreateTextFormat(family, collection, DWRITE_FONT_WEIGHT_NORMAL,
                            DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, 13.f, L"en-us",
                            body_format_.GetAddressOf());
  dwrite_->CreateTextFormat(family, collection, DWRITE_FONT_WEIGHT_NORMAL,
                            DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, 13.f, L"en-us",
                            button_format_.GetAddressOf());
  for (auto* f : {title_format_.Get(), body_format_.Get()}) {
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  }
  button_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  button_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

  dpi_scale_ = static_cast<float>(::GetDpiForWindow(hwnd_)) / 96.f;

  // Reduced motion means less movement, not none: fades aid comprehension and
  // stay; travel and stagger are decoration and go.
  BOOL animations = TRUE;
  ::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
  reduced_motion_ = !animations;

  instance_ = instance;
  start_ticks_ = ::GetTickCount64();

  // The demo has nothing to confirm, so it skips straight to the sequence.
  started_ = demo_;

  ::ShowWindow(hwnd_, SW_SHOW);
  ::UpdateWindow(hwnd_);
  ::SetTimer(hwnd_, 1, 16, nullptr);
  return true;
}

int SetupWindow::RunMessageLoop() {
  MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
  return 0;
}

LRESULT CALLBACK SetupWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM w,
                                           LPARAM l) {
  SetupWindow* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCT*>(l);
    self = static_cast<SetupWindow*>(cs->lpCreateParams);
    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  } else {
    self =
        reinterpret_cast<SetupWindow*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }
  return self ? self->WndProc(hwnd, msg, w, l)
              : ::DefWindowProc(hwnd, msg, w, l);
}

LRESULT SetupWindow::WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_TIMER:
      Tick();
      ::InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      ::BeginPaint(hwnd, &ps);
      Render();
      ::EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const float x = ToDip(GET_X_LPARAM(l));
      const float y = ToDip(GET_Y_LPARAM(l));
      bool changed = false;
      for (Button& b : buttons_) {
        const bool hot = x >= b.bounds.left && x <= b.bounds.right &&
                         y >= b.bounds.top && y <= b.bounds.bottom;
        changed |= (hot != b.hot);
        b.hot = hot;
      }
      if (changed) {
        ::InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const float x = ToDip(GET_X_LPARAM(l));
      const float y = ToDip(GET_Y_LPARAM(l));
      for (Button& b : buttons_) {
        b.pressed = x >= b.bounds.left && x <= b.bounds.right &&
                    y >= b.bounds.top && y <= b.bounds.bottom;
      }
      ::InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONUP: {
      OnClick(ToDip(GET_X_LPARAM(l)), ToDip(GET_Y_LPARAM(l)));
      for (Button& b : buttons_) {
        b.pressed = false;
      }
      ::InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_SIZE:
      if (rt_) {
        rt_->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
      }
      return 0;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
  }
  return ::DefWindowProc(hwnd, msg, w, l);
}

// ---------------------------------------------------------------------------

void SetupWindow::Tick() {
  const double previous = now_;
  now_ = (::GetTickCount64() - start_ticks_) / 1000.0;
  const float dt = static_cast<float>(std::clamp(now_ - previous, 0.0, 0.1));

  // The bar eases toward whatever was last reported. A real install reports
  // coarse milestones, and a bar that jumps 20% in one frame reads as broken
  // even though the number is honest.
  progress_shown_ = Approach(progress_shown_, progress_, 7.f, dt);

  if (finished_ && finished_at_ < 0.0) {
    finished_at_ = now_;
  } else if (!finished_) {
    finished_at_ = -1.0;
  }

  if (demo_) {
    TickDemo();
  } else {
    TickInstall();
  }
  LayoutButtons();
}

void SetupWindow::TickInstall() {
  if (!started_) {
    if (state_ != State::kIdle) {
      state_ = State::kIdle;
      state_started_ = now_;
    }
    status_ = L"Installs for your account only. No admin rights needed.";
    return;
  }

  progress_ = runner_.progress();

  const InstallerRunner::Phase phase = runner_.phase();
  auto set = [&](State s, const wchar_t* text) {
    if (state_ != s) {
      state_ = s;
      state_started_ = now_;
    }
    status_ = text;
  };

  switch (phase) {
    case InstallerRunner::Phase::kIdle:
    case InstallerRunner::Phase::kExtracting:
      set(State::kWorking, L"Unpacking files");
      break;
    case InstallerRunner::Phase::kInstalling:
      set(State::kCarrying, L"Copying program files");
      break;
    case InstallerRunner::Phase::kFinishing:
      set(State::kThinking, L"Finishing up");
      break;
    case InstallerRunner::Phase::kDone:
      set(State::kHappy, L"Zephyrus is ready to use.");
      finished_ = true;
      failed_ = false;
      break;
    case InstallerRunner::Phase::kFailed: {
      // The runner's own words, not a generic apology. It knows whether the
      // payload was missing, the disk was full, or setup refused -- and those
      // need different things from the user.
      const std::wstring reason = runner_.error();
      set(State::kAlert, L"");
      status_ = reason.empty() ? L"The install didn’t finish." : reason;
      finished_ = true;
      failed_ = true;
      break;
    }
  }
}

void SetupWindow::TickDemo() {
  const double cycle = fmod(now_, 20.0);
  auto set = [&](State s, float p, const wchar_t* text) {
    if (state_ != s) {
      state_ = s;
      state_started_ = now_;
    }
    progress_ = p;
    status_ = text;
  };

  if (cycle < 4.0) {
    set(State::kWorking, static_cast<float>(cycle / 4.0 * 0.35),
        L"Unpacking files");
    finished_ = failed_ = false;
  } else if (cycle < 9.0) {
    set(State::kCarrying, static_cast<float>(0.35 + (cycle - 4.0) / 5.0 * 0.45),
        L"Copying program files");
  } else if (cycle < 12.0) {
    set(State::kThinking, static_cast<float>(0.80 + (cycle - 9.0) / 3.0 * 0.20),
        L"Finishing up");
  } else if (cycle < 16.0) {
    set(State::kHappy, 1.f, L"Zephyrus is ready to use.");
    finished_ = true;
    failed_ = false;
  } else {
    set(State::kAlert, 1.f, L"The install didn’t finish.");
    finished_ = true;
    failed_ = true;
  }
}

void SetupWindow::LayoutButtons() {
  buttons_.clear();
  // The bar and the buttons share one row. Before starting and after finishing
  // there is nothing to report, so the row belongs to whatever the user does
  // next; in between it belongs to the bar.
  if (started_ && !finished_) {
    return;
  }
  float x = kTextRight - kButtonW;
  auto add = [&](const wchar_t* label, bool primary) {
    Button b;
    b.bounds = D2D1::RectF(x, kButtonsY, x + kButtonW, kButtonsY + kButtonH);
    b.label = label;
    b.primary = primary;
    buttons_.push_back(b);
    x -= kButtonW + kButtonGap;
  };
  if (!started_) {
    add(L"Install", true);
    add(L"Cancel", false);
  } else if (failed_) {
    add(L"Retry", true);
    add(L"Close", false);
  } else {
    add(L"Launch Zephyrus", true);
    add(L"Create shortcut", false);
  }
}

void SetupWindow::OnClick(float x, float y) {
  for (Button& b : buttons_) {
    if (x < b.bounds.left || x > b.bounds.right || y < b.bounds.top ||
        y > b.bounds.bottom) {
      continue;
    }
    if (b.label == L"Install") {
      started_ = true;
      // Reset the entrance clock so the phase animations start from the moment
      // the user committed, not from when the window opened.
      state_started_ = now_;
      if (!demo_) {
        runner_.Start(instance_);
      }
      LayoutButtons();
      return;
    }
    if (b.label == L"Cancel") {
      ::PostMessage(hwnd_, WM_CLOSE, 0, 0);
      return;
    }
    if (b.label == L"Create shortcut" && !b.done) {
      // Reports what actually happened. A button that says "created" whether or
      // not the file appeared is worse than one that admits it could not.
      const bool ok = demo_ || InstallerRunner::CreateDesktopShortcut();
      b.done = ok;
      b.label = ok ? L"Shortcut created" : L"Couldn’t create shortcut";
    } else if (b.label == L"Launch Zephyrus") {
      if (demo_ || InstallerRunner::Launch()) {
        ::PostMessage(hwnd_, WM_CLOSE, 0, 0);
      }
    } else if (b.label == L"Close") {
      ::PostMessage(hwnd_, WM_CLOSE, 0, 0);
    } else if (b.label == L"Retry") {
      // Relaunches this installer and steps aside, rather than trying to
      // restart a worker thread whose state is already in doubt. A fresh
      // process is the only retry that is genuinely a retry.
      wchar_t self[MAX_PATH] = {};
      if (::GetModuleFileNameW(nullptr, self, MAX_PATH)) {
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        if (::CreateProcessW(self, nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                             nullptr, &si, &pi)) {
          ::CloseHandle(pi.hThread);
          ::CloseHandle(pi.hProcess);
          ::PostMessage(hwnd_, WM_CLOSE, 0, 0);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------

bool SetupWindow::EnsureDeviceResources() {
  if (rt_) {
    return true;
  }
  RECT rc;
  ::GetClientRect(hwnd_, &rc);
  if (FAILED(d2d_->CreateHwndRenderTarget(
          D2D1::RenderTargetProperties(),
          D2D1::HwndRenderTargetProperties(
              hwnd_, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
          rt_.GetAddressOf()))) {
    return false;
  }
  rt_->CreateSolidColorBrush(Rgb(kTextPrimary), brush_.GetAddressOf());
  return true;
}

void SetupWindow::DiscardDeviceResources() {
  brush_.Reset();
  rt_.Reset();
}

void SetupWindow::DrawLabel(const std::wstring& text, IDWriteTextFormat* format,
                            D2D1_RECT_F rect, D2D1_COLOR_F color) {
  brush_->SetColor(color);
  rt_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, rect,
                brush_.Get());
}

void SetupWindow::DrawMascot() {
  const std::vector<Rect> rects = BuildMascot(state_, now_ - state_started_);

  const float scale = kArtBoxSize / kArtW;
  const float ox = kArtBoxX;
  const float oy = kArtBoxY + (kArtBoxSize - kArtH * scale) / 2.f;

  // Contact shadow, sized from the lowest rectangle in the frame rather than
  // from a copy of the hop curve -- so it stays correct for every state without
  // the two definitions drifting apart.
  float lowest = 0.f;
  for (const Rect& r : rects) {
    lowest = std::max(lowest, r.y + r.h);
  }
  constexpr float kGroundY = 141.f;
  const float airborne = std::clamp((kGroundY - lowest) / 18.f, 0.f, 1.f);
  const float shadow_w = (58.f - 20.f * airborne) * scale;
  const float shadow_a = 0.15f - 0.08f * airborne;
  const float shadow_cx = ox + (kArtW / 2.f) * scale;
  const float shadow_y = oy + (kGroundY + 4.f) * scale;
  const float block = std::max(Snap(4.f * scale), 1.f);
  const float rows[][2] = {{1.0f, 0.f}, {0.6f, 1.f}};
  for (const auto& row : rows) {
    const float half = shadow_w * row[0] / 2.f;
    const float top = shadow_y + row[1] * block;
    brush_->SetColor(Rgb(0x000000, shadow_a * (1.f - row[1] * 0.3f)));
    rt_->FillRectangle(D2D1::RectF(Snap(shadow_cx - half), Snap(top),
                                   Snap(shadow_cx + half), Snap(top) + block),
                       brush_.Get());
  }

  for (const Rect& r : rects) {
    brush_->SetColor(Rgb(r.color));
    // Snapped to whole device pixels: this is pixel art, and half-pixel edges
    // turn crisp blocks into grey mush.
    const float left = Snap(ox + r.x * scale);
    const float top = Snap(oy + r.y * scale);
    const float right = Snap(ox + (r.x + r.w) * scale);
    const float bottom = Snap(oy + (r.y + r.h) * scale);
    rt_->FillRectangle(D2D1::RectF(left, top, right, bottom), brush_.Get());
  }
}

void SetupWindow::DrawBar(float progress) {
  progress = std::clamp(progress, 0.f, 1.f);

  const D2D1_RECT_F outer = D2D1::RectF(
      Snap(kBarX), Snap(kBarY), Snap(kBarX + kBarW), Snap(kBarY + kBarH));

  // Track, then a hairline border: the classic recessed field.
  brush_->SetColor(Rgb(kFieldBg));
  rt_->FillRectangle(outer, brush_.Get());
  brush_->SetColor(Rgb(kBorderCol));
  rt_->DrawRectangle(outer, brush_.Get(), 1.f / dpi_scale_);

  const float inset = 3.f;
  const float span = kBarW - inset * 2.f;
  const float filled = span * progress;
  if (filled <= 0.5f) {
    return;
  }

  float flash = 0.f;
  if (finished_ && !failed_ && finished_at_ >= 0.0 && !reduced_motion_) {
    flash = std::clamp(
        1.f - EaseOut(static_cast<float>((now_ - finished_at_) / 0.45)), 0.f,
        1.f);
  }

  brush_->SetColor(failed_ ? Rgb(kRed) : Rgb(flash > 0.4f ? kBlueLight : kBlue));
  rt_->FillRectangle(
      D2D1::RectF(Snap(kBarX + inset), Snap(kBarY + inset),
                  Snap(kBarX + inset + filled), Snap(kBarY + kBarH - inset)),
      brush_.Get());
}

void SetupWindow::DrawButton(const Button& b) {
  // Staggered entrance, 60ms apart: a cascade reads as composed; both at once
  // reads as a dialog popping.
  float appear = 1.f;
  if (finished_at_ >= 0.0 && !reduced_motion_ && finished_) {
    const double delay = b.primary ? 0.0 : 0.06;
    appear = EaseOut(static_cast<float>((now_ - finished_at_ - delay) / 0.2));
  }
  if (appear <= 0.001f) {
    return;
  }

  const float rise = (1.f - appear) * 4.f;
  const D2D1_RECT_F box =
      D2D1::RectF(b.bounds.left, b.bounds.top + rise, b.bounds.right,
                  b.bounds.bottom + rise);
  const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(box, 4.f, 4.f);

  // Pressed shifts the fill a shade darker rather than moving the button. On a
  // light surface a 2px drop reads as a rendering glitch; a tone change reads
  // as a press.
  if (b.primary) {
    const float shade = b.pressed ? 0.78f : (b.hot ? 1.f : 0.92f);
    brush_->SetColor(Rgb(kBlue, shade * appear));
    rt_->FillRoundedRectangle(rr, brush_.Get());
    DrawLabel(b.label, button_format_.Get(), box, Rgb(0xFFFFFF, appear));
  } else {
    brush_->SetColor(Rgb(kFieldBg, (b.pressed ? 0.75f : 1.f) * appear));
    rt_->FillRoundedRectangle(rr, brush_.Get());
    brush_->SetColor(Rgb(kBorderCol, appear));
    rt_->DrawRoundedRectangle(rr, brush_.Get(), 1.f / dpi_scale_);
    DrawLabel(b.label, button_format_.Get(), box,
              Rgb(b.done ? kBlue : kTextPrimary, appear));
  }
}

void SetupWindow::Render() {
  if (!EnsureDeviceResources()) {
    return;
  }

  const D2D1_SIZE_F size = rt_->GetSize();
  width_ = size.width;
  height_ = size.height;
  float dpi_x = 96.f, dpi_y = 96.f;
  rt_->GetDpi(&dpi_x, &dpi_y);
  dpi_scale_ = dpi_x / 96.f;
  opened_ = now_;

  rt_->BeginDraw();
  rt_->Clear(Rgb(kSurface));

  DrawMascot();

  const float text_in =
      reduced_motion_ ? 1.f
                      : EaseOut(static_cast<float>((opened_ - 0.05) / 0.22));

  const wchar_t* headline =
      !started_ ? L"Install Zephyrus"
                : (failed_ ? L"Something went wrong"
                           : (finished_ ? L"Zephyrus is installed"
                                        : L"Installing, please wait…"));
  DrawLabel(headline, title_format_.Get(),
            D2D1::RectF(kTextX, kTitleY, kTextRight, kTitleY + 24.f),
            Rgb(kTextPrimary, text_in));

  std::wstring status = status_;
  if (started_ && !finished_) {
    status +=
        L"  " +
        std::to_wstring(static_cast<int>(progress_shown_ * 100.f + 0.5f)) + L"%";
  }
  DrawLabel(status, body_format_.Get(),
            D2D1::RectF(kTextX, kStatusY, kTextRight, kStatusY + 20.f),
            Rgb(kTextSecondary, text_in));

  // The bar and the buttons share a row and are never both present. The
  // condition has to match LayoutButtons() exactly -- it did not, which is why
  // the waiting screen drew an empty bar and no Install button: buttons were
  // being created and then never painted.
  if (!started_ || finished_) {
    for (const Button& b : buttons_) {
      DrawButton(b);
    }
  } else {
    DrawBar(progress_shown_);
  }

  if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
    DiscardDeviceResources();
  }
}

}  // namespace
}  // namespace zephyrus_setup

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmdline, int) {
  ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  const std::wstring_view args(cmdline ? cmdline : L"");
  const bool demo = args.find(L"--demo") != std::wstring_view::npos;

  zephyrus_setup::SetupWindow window;
  if (!window.Create(instance, demo)) {
    ::MessageBoxW(nullptr, L"Could not start the Zephyrus installer.",
                  L"Zephyrus", MB_ICONERROR | MB_OK);
    return 1;
  }
  const int rc = window.RunMessageLoop();
  ::CoUninitialize();
  return rc;
}

// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AUDIO_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AUDIO_BUTTON_H_

#include "base/timer/timer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/button/button.h"

// The audio control of a tab, in the horizontal strip and in the sidebar alike.
// It tells you a tab is playing, and it is how you mute it: a click mutes, a
// click again unmutes.
//
// Playing is three bars that rise and fall (the equalizer mark); muted is the
// struck-through speaker, so the two states read apart at a glance. Hover and
// press are an M3 state layer of the row's own ink. The bars move only while
// the button is on screen and playing, and not at all with reduced motion.
class ZephyrusAudioButton : public views::Button {
  METADATA_HEADER(ZephyrusAudioButton, views::Button)

 public:
  // `diameter` is the control's own size; the state layer is a circle of it.
  ZephyrusAudioButton(PressedCallback callback, int diameter);
  ZephyrusAudioButton(const ZephyrusAudioButton&) = delete;
  ZephyrusAudioButton& operator=(const ZephyrusAudioButton&) = delete;
  ~ZephyrusAudioButton() override;

  // `audible` and `muted` as the tab reports them. Wanted whenever either is
  // true: a muted tab keeps the button so the user can undo it. Showing and
  // hiding is the owner's (it knows its layout).
  void SetAudio(bool audible, bool muted);
  bool wanted() const { return audible_ || muted_; }

  void SetInk(SkColor ink);

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void VisibilityChanged(views::View* starting_from, bool is_visible) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  bool animating() const { return timer_.IsRunning(); }
  void UpdateTimer();
  void Tick();

  const int diameter_;
  bool audible_ = false;
  bool muted_ = false;
  float phase_ = 0.f;
  SkColor ink_ = SK_ColorTRANSPARENT;
  base::RepeatingTimer timer_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AUDIO_BUTTON_H_

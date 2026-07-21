// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"

#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/bubble/bubble_frame_view.h"

namespace zephyrus {

void ConfigureBubble(views::BubbleDialogDelegate* bubble) {
  if (!bubble) {
    return;
  }
  bubble->set_corner_radius(kCornerRadius);
}

void ApplyBubbleFrame(views::BubbleDialogDelegate* bubble) {
  if (!bubble) {
    return;
  }
  views::BubbleFrameView* frame = bubble->GetBubbleFrameView();
  if (!frame || !frame->bubble_border()) {
    return;
  }
  frame->bubble_border()->set_draw_border_stroke(false);
}

}  // namespace zephyrus

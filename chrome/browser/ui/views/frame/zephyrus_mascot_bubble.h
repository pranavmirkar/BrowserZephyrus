// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_MASCOT_BUBBLE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_MASCOT_BUBBLE_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace views {
class Label;
class LabelButton;
}  // namespace views

namespace zephyrus::agent {

// What the agent is saying, above the mascot, wherever the mascot is.
//
// The agent panel is a place to read a whole task afterwards. The mascot is
// where the person is LOOKING while a task runs -- on the page, at the thing
// being clicked -- so what the agent is doing, what it found, and what it needs
// permission for are said there, next to it. A person who never opens the panel
// (hands-free, with "Hey Zep") gets all of it from here.
//
// Three things, in order of importance:
//   * a QUESTION: a permission or a hand-off, with buttons. It stays until it is
//     answered and is the only state that takes clicks.
//   * an ANSWER: the result. Stays a while, then goes.
//   * a STATUS: the step in progress. Replaced by the next one.
//
// Everything else passes clicks straight through to the page underneath: a
// bubble that catches a click meant for the page would be in the way of the
// thing it is reporting on.
class ZephyrusMascotBubble : public views::View {
  METADATA_HEADER(ZephyrusMascotBubble, views::View)

 public:
  // Called once with the person's choice: true for the first button.
  using AnswerCallback = base::RepeatingCallback<void(bool)>;

  static constexpr int kMaxWidth = 300;

  ZephyrusMascotBubble();
  ZephyrusMascotBubble(const ZephyrusMascotBubble&) = delete;
  ZephyrusMascotBubble& operator=(const ZephyrusMascotBubble&) = delete;
  ~ZephyrusMascotBubble() override;

  void ShowStatus(const std::u16string& text);
  void ShowAnswer(const std::u16string& text);
  // Replaces any question already on screen (its callback is NOT run: the panel
  // answers a question exactly once and owns that).
  void ShowQuestion(const std::u16string& title,
                    const std::u16string& reason,
                    const std::u16string& yes,
                    const std::u16string& no,
                    AnswerCallback on_answer);
  // Removes a question, and everything else if `and_text`.
  void ClearQuestion();
  void Clear();

  bool HasQuestion() const { return has_question_; }
  bool HasContent() const { return has_content_; }
  const std::u16string& status_text() const { return status_text_; }
  const std::u16string& answer_text() const { return answer_text_; }

  // Puts the bubble above (or, with no room above, below) `mascot`, inside
  // `area`, with its tail pointing at the mascot. Cheap: called on every frame
  // the mascot moves.
  void PlaceNear(const gfx::Rect& mascot, const gfx::Rect& area);

  // Test-facing: the answer buttons, as a click would reach them.
  void PressForTesting(bool yes);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

 private:
  void Refresh();
  void Answer(bool yes);
  void ApplyColors();

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> body_ = nullptr;
  raw_ptr<views::LabelButton> yes_ = nullptr;
  raw_ptr<views::LabelButton> no_ = nullptr;
  raw_ptr<views::View> buttons_ = nullptr;

  std::u16string status_text_;
  std::u16string answer_text_;
  bool has_question_ = false;
  bool has_content_ = false;
  AnswerCallback on_answer_;
  base::OneShotTimer answer_timer_;

  // Where the tail points, as an x offset into the bubble, and whether it hangs
  // off the bottom (bubble above the mascot) or the top (below it).
  int tail_x_ = 24;
  bool tail_on_bottom_ = true;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_MASCOT_BUBBLE_H_

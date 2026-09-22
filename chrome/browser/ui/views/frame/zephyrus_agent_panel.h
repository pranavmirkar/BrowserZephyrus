// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_

#include <memory>
#include <string>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_task_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class BrowserView;

namespace views {
class BoxLayoutView;
class ImageButton;
class ImageView;
class Label;
class LabelButton;
class ProgressBar;
class ScrollView;
class Textfield;
}  // namespace views

namespace zephyrus::agent {

class JumpToLatestButton;

// Where a task lives while it runs.
//
// A right-hand column, mirroring the workspaces sidebar on the left -- an M3
// STANDARD SIDE SHEET: docked beside the page rather than over it, so the page
// the agent is working on stays in view. It exists because a task is not a
// one-shot command: it takes several steps, may stop to ask permission, and
// ends with an answer. A prompt box can start one; only something persistent
// can show one.
//
// The running log is built from what the BROWSER sees, not from a progress
// channel through the kernel. The browser performs every step, so it already
// knows about each one -- a second source of truth for the same events would be
// one more thing to disagree.
class ZephyrusAgentPanel : public views::View,
                           public ZephyrusAgentTaskController::Delegate,
                           public views::TextfieldController {
  METADATA_HEADER(ZephyrusAgentPanel, views::View)

 public:
  static constexpr int kDefaultWidth = 340;
  static constexpr int kMinWidth = 260;

  explicit ZephyrusAgentPanel(BrowserView* browser_view);
  ~ZephyrusAgentPanel() override;

  ZephyrusAgentPanel(const ZephyrusAgentPanel&) = delete;
  ZephyrusAgentPanel& operator=(const ZephyrusAgentPanel&) = delete;

  bool is_open() const { return is_open_; }

  // The width the layout should reserve: the panel's width when open, zero
  // when not. The page gets the space back the moment the panel closes.
  int GetReservedWidth() const;

  void Toggle();
  void Open();
  void Close();

  // Answers the question on screen as the buttons would. Test-facing: it
  // replaces the click, not the decision.
  void AnswerApprovalForTesting(bool approved) { AnswerApproval(approved); }

  bool HasQuestionForTesting() const { return !approval_answer_.is_null(); }

  // The running log, so a test can check its lines are actually laid out
  // inside the panel. Two attempts at a ScrollView here rendered NOTHING, and
  // neither was caught by anything but a person looking at the window.
  const views::BoxLayoutView* log_for_testing() const { return log_; }
  views::ScrollView* log_scroll_for_testing() { return log_scroll_; }
  bool IsJumpToLatestVisibleForTesting() const;
  void JumpToLatestForTesting() { ScrollLogToLatest(); }

  // ZephyrusAgentTaskController::Delegate:
  void OnAgentProgress(const std::string& line) override;
  void OnAgentApprovalNeeded(const std::string& reason,
                             const std::string& risk,
                             base::OnceCallback<void(bool)> answer) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // Submits whatever is in the input. Enter and the send button both land
  // here, so neither is the only way in.
  void Submit();

  // views::View:
  void OnThemeChanged() override;
  void Layout(PassKey) override;

  // What a line in the log is, which decides how it is drawn.
  enum class LineKind {
    // What the user asked for: a trailing bubble, the way a sent message sits.
    kTask,
    // A step the browser took, or anything else the agent says on the way.
    // Quiet on purpose: a log that shouts every step is a log nobody reads.
    kStep,
    // The answer, or the question the agent stopped to ask. The one line the
    // user came for, so it gets a card.
    kAnswer,
  };

 private:
  void StartTask(const std::string& task);
  void OnTaskFinished(mojom::TaskOutcomePtr outcome);

  void AddLine(const std::string& text, LineKind kind);

  // Flips everything that depends on whether a task is running: the progress
  // indicator, the send/stop button, and the input.
  void SetTaskRunning(bool running);

  // The send button's one callback: stop when a task is running, send when not.
  void OnSendOrStop();

  void ShowApprovalCard(const std::string& reason,
                        base::OnceCallback<void(bool)> answer);
  void ClearApprovalCard();
  void AnswerApproval(bool approved);

  void ApplyRoles();

  // The log follows new lines only while the user is already at its end --
  // the way a chat does. Scrolled back to read something, they stay where they
  // are, and a jump-to-latest button appears instead.
  bool IsLogAtBottom() const;
  void ScrollLogToLatest();
  void OnLogScrolled();
  void UpdateJumpButton();

  const raw_ptr<BrowserView> browser_view_;

  bool is_open_ = false;
  bool task_running_ = false;

  raw_ptr<views::ImageView> header_icon_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<views::ProgressBar> progress_ = nullptr;
  raw_ptr<views::View> empty_state_ = nullptr;
  raw_ptr<views::ImageView> empty_icon_ = nullptr;
  raw_ptr<views::Label> empty_label_ = nullptr;
  raw_ptr<views::ScrollView> log_scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> log_ = nullptr;
  raw_ptr<views::BoxLayoutView> approval_ = nullptr;
  raw_ptr<views::ImageView> approval_icon_ = nullptr;
  raw_ptr<views::Label> approval_title_ = nullptr;
  raw_ptr<views::Label> approval_reason_ = nullptr;
  raw_ptr<views::LabelButton> allow_button_ = nullptr;
  raw_ptr<views::LabelButton> decline_button_ = nullptr;
  raw_ptr<views::View> input_bar_ = nullptr;
  raw_ptr<views::Textfield> input_ = nullptr;
  raw_ptr<views::ImageButton> send_button_ = nullptr;
  raw_ptr<JumpToLatestButton> jump_button_ = nullptr;

  // Lines added while the user was scrolled away from the end.
  int unread_ = 0;
  base::CallbackListSubscription log_scrolled_subscription_;

  // Non-null only while a question is on screen. Run exactly once.
  base::OnceCallback<void(bool)> approval_answer_;

  std::unique_ptr<ZephyrusAgentTaskController> controller_;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_

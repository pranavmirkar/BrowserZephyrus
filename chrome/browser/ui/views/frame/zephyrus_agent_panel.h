// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_task_controller.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class BrowserView;

namespace views {
class BoxLayoutView;
class Label;
class ScrollView;
class Textfield;
}  // namespace views

namespace zephyrus::agent {

// Where a task lives while it runs.
//
// A right-hand column, mirroring the workspaces sidebar on the left. It exists
// because a task is not a one-shot command: it takes several steps, may stop to
// ask permission, and ends with an answer. A prompt box can start one; only
// something persistent can show one.
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

  // ZephyrusAgentTaskController::Delegate:
  void OnAgentProgress(const std::string& line) override;
  void OnAgentApprovalNeeded(const std::string& reason,
                             const std::string& risk,
                             base::OnceCallback<void(bool)> answer) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

  // views::View:
  void OnThemeChanged() override;

 private:
  void StartTask(const std::string& task);
  void OnTaskFinished(mojom::TaskOutcomePtr outcome);

  // One line in the running log. `emphasis` is for the answer and for anything
  // the user has to read; everything else is quiet on purpose, because a log
  // that shouts every step is a log nobody reads.
  void AddLine(const std::string& text, bool emphasis);

  void ShowApprovalCard(const std::string& reason,
                        base::OnceCallback<void(bool)> answer);
  void ClearApprovalCard();
  void AnswerApproval(bool approved);

  void ApplyPalette();

  const raw_ptr<BrowserView> browser_view_;

  bool is_open_ = false;
  bool task_running_ = false;

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::ScrollView> log_scroll_ = nullptr;
  raw_ptr<views::BoxLayoutView> log_ = nullptr;
  raw_ptr<views::BoxLayoutView> approval_ = nullptr;
  raw_ptr<views::Label> approval_reason_ = nullptr;
  raw_ptr<views::Textfield> input_ = nullptr;

  // Non-null only while a question is on screen. Run exactly once.
  base::OnceCallback<void(bool)> approval_answer_;

  std::unique_ptr<ZephyrusAgentTaskController> controller_;
};

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_AGENT_PANEL_H_

// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_panel.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/font_list.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout_view.h"

namespace zephyrus::agent {
namespace {

constexpr int kEdge = 12;
constexpr int kLineGap = 6;

// How many steps a task started from here may take.
//
// Small on purpose. At roughly four seconds a step, a budget of twenty is over
// a minute of a browser doing things on its own, which is longer than anyone
// will watch without wondering whether it has hung.
constexpr uint32_t kMaxSteps = 12;

}  // namespace

ZephyrusAgentPanel::ZephyrusAgentPanel(BrowserView* browser_view)
    : browser_view_(browser_view) {
  SetVisible(false);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kEdge), kLineGap));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  title_ = AddChildView(std::make_unique<views::Label>(u"Agent"));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  // The log takes whatever height is left, so the input stays pinned to the
  // bottom however long the conversation gets.
  log_scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  log_scroll_->SetDrawOverflowIndicator(false);
  log_scroll_->SetBackgroundColor(std::nullopt);
  auto log = std::make_unique<views::BoxLayoutView>();
  log->SetOrientation(views::BoxLayout::Orientation::kVertical);
  log->SetBetweenChildSpacing(kLineGap);
  log->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  log_ = log_scroll_->SetContents(std::move(log));
  layout->SetFlexForView(log_scroll_, 1);

  // The approval card. Built once and hidden, rather than created per question,
  // so a question cannot arrive while its own view is still being constructed.
  approval_ = AddChildView(std::make_unique<views::BoxLayoutView>());
  approval_->SetOrientation(views::BoxLayout::Orientation::kVertical);
  approval_->SetInsideBorderInsets(gfx::Insets(kEdge));
  approval_->SetBetweenChildSpacing(kLineGap);
  approval_->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  approval_->SetVisible(false);

  approval_reason_ = approval_->AddChildView(std::make_unique<views::Label>());
  approval_reason_->SetMultiLine(true);
  approval_reason_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  auto* buttons =
      approval_->AddChildView(std::make_unique<views::BoxLayoutView>());
  buttons->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  buttons->SetBetweenChildSpacing(kLineGap);
  buttons->AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&ZephyrusAgentPanel::AnswerApproval,
                          base::Unretained(this), true),
      u"Allow once"));
  buttons->AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&ZephyrusAgentPanel::AnswerApproval,
                          base::Unretained(this), false),
      u"Not now"));

  input_ = AddChildView(std::make_unique<views::Textfield>());
  input_->set_controller(this);
  input_->SetPlaceholderText(u"Ask the agent to do something");
  input_->GetViewAccessibility().SetName(u"Agent task");

  ApplyPalette();
}

ZephyrusAgentPanel::~ZephyrusAgentPanel() {
  // A question left on screen when the panel goes away is still a question the
  // task is waiting on. Answering no is the only safe reading of "the window
  // closed".
  if (approval_answer_) {
    std::move(approval_answer_).Run(false);
  }
}

int ZephyrusAgentPanel::GetReservedWidth() const {
  return is_open_ ? kDefaultWidth : 0;
}

void ZephyrusAgentPanel::Toggle() {
  is_open_ ? Close() : Open();
}

void ZephyrusAgentPanel::Open() {
  if (is_open_) {
    return;
  }
  is_open_ = true;
  SetVisible(true);
  if (browser_view_) {
    browser_view_->InvalidateLayout();
  }
  input_->RequestFocus();
}

void ZephyrusAgentPanel::Close() {
  if (!is_open_) {
    return;
  }
  is_open_ = false;
  SetVisible(false);
  if (browser_view_) {
    browser_view_->InvalidateLayout();
  }
}

bool ZephyrusAgentPanel::HandleKeyEvent(views::Textfield* sender,
                                        const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    Close();
    return true;
  }
  if (key_event.key_code() != ui::VKEY_RETURN) {
    return false;
  }

  const std::string task = base::UTF16ToUTF8(input_->GetText());
  if (task.empty() || task_running_) {
    return true;
  }
  input_->SetText(std::u16string());
  StartTask(task);
  return true;
}

void ZephyrusAgentPanel::StartTask(const std::string& task) {
  AddLine(task, /*emphasis=*/true);

  // A fresh controller per task. It holds the kernel connection, the executor
  // and the Observation for one task's lifetime, and reusing one across tasks
  // would carry the last task's page knowledge into the next.
  controller_ = std::make_unique<ZephyrusAgentTaskController>(
      browser_view_ ? browser_view_->browser() : nullptr);
  controller_->SetDelegate(this);

  task_running_ = true;
  if (!controller_->StartTaskWithConfiguredModel(
          task, kMaxSteps,
          base::BindOnce(&ZephyrusAgentPanel::OnTaskFinished,
                         base::Unretained(this)))) {
    task_running_ = false;
    // Said plainly rather than left to look like a hang. This is the ordinary
    // state of a build nobody passed the development switches to.
    AddLine(
        "No model is configured. Start the browser with "
        "--zephyrus-agent-model-endpoint and --zephyrus-agent-model.",
        /*emphasis=*/false);
  }
}

void ZephyrusAgentPanel::OnTaskFinished(mojom::TaskOutcomePtr outcome) {
  task_running_ = false;
  ClearApprovalCard();

  if (!outcome) {
    AddLine("The agent stopped without answering.", /*emphasis=*/false);
    return;
  }

  switch (outcome->status) {
    case mojom::TaskStatus::kCompleted:
    case mojom::TaskStatus::kAskedTheUser:
      AddLine(outcome->message, /*emphasis=*/true);
      break;
    case mojom::TaskStatus::kOutOfSteps:
      AddLine("Stopped after " + base::NumberToString(outcome->steps) +
                  " steps without finishing.",
              /*emphasis=*/false);
      break;
    case mojom::TaskStatus::kNeedsApproval:
    case mojom::TaskStatus::kFailed:
      AddLine(outcome->message, /*emphasis=*/false);
      break;
  }
}

void ZephyrusAgentPanel::OnAgentProgress(const std::string& line) {
  AddLine(line, /*emphasis=*/false);
}

void ZephyrusAgentPanel::OnAgentApprovalNeeded(
    const std::string& reason,
    const std::string& risk,
    base::OnceCallback<void(bool)> answer) {
  ShowApprovalCard(reason, std::move(answer));
}

void ZephyrusAgentPanel::AddLine(const std::string& text, bool emphasis) {
  const zephyrus::Palette& palette = zephyrus::Current();

  auto* label = log_->AddChildView(
      std::make_unique<views::Label>(base::UTF8ToUTF16(text)));
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(emphasis ? palette.ink : palette.muted);

  // Follow the newest line. A log that has to be scrolled to be read is a log
  // that stops being read.
  log_->InvalidateLayout();
  log_scroll_->ScrollByOffset(gfx::PointF(0, log_->GetPreferredSize().height()));
}

void ZephyrusAgentPanel::ShowApprovalCard(
    const std::string& reason,
    base::OnceCallback<void(bool)> answer) {
  // A second question while one is on screen would strand the first. It cannot
  // happen today -- the loop stops on the first -- but leaving the older
  // callback unanswered is the sort of thing that becomes a hang later.
  if (approval_answer_) {
    std::move(approval_answer_).Run(false);
  }
  approval_answer_ = std::move(answer);
  approval_reason_->SetText(base::UTF8ToUTF16(reason));
  approval_->SetVisible(true);
  InvalidateLayout();
}

void ZephyrusAgentPanel::ClearApprovalCard() {
  approval_->SetVisible(false);
  approval_answer_.Reset();
  InvalidateLayout();
}

void ZephyrusAgentPanel::AnswerApproval(bool approved) {
  if (!approval_answer_) {
    return;
  }
  base::OnceCallback<void(bool)> answer = std::move(approval_answer_);
  approval_->SetVisible(false);
  InvalidateLayout();
  AddLine(approved ? "You allowed it." : "You declined.", /*emphasis=*/false);
  std::move(answer).Run(approved);
}

void ZephyrusAgentPanel::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplyPalette();
}

void ZephyrusAgentPanel::ApplyPalette() {
  const zephyrus::Palette& palette = zephyrus::Current();

  SetBackground(views::CreateRoundedRectBackground(palette.ground,
                                                   zephyrus::kRadiusCard));
  SetBorder(views::CreateRoundedRectBorder(1, zephyrus::kRadiusCard,
                                           palette.rule));

  if (title_) {
    title_->SetEnabledColor(palette.muted);
  }
  if (approval_) {
    approval_->SetBackground(views::CreateRoundedRectBackground(
        palette.surface, zephyrus::kRadiusCard));
  }
  if (approval_reason_) {
    approval_reason_->SetEnabledColor(palette.ink);
  }
}

BEGIN_METADATA(ZephyrusAgentPanel)
END_METADATA

}  // namespace zephyrus::agent

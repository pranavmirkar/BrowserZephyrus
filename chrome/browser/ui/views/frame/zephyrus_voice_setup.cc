// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_voice_setup.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_panel.h"
#include "chrome/browser/ui/views/frame/zephyrus_hands_free.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_controls.h"
#include "chrome/browser/ui/views/frame/zephyrus_voice_access.h"
#include "chrome/browser/zephyrus/agent/hands_free.h"
#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "chrome/browser/zephyrus/agent/phrase_recorder.h"
#include "chrome/browser/zephyrus/agent/voice_input.h"
#include "chrome/browser/zephyrus/agent/voice_library.h"
#include "chrome/browser/zephyrus/agent/voice_lock.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/combobox_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/background.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

namespace zephyrus {

namespace {

namespace m3 = zephyrus::m3;
namespace agent = zephyrus::agent;

constexpr int kWidth = 440;
constexpr int kVoiceListHeight = 104;
constexpr base::TimeDelta kRecordingTimeout = base::Seconds(90);

class SensitivityModel : public ui::ComboboxModel {
 public:
  size_t GetItemCount() const override { return 3; }
  std::u16string GetItemAt(size_t index) const override {
    switch (index) {
      case 0:
        return u"Strict: a noisy room, or several people";
      case 2:
        return u"Relaxed: a quiet room, or a voice that will not match";
      default:
        return u"Balanced";
    }
  }
};

std::u16string Describe(agent::VoiceLibrary::Result result) {
  switch (result) {
    case agent::VoiceLibrary::Result::kOk:
      return u"Saved.";
    case agent::VoiceLibrary::Result::kFull:
      return u"That is as many voices as Zep keeps. Delete one first.";
    case agent::VoiceLibrary::Result::kBadName:
      return u"Give the voice a name (up to 32 characters).";
    case agent::VoiceLibrary::Result::kDuplicate:
      return u"Another voice already has that name.";
    case agent::VoiceLibrary::Result::kNoKeystore:
      return u"The Windows keystore is not available, so the voice was not "
             u"saved. Nothing was kept.";
    case agent::VoiceLibrary::Result::kNotFound:
      return u"That voice is already gone.";
  }
  return std::u16string();
}

}  // namespace

// The dialog's contents. The dialog itself is a plain DialogDelegate (see
// ShowVoiceSetup): the bubble and dialog-view base classes keep their
// constructors private to a list of friends inside the views library.
class ZephyrusVoiceSetupDialog : public views::View {
  METADATA_HEADER(ZephyrusVoiceSetupDialog, views::View)

 public:
  explicit ZephyrusVoiceSetupDialog(BrowserView* browser_view)
      : browser_view_(browser_view) {
    Profile* profile = browser_view_->browser()->profile();
    prefs_ = profile->GetPrefs();
    private_ = profile->IsOffTheRecord();
    library_ = agent::GetVoiceLibrary(profile);
    if (agent::ZephyrusAgentPanel* panel = browser_view_->zephyrus_agent_panel()) {
      if (agent::ZephyrusHandsFree* hands_free = panel->hands_free()) {
        hands_free_ = hands_free->GetWeakPtr();
      }
    }

    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    AddChildView(MakeText(
        u"Say \"Hey Zep\" and then what you want done. Zep listens only while "
        u"this window is the one you are using, and nothing leaves this "
        u"computer until the phrase is recognised. Ctrl+Shift+Space talks to "
        u"Zep without the phrase; Ctrl+Shift+, mutes it.",
        m3::Type::kBodyMedium));

    if (private_) {
      AddChildView(MakeText(
          u"A Private Workspace keeps no voices and does not listen. Open a "
          u"regular window to set this up.",
          m3::Type::kBodyMedium));
    }

    auto* card = AddChildView(std::make_unique<m3::Card>(gfx::Insets::VH(4, 0)));
    hands_free_row_ = card->AddChildView(std::make_unique<m3::SwitchRow>(
        u"Hands-free: say \"Hey Zep\"",
        u"Needs at least one recorded voice. Off until you turn it on.",
        prefs_->GetBoolean(agent::kHandsFreePref)));
    hands_free_row_->SetEnabled(!private_);
    hands_free_row_->set_on_change(base::BindRepeating(
        &ZephyrusVoiceSetupDialog::OnSwitches, base::Unretained(this)));
    lock_row_ = card->AddChildView(std::make_unique<m3::SwitchRow>(
        u"Voice Lock: only answer my voices",
        u"A convenience lock, not security. Payments and sign-in always "
        u"need a click.",
        prefs_->GetBoolean(agent::kVoiceLockPref)));
    lock_row_->set_on_change(base::BindRepeating(
        &ZephyrusVoiceSetupDialog::OnSwitches, base::Unretained(this)));

    AddChildView(std::make_unique<m3::SectionLabel>(u"How strict"));
    sensitivity_ = AddChildView(
        std::make_unique<views::Combobox>(std::make_unique<SensitivityModel>()));
    sensitivity_->GetViewAccessibility().SetName(u"How strict the voice match is");
    sensitivity_->SetSelectedIndex(
        static_cast<size_t>(std::clamp(
            prefs_->GetInteger(agent::kVoiceSensitivityPref), 0, 2)));
    sensitivity_->SetCallback(base::BindRepeating(
        &ZephyrusVoiceSetupDialog::OnSensitivity, base::Unretained(this)));

    AddChildView(std::make_unique<m3::SectionLabel>(u"Voices"));
    auto* scroll = AddChildView(std::make_unique<views::ScrollView>(
        views::ScrollView::ScrollWithLayers::kEnabled));
    scroll->SetBackgroundColor(std::nullopt);
    scroll->SetDrawOverflowIndicator(false);
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll->SetPreferredSize(gfx::Size(kWidth, kVoiceListHeight));
    auto voices = std::make_unique<views::BoxLayoutView>();
    voices->SetOrientation(views::BoxLayout::Orientation::kVertical);
    voices->SetBetweenChildSpacing(6);
    voices_ = scroll->SetContents(std::move(voices));

    name_ = AddChildView(std::make_unique<m3::FilledField>(u"Name for a new voice",
                                                           kWidth));
    name_->field()->SetText(u"Me");

    auto* actions = AddChildView(std::make_unique<views::BoxLayoutView>());
    actions->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    actions->SetBetweenChildSpacing(8);
    record_ = actions->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&ZephyrusVoiceSetupDialog::OnRecord,
                            base::Unretained(this)),
        u"Record my voice"));
    test_ = actions->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&ZephyrusVoiceSetupDialog::OnTest,
                            base::Unretained(this)),
        u"Test my voice"));
    record_->SetEnabled(!private_);
    test_->SetEnabled(!private_);

    level_ = AddChildView(std::make_unique<views::ProgressBar>());
    level_->SetVisible(false);

    status_ = AddChildView(MakeText(std::u16string(), m3::Type::kBodyMedium));
    status_->SetMaxLines(3);
    status_->SetPreferredSize(gfx::Size(kWidth, 3 * 20));

    if (library_) {
      subscription_ = library_->Subscribe(base::BindRepeating(
          &ZephyrusVoiceSetupDialog::RebuildVoices, base::Unretained(this)));
      library_->WhenLoaded(base::BindOnce(
          &ZephyrusVoiceSetupDialog::RebuildVoices, weak_factory_.GetWeakPtr()));
    }
    RebuildVoices();
  }

  ~ZephyrusVoiceSetupDialog() override { StopMic(); }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kWidth, GetLayoutManager()->GetPreferredHeightForWidth(
                                 this, kWidth));
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateSolidBackground(
        m3::Role(*this, kColorZephyrusSurfaceContainer)));
    if (status_) {
      status_->SetEnabledColor(m3::Role(*this, kColorZephyrusOnSurfaceVariant));
    }
  }

 private:
  enum class Mode { kIdle, kEnrol, kTest };

  std::unique_ptr<views::Label> MakeText(const std::u16string& text,
                                         m3::Type type) {
    auto label = std::make_unique<views::Label>(text);
    label->SetFontList(m3::Font(type));
    label->SetMultiLine(true);
    label->SetMaximumWidth(kWidth);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    return label;
  }

  void SetStatus(const std::u16string& text) { status_->SetText(text); }

  // ---- Settings, written as they are changed ----------------------------------

  void OnSwitches() {
    prefs_->SetBoolean(agent::kHandsFreePref, hands_free_row_->is_on());
    prefs_->SetBoolean(agent::kVoiceLockPref, lock_row_->is_on());
    if (hands_free_row_->is_on() && library_ && library_->loaded() &&
        library_->profiles().empty()) {
      SetStatus(u"Record a voice below: Zep only listens once it knows whose "
                u"voice to answer.");
    }
  }

  void OnSensitivity() {
    prefs_->SetInteger(agent::kVoiceSensitivityPref,
                       static_cast<int>(sensitivity_->GetSelectedIndex().value_or(1)));
  }

  // ---- The voice list -----------------------------------------------------------

  // Never rebuilt in place from an observer or a button: that frees the button
  // being clicked. Always a task later.
  void RebuildVoices() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusVoiceSetupDialog::RebuildVoicesNow,
                       weak_factory_.GetWeakPtr()));
  }

  void RebuildVoicesNow() {
    voices_->RemoveAllChildViews();
    if (!library_ || !library_->loaded() || library_->profiles().empty()) {
      voices_->AddChildView(MakeText(
          library_ && library_->loaded()
              ? u"No voices yet. Record yours below."
              : u"Loading voices...",
          m3::Type::kBodyMedium));
      return;
    }
    for (const agent::voice::VoiceProfile& profile : library_->profiles()) {
      const std::string id = profile.id;
      auto* row = voices_->AddChildView(std::make_unique<views::BoxLayoutView>());
      row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
      row->SetBetweenChildSpacing(6);
      auto* field = row->AddChildView(std::make_unique<views::Textfield>());
      field->SetText(base::UTF8ToUTF16(profile.name));
      field->GetViewAccessibility().SetName(u"Voice name");
      field->SetProperty(views::kBoxLayoutFlexKey,
                         views::BoxLayoutFlexSpecification());
      row->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&ZephyrusVoiceSetupDialog::OnRename,
                              base::Unretained(this), id,
                              base::Unretained(field)),
          u"Rename"));
      row->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&ZephyrusVoiceSetupDialog::OnDelete,
                              base::Unretained(this), id),
          u"Delete"));
    }
  }

  void OnRename(const std::string& id, views::Textfield* field) {
    if (!library_) {
      return;
    }
    const agent::VoiceLibrary::Result result =
        library_->Rename(id, base::UTF16ToUTF8(field->GetText()));
    SetStatus(result == agent::VoiceLibrary::Result::kOk ? u"Renamed."
                                                         : Describe(result));
  }

  void OnDelete(const std::string& id) {
    if (!library_) {
      return;
    }
    const agent::VoiceLibrary::Result result = library_->Remove(id);
    SetStatus(result == agent::VoiceLibrary::Result::kOk
                  ? u"Deleted. Its recordings are gone from this computer."
                  : Describe(result));
  }

  // ---- Recording ----------------------------------------------------------------

  void OnRecord() {
    if (mode_ == Mode::kEnrol) {
      StopMic();
      SetStatus(u"Stopped. Nothing was saved.");
      return;
    }
    if (!library_ || !library_->loaded()) {
      SetStatus(u"Zep is still loading the saved voices. Try again in a "
                u"moment.");
      return;
    }
    new_name_ = agent::VoiceLibrary::CleanName(
        base::UTF16ToUTF8(name_->field()->GetText()));
    if (new_name_.empty()) {
      SetStatus(Describe(agent::VoiceLibrary::Result::kBadName));
      return;
    }
    if (library_->profiles().size() >= agent::voice::kMaxProfiles) {
      SetStatus(Describe(agent::VoiceLibrary::Result::kFull));
      return;
    }
    takes_.clear();
    if (!BeginMic(Mode::kEnrol)) {
      return;
    }
    record_->SetText(u"Stop");
    Prompt();
  }

  void Prompt() {
    SetStatus(u"Say \"Hey Zep\" (" + base::NumberToString16(takes_.size() + 1) +
              u" of " +
              base::NumberToString16(agent::voice::kEnrollmentSamples) +
              u"). Say it the way you will normally say it.");
  }

  void OnTest() {
    if (mode_ == Mode::kTest) {
      StopMic();
      SetStatus(std::u16string());
      return;
    }
    if (!library_ || !library_->loaded() || library_->profiles().empty()) {
      SetStatus(u"Record a voice first: there is nothing to test against yet.");
      return;
    }
    if (!BeginMic(Mode::kTest)) {
      return;
    }
    test_->SetText(u"Stop");
    SetStatus(u"Say \"Hey Zep\" or \"Hey Zep, what is the weather\".");
  }

  bool BeginMic(Mode mode) {
    StopMic();
    if (!mic_) {
      mic_ = std::make_unique<agent::VoiceInput>(
          g_browser_process->shared_url_loader_factory());
    }
    if (hands_free_ && !paused_) {
      hands_free_->Pause();
      paused_ = true;
    }
    recorder_.Reset();
    if (!mic_->StartStreaming(base::BindRepeating(
            &ZephyrusVoiceSetupDialog::OnAudio, base::Unretained(this)))) {
      ResumeHandsFree();
      SetStatus(u"The microphone could not be opened. Check that Windows lets "
                u"desktop apps use it (Settings > Privacy > Microphone).");
      return false;
    }
    mode_ = mode;
    level_->SetValue(0);
    level_->SetVisible(true);
    timeout_.Start(FROM_HERE, kRecordingTimeout,
                   base::BindOnce(&ZephyrusVoiceSetupDialog::OnTimeout,
                                  base::Unretained(this)));
    return true;
  }

  void StopMic() {
    timeout_.Stop();
    if (mic_ && mic_->streaming()) {
      mic_->StopStreaming();
    }
    mode_ = Mode::kIdle;
    if (level_) {
      level_->SetVisible(false);
    }
    if (record_) {
      record_->SetText(u"Record my voice");
    }
    if (test_) {
      test_->SetText(u"Test my voice");
    }
    ResumeHandsFree();
  }

  void ResumeHandsFree() {
    if (paused_) {
      paused_ = false;
      if (hands_free_) {
        hands_free_->Resume();
      }
    }
  }

  void OnTimeout() {
    StopMic();
    SetStatus(u"Stopped: nothing was heard for a while.");
  }

  void OnAudio(base::span<const int16_t> pcm) {
    std::optional<std::vector<int16_t>> utterance = recorder_.Feed(pcm);
    level_->SetValue(recorder_.level());
    if (utterance) {
      // May stop the stream; nothing after this touches it.
      OnUtterance(std::move(*utterance));
    }
  }

  void OnUtterance(std::vector<int16_t> pcm) {
    if (mode_ == Mode::kEnrol) {
      agent::voice::SampleCheck check =
          agent::voice::AnalyseEnrollmentSample(pcm, TakeSpan());
      if (check.problem != agent::voice::SampleProblem::kNone) {
        SetStatus(base::UTF8ToUTF16(
                      agent::voice::DescribeProblem(check.problem)) +
                  u" Try again.");
        return;
      }
      takes_.push_back(std::move(check.sample));
      if (takes_.size() < agent::voice::kEnrollmentSamples) {
        Prompt();
        return;
      }
      FinishEnrol();
      return;
    }
    if (mode_ == Mode::kTest) {
      const agent::voice::MatchResult match = agent::voice::TestPhrase(
          library_->profiles(), pcm, prefs_->GetBoolean(agent::kVoiceLockPref),
          Sensitivity());
      StopMic();
      if (match.wake && match.voice_ok && match.profile >= 0 &&
          static_cast<size_t>(match.profile) < library_->profiles().size()) {
        SetStatus(u"Heard \"Hey Zep\" from " +
                  base::UTF8ToUTF16(library_->profiles()[match.profile].name) +
                  u".");
      } else if (match.wake) {
        SetStatus(u"That sounded like \"Hey Zep\", but not like a voice Zep "
                  u"knows, so it would be ignored.");
      } else {
        SetStatus(u"That did not sound like \"Hey Zep\". Say just those two "
                  u"words, clearly.");
      }
    }
  }

  base::span<const agent::voice::WakeTemplate> TakeSpan() const {
    return takes_;
  }

  agent::voice::Sensitivity Sensitivity() const {
    switch (prefs_->GetInteger(agent::kVoiceSensitivityPref)) {
      case 0:
        return agent::voice::Sensitivity::kStrict;
      case 2:
        return agent::voice::Sensitivity::kRelaxed;
      default:
        return agent::voice::Sensitivity::kBalanced;
    }
  }

  void FinishEnrol() {
    StopMic();
    agent::voice::VoiceProfile profile;
    profile.id = agent::VoiceLibrary::NewId();
    profile.name = new_name_;
    profile.created_unix = base::Time::Now().ToTimeT();
    profile.templates = std::move(takes_);
    takes_.clear();
    const agent::VoiceLibrary::Result result = library_->Add(std::move(profile));
    if (result == agent::VoiceLibrary::Result::kOk) {
      SetStatus(u"Saved. Zep now answers " + base::UTF8ToUTF16(new_name_) +
                u". Turn on \"Hands-free\" above to start using it.");
      name_->field()->SetText(std::u16string());
    } else {
      SetStatus(Describe(result));
    }
  }

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<PrefService> prefs_ = nullptr;
  raw_ptr<agent::VoiceLibrary> library_ = nullptr;
  base::WeakPtr<agent::ZephyrusHandsFree> hands_free_;
  bool private_ = false;
  bool paused_ = false;

  raw_ptr<m3::SwitchRow> hands_free_row_ = nullptr;
  raw_ptr<m3::SwitchRow> lock_row_ = nullptr;
  raw_ptr<views::Combobox> sensitivity_ = nullptr;
  raw_ptr<views::View> voices_ = nullptr;
  raw_ptr<m3::FilledField> name_ = nullptr;
  raw_ptr<views::MdTextButton> record_ = nullptr;
  raw_ptr<views::MdTextButton> test_ = nullptr;
  raw_ptr<views::ProgressBar> level_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;

  std::unique_ptr<agent::VoiceInput> mic_;
  agent::voice::PhraseRecorder recorder_;
  Mode mode_ = Mode::kIdle;
  std::string new_name_;
  std::vector<agent::voice::WakeTemplate> takes_;
  base::OneShotTimer timeout_;
  base::CallbackListSubscription subscription_;

  base::WeakPtrFactory<ZephyrusVoiceSetupDialog> weak_factory_{this};
};

BEGIN_METADATA(ZephyrusVoiceSetupDialog)
END_METADATA

void ShowVoiceSetup(BrowserView* browser_view) {
  if (!browser_view || !browser_view->browser() || !browser_view->GetWidget()) {
    return;
  }
  auto delegate = std::make_unique<views::DialogDelegate>();
  delegate->SetModalType(ui::mojom::ModalType::kWindow);
  delegate->SetTitle(u"Hey Zep and voices");
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk));
  delegate->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Done");
  delegate->set_margins(gfx::Insets::VH(8, 24));
  delegate->SetContentsView(
      std::make_unique<ZephyrusVoiceSetupDialog>(browser_view));
  constrained_window::CreateBrowserModalDialogViews(
      std::move(delegate), browser_view->GetWidget()->GetNativeWindow())
      ->Show();
}

}  // namespace zephyrus

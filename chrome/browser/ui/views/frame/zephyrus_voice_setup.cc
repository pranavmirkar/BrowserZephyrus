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

#include "base/callback_list.h"
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
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_hands_free.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_controls.h"
#include "chrome/browser/ui/views/frame/zephyrus_voice_access.h"
#include "chrome/browser/zephyrus/agent/audio_devices.h"
#include "chrome/browser/zephyrus/agent/hands_free.h"
#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "chrome/browser/zephyrus/agent/phrase_recorder.h"
#include "chrome/browser/zephyrus/agent/voice_input.h"
#include "chrome/browser/zephyrus/agent/voice_library.h"
#include "chrome/browser/zephyrus/agent/voice_lock.h"
#include "components/prefs/pref_service.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/combobox_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/ui_base_types.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace zephyrus {

namespace {

namespace m3 = zephyrus::m3;
namespace agent = zephyrus::agent;

constexpr int kWidth = 300;
constexpr int kVoiceRowsHeight = 84;
constexpr base::TimeDelta kRecordingTimeout = base::Seconds(90);

class SensitivityModel : public ui::ComboboxModel {
 public:
  size_t GetItemCount() const override { return 3; }
  std::u16string GetItemAt(size_t index) const override {
    switch (index) {
      case 0:
        return u"Strict (noisy room)";
      case 2:
        return u"Relaxed (quiet room)";
      default:
        return u"Balanced";
    }
  }
};

// The microphones, as a list to pick from. Each combobox owns its own copy of the
// list: the model must outlive the control that shows it.
class MicModel : public ui::ComboboxModel {
 public:
  explicit MicModel(std::vector<agent::AudioInputDevice> devices)
      : devices_(std::move(devices)) {}
  size_t GetItemCount() const override { return devices_.size(); }
  std::u16string GetItemAt(size_t index) const override {
    return base::UTF8ToUTF16(devices_[index].name);
  }
  const agent::AudioInputDevice& device(size_t index) const {
    return devices_[index];
  }

 private:
  std::vector<agent::AudioInputDevice> devices_;
};

std::u16string Describe(agent::VoiceLibrary::Result result) {
  switch (result) {
    case agent::VoiceLibrary::Result::kOk:
      return u"Saved.";
    case agent::VoiceLibrary::Result::kFull:
      return u"Zep keeps up to six voices. Delete one to add another.";
    case agent::VoiceLibrary::Result::kBadName:
      return u"Give this voice a name (up to 32 characters).";
    case agent::VoiceLibrary::Result::kDuplicate:
      return u"Someone already has that name.";
    case agent::VoiceLibrary::Result::kNoKeystore:
      return u"The Windows keystore is not available, so nothing was saved.";
    case agent::VoiceLibrary::Result::kNotFound:
      return u"That voice is already gone.";
  }
  return std::u16string();
}

}  // namespace

// The popup's contents. Four screens, one visible at a time; see the header.
class ZephyrusVoicePopup : public views::View {
  METADATA_HEADER(ZephyrusVoicePopup, views::View)

 public:
  explicit ZephyrusVoicePopup(BrowserView* browser_view)
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
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    mic_devices_ = agent::ListAudioInputDevices();
    BuildIntro();
    BuildName();
    BuildRecording();
    BuildReady();

    if (library_) {
      subscription_ = library_->Subscribe(base::BindRepeating(
          &ZephyrusVoicePopup::Refresh, base::Unretained(this)));
      library_->WhenLoaded(base::BindOnce(&ZephyrusVoicePopup::Refresh,
                                          weak_factory_.GetWeakPtr()));
    }
    ShowScreen(HasVoices() ? Screen::kReady : Screen::kIntro);
    RebuildVoicesNow();
  }

  ~ZephyrusVoicePopup() override { StopMic(); }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kWidth,
                     GetLayoutManager()->GetPreferredHeightForWidth(this, kWidth));
  }

 private:
  enum class Screen { kIntro, kName, kRecording, kReady };
  enum class Mode { kIdle, kEnrol, kTest };

  // ---- Building the screens ---------------------------------------------------

  std::unique_ptr<views::Label> MakeText(const std::u16string& text,
                                         m3::Type type,
                                         bool emphasized = false) {
    auto label = std::make_unique<views::Label>(text);
    label->SetFontList(m3::Font(type, emphasized));
    label->SetMultiLine(true);
    label->SetMaximumWidth(kWidth);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    return label;
  }

  // "Microphone" and a list to choose from. There is one on each screen where it
  // matters; choosing in one changes them all, and the choice is remembered.
  std::unique_ptr<views::View> MakeMicRow() {
    auto row = std::make_unique<views::BoxLayoutView>();
    row->SetOrientation(views::BoxLayout::Orientation::kVertical);
    row->SetBetweenChildSpacing(4);
    row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    row->AddChildView(MakeText(u"Microphone", m3::Type::kLabelLarge, true));
    auto combobox = std::make_unique<views::Combobox>(
        std::make_unique<MicModel>(mic_devices_));
    combobox->GetViewAccessibility().SetName(u"Microphone");
    combobox->SetSelectedIndex(SavedMicIndex());
    combobox->SetCallback(base::BindRepeating(
        &ZephyrusVoicePopup::OnMicChosen, base::Unretained(this),
        base::Unretained(combobox.get())));
    mic_boxes_.push_back(row->AddChildView(std::move(combobox)));
    return row;
  }

  size_t SavedMicIndex() const {
    const std::string saved = prefs_->GetString(agent::kMicDevicePref);
    for (size_t i = 0; i < mic_devices_.size(); ++i) {
      if (!saved.empty() && mic_devices_[i].name == saved) {
        return i;
      }
    }
    return 0;
  }

  void OnMicChosen(views::Combobox* from) {
    const size_t index = from->GetSelectedIndex().value_or(0);
    if (index >= mic_devices_.size()) {
      return;
    }
    prefs_->SetString(agent::kMicDevicePref, mic_devices_[index].is_default
                                                 ? std::string()
                                                 : mic_devices_[index].name);
    for (views::Combobox* box : mic_boxes_) {
      if (box != from) {
        box->SetSelectedIndex(index);
      }
    }
    // A recording in progress moves to the new microphone.
    if (mode_ != Mode::kIdle) {
      const Mode mode = mode_;
      if (BeginMic(mode) && mode == Mode::kEnrol) {
        status_recording_->SetText(u"Listening on the new microphone. Say "
                                   u"\"Hey Zep\".");
      }
    }
  }

  views::BoxLayoutView* MakeScreen() {
    auto* screen = AddChildView(std::make_unique<views::BoxLayoutView>());
    screen->SetOrientation(views::BoxLayout::Orientation::kVertical);
    screen->SetBetweenChildSpacing(10);
    screen->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    screen->SetVisible(false);
    return screen;
  }

  std::unique_ptr<views::MdTextButton> MakeButton(const std::u16string& text,
                                                  void (ZephyrusVoicePopup::*fn)(),
                                                  ui::ButtonStyle style) {
    auto button = std::make_unique<views::MdTextButton>(
        base::BindRepeating(fn, base::Unretained(this)), text);
    button->SetStyle(style);
    return button;
  }

  void BuildIntro() {
    intro_ = MakeScreen();
    intro_->AddChildView(MakeText(u"Talk to Zep hands-free", m3::Type::kTitleMedium, true));
    intro_body_ = intro_->AddChildView(MakeText(
        u"Say \"Hey Zep\", then what you want done. First Zep needs to learn "
        u"your voice: you will say \"Hey Zep\" three times.",
        m3::Type::kBodyMedium));
    intro_->AddChildView(MakeMicRow());
    record_first_ = intro_->AddChildView(MakeButton(
        u"Record my voice", &ZephyrusVoicePopup::OnRecordFirst,
        ui::ButtonStyle::kProminent));
    intro_->AddChildView(MakeText(
        u"Your voice stays on this computer. The recordings themselves are "
        u"never saved.",
        m3::Type::kBodySmall));
    if (private_) {
      intro_body_->SetText(
          u"A Private Workspace keeps no voices and does not listen. Open a "
          u"regular window to set this up.");
      record_first_->SetEnabled(false);
    }
  }

  void BuildName() {
    name_screen_ = MakeScreen();
    name_screen_->AddChildView(
        MakeText(u"Who is this?", m3::Type::kTitleMedium, true));
    name_field_ = name_screen_->AddChildView(
        std::make_unique<m3::FilledField>(u"Name", kWidth));
    auto* row = name_screen_->AddChildView(std::make_unique<views::BoxLayoutView>());
    row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    row->SetBetweenChildSpacing(8);
    row->AddChildView(MakeButton(u"Record voice", &ZephyrusVoicePopup::OnRecordNamed,
                                 ui::ButtonStyle::kProminent));
    row->AddChildView(MakeButton(u"Back", &ZephyrusVoicePopup::OnBack,
                                 ui::ButtonStyle::kText));
  }

  void BuildRecording() {
    recording_ = MakeScreen();
    recording_->AddChildView(
        MakeText(u"Say \"Hey Zep\"", m3::Type::kTitleLarge, true));
    dots_ = recording_->AddChildView(MakeText(u"", m3::Type::kHeadlineSmall));
    level_ = recording_->AddChildView(std::make_unique<views::ProgressBar>());
    status_recording_ = recording_->AddChildView(
        MakeText(u"", m3::Type::kBodyMedium));
    recording_->AddChildView(MakeMicRow());
    recording_->AddChildView(MakeButton(u"Cancel", &ZephyrusVoicePopup::OnCancel,
                                        ui::ButtonStyle::kText));
  }

  void BuildReady() {
    ready_ = MakeScreen();
    ready_->AddChildView(MakeText(u"Hey Zep", m3::Type::kTitleMedium, true));

    auto* card = ready_->AddChildView(std::make_unique<m3::Card>(gfx::Insets::VH(2, 0)));
    hands_free_row_ = card->AddChildView(std::make_unique<m3::SwitchRow>(
        u"Say \"Hey Zep\" to talk", u"Zep listens only while this window is in use.",
        prefs_->GetBoolean(agent::kHandsFreePref)));
    hands_free_row_->SetEnabled(!private_);
    hands_free_row_->set_on_change(base::BindRepeating(
        &ZephyrusVoicePopup::OnHandsFreeSwitch, base::Unretained(this)));

    ready_->AddChildView(MakeText(u"Zep answers to", m3::Type::kLabelLarge, true));
    auto* scroll = ready_->AddChildView(std::make_unique<views::ScrollView>(
        views::ScrollView::ScrollWithLayers::kEnabled));
    scroll->SetBackgroundColor(std::nullopt);
    scroll->SetDrawOverflowIndicator(false);
    scroll->SetHorizontalScrollBarMode(views::ScrollView::ScrollBarMode::kDisabled);
    scroll->ClipHeightTo(0, kVoiceRowsHeight);
    auto voices = std::make_unique<views::BoxLayoutView>();
    voices->SetOrientation(views::BoxLayout::Orientation::kVertical);
    voices->SetBetweenChildSpacing(4);
    voices_ = scroll->SetContents(std::move(voices));

    auto* actions = ready_->AddChildView(std::make_unique<views::BoxLayoutView>());
    actions->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
    actions->SetBetweenChildSpacing(8);
    test_ = actions->AddChildView(MakeButton(u"Test", &ZephyrusVoicePopup::OnTest,
                                             ui::ButtonStyle::kTonal));
    add_ = actions->AddChildView(MakeButton(u"Add another person",
                                            &ZephyrusVoicePopup::OnAddPerson,
                                            ui::ButtonStyle::kTonal));
    test_->SetEnabled(!private_);
    add_->SetEnabled(!private_);

    status_ready_ = ready_->AddChildView(MakeText(u"", m3::Type::kBodyMedium));
    status_ready_->SetVisible(false);

    more_toggle_ = ready_->AddChildView(MakeButton(
        u"More options", &ZephyrusVoicePopup::OnToggleMore, ui::ButtonStyle::kText));
    more_ = ready_->AddChildView(std::make_unique<views::BoxLayoutView>());
    more_->SetOrientation(views::BoxLayout::Orientation::kVertical);
    more_->SetBetweenChildSpacing(8);
    more_->SetVisible(false);
    more_->AddChildView(MakeMicRow());
    auto* lock_card = more_->AddChildView(std::make_unique<m3::Card>(gfx::Insets::VH(2, 0)));
    lock_row_ = lock_card->AddChildView(std::make_unique<m3::SwitchRow>(
        u"Only answer my voices",
        u"Voice Lock. A convenience, not security: payments and sign-in always "
        u"need a click.",
        prefs_->GetBoolean(agent::kVoiceLockPref)));
    lock_row_->set_on_change(base::BindRepeating(
        &ZephyrusVoicePopup::OnLockSwitch, base::Unretained(this)));
    more_->AddChildView(MakeText(u"How strict", m3::Type::kLabelLarge, true));
    sensitivity_ = more_->AddChildView(
        std::make_unique<views::Combobox>(std::make_unique<SensitivityModel>()));
    sensitivity_->GetViewAccessibility().SetName(u"How strict the voice match is");
    sensitivity_->SetSelectedIndex(static_cast<size_t>(
        std::clamp(prefs_->GetInteger(agent::kVoiceSensitivityPref), 0, 2)));
    sensitivity_->SetCallback(base::BindRepeating(
        &ZephyrusVoicePopup::OnSensitivity, base::Unretained(this)));
    more_->AddChildView(MakeText(
        u"Shortcuts: Ctrl+Shift+Space talks to Zep without the phrase. "
        u"Ctrl+Shift+Comma mutes it.",
        m3::Type::kBodySmall));
  }

  // ---- Which screen -----------------------------------------------------------

  bool HasVoices() const {
    return library_ && library_->loaded() && !library_->profiles().empty();
  }

  void ShowScreen(Screen screen) {
    screen_ = screen;
    intro_->SetVisible(screen == Screen::kIntro);
    name_screen_->SetVisible(screen == Screen::kName);
    recording_->SetVisible(screen == Screen::kRecording);
    ready_->SetVisible(screen == Screen::kReady);
    if (record_first_ && !private_) {
      record_first_->SetEnabled(library_ && library_->loaded());
    }
    InvalidateLayout();
    // A popup that autosizes follows its contents; this covers the frame where
    // the contents have not been laid out yet.
    if (GetWidget()) {
      if (views::BubbleDialogDelegate* bubble =
              GetWidget()->widget_delegate()->AsBubbleDialogDelegate()) {
        bubble->SizeToContents();
      }
    }
  }

  // Keeps the screen and the list in step with the voices that exist. Never
  // rebuilt in place from an observer or a button: that frees the button being
  // clicked. Always a task later.
  void Refresh() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ZephyrusVoicePopup::RefreshNow,
                                  weak_factory_.GetWeakPtr()));
  }

  void RefreshNow() {
    if (screen_ == Screen::kRecording || screen_ == Screen::kName) {
      return;
    }
    RebuildVoicesNow();
    ShowScreen(HasVoices() ? Screen::kReady : Screen::kIntro);
  }

  void RebuildVoicesNow() {
    voices_->RemoveAllChildViews();
    if (!library_ || !library_->loaded()) {
      return;
    }
    for (const agent::voice::VoiceProfile& profile : library_->profiles()) {
      const std::string id = profile.id;
      auto* row = voices_->AddChildView(std::make_unique<views::BoxLayoutView>());
      row->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
      row->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
      auto* name = row->AddChildView(MakeText(
          base::UTF8ToUTF16(profile.name), m3::Type::kBodyLarge));
      name->SetMultiLine(false);
      row->SetFlexForView(name, 1);
      auto* remove = row->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&ZephyrusVoicePopup::OnDelete,
                              base::Unretained(this), id),
          u"Delete"));
      remove->SetStyle(ui::ButtonStyle::kText);
    }
  }

  void SayReady(const std::u16string& text) {
    status_ready_->SetText(text);
    status_ready_->SetVisible(!text.empty());
    InvalidateLayout();
  }

  // ---- The switches, written as they are changed ----------------------------

  void OnHandsFreeSwitch() {
    const bool on = hands_free_row_->is_on();
    prefs_->SetBoolean(agent::kHandsFreePref, on);
    if (on && !HasVoices()) {
      SayReady(u"Record your voice first: Zep only answers voices it knows.");
    } else {
      SayReady(on ? u"Hands-free is on. Just say \"Hey Zep\"." : u"Hands-free is off.");
    }
  }

  void OnLockSwitch() {
    prefs_->SetBoolean(agent::kVoiceLockPref, lock_row_->is_on());
  }

  void OnSensitivity() {
    prefs_->SetInteger(agent::kVoiceSensitivityPref,
                       static_cast<int>(sensitivity_->GetSelectedIndex().value_or(1)));
  }

  void OnToggleMore() {
    const bool show = !more_->GetVisible();
    more_->SetVisible(show);
    more_toggle_->SetText(show ? u"Fewer options" : u"More options");
    InvalidateLayout();
    if (GetWidget()) {
      if (views::BubbleDialogDelegate* bubble =
              GetWidget()->widget_delegate()->AsBubbleDialogDelegate()) {
        bubble->SizeToContents();
      }
    }
  }

  // ---- Buttons -----------------------------------------------------------------

  void OnRecordFirst() { StartEnrol("Me"); }

  void OnAddPerson() {
    if (library_ && library_->profiles().size() >= agent::voice::kMaxProfiles) {
      SayReady(Describe(agent::VoiceLibrary::Result::kFull));
      return;
    }
    name_field_->field()->SetText(u"Person " + base::NumberToString16(
        (library_ ? library_->profiles().size() : 0) + 1));
    ShowScreen(Screen::kName);
    name_field_->field()->SelectAll(false);
    name_field_->field()->RequestFocus();
  }

  void OnRecordNamed() {
    const std::string name = agent::VoiceLibrary::CleanName(
        base::UTF16ToUTF8(name_field_->field()->GetText()));
    if (name.empty()) {
      name_field_->field()->SetText(u"");
      return;
    }
    StartEnrol(name);
  }

  void OnBack() { ShowScreen(HasVoices() ? Screen::kReady : Screen::kIntro); }

  void OnCancel() {
    StopMic();
    ShowScreen(HasVoices() ? Screen::kReady : Screen::kIntro);
  }

  void OnDelete(const std::string& id) {
    if (!library_) {
      return;
    }
    const agent::VoiceLibrary::Result result = library_->Remove(id);
    SayReady(result == agent::VoiceLibrary::Result::kOk
                 ? u"Deleted. Its recording is gone from this computer."
                 : Describe(result));
  }

  // ---- Recording ----------------------------------------------------------------

  void StartEnrol(const std::string& name) {
    if (!library_ || !library_->loaded()) {
      return;
    }
    if (library_->profiles().size() >= agent::voice::kMaxProfiles) {
      SayReady(Describe(agent::VoiceLibrary::Result::kFull));
      ShowScreen(Screen::kReady);
      return;
    }
    new_name_ = name;
    takes_.clear();
    ShowScreen(Screen::kRecording);
    if (!BeginMic(Mode::kEnrol)) {
      return;
    }
    UpdateDots();
    status_recording_->SetText(
        u"Say it the way you will normally say it. Zep is listening.");
  }

  void UpdateDots() {
    std::u16string dots;
    for (size_t i = 0; i < agent::voice::kEnrollmentSamples; ++i) {
      dots += i < takes_.size() ? u"● " : u"○ ";
    }
    dots_->SetText(dots);
  }

  void OnTest() {
    if (mode_ == Mode::kTest) {
      StopMic();
      SayReady(u"");
      return;
    }
    if (!HasVoices()) {
      SayReady(u"Record your voice first.");
      return;
    }
    if (!BeginMic(Mode::kTest)) {
      return;
    }
    level_->SetVisible(false);
    test_->SetText(u"Stop");
    SayReady(u"Say \"Hey Zep\" or \"Hey Zep, what is the weather\".");
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
    mic_->SetDeviceName(prefs_->GetString(agent::kMicDevicePref));
    heard_something_ = false;
    if (!mic_->StartStreaming(base::BindRepeating(
            &ZephyrusVoicePopup::OnAudio, base::Unretained(this)))) {
      ResumeHandsFree();
      const std::u16string message =
          u"Zep could not open the microphone. Check that Windows lets desktop "
          u"apps use it (Settings > Privacy > Microphone).";
      if (screen_ == Screen::kRecording) {
        ShowScreen(HasVoices() ? Screen::kReady : Screen::kIntro);
      }
      SayReady(message);
      return false;
    }
    mode_ = mode;
    level_->SetValue(0);
    // Four seconds of nothing at all from a microphone means it is the wrong
    // one (muted, a headset in a case, a virtual input with nothing feeding it),
    // and the person is left talking to a screen that does not react.
    silence_timer_.Start(FROM_HERE, base::Seconds(4),
                         base::BindOnce(&ZephyrusVoicePopup::OnSilence,
                                        base::Unretained(this)));
    timeout_.Start(FROM_HERE, kRecordingTimeout,
                   base::BindOnce(&ZephyrusVoicePopup::OnTimeout,
                                  base::Unretained(this)));
    return true;
  }

  void OnSilence() {
    if (heard_something_ || mode_ == Mode::kIdle) {
      return;
    }
    const std::u16string message =
        u"Zep is not hearing anything from this microphone. Choose another "
        u"one below, or check that it is not muted.";
    if (mode_ == Mode::kEnrol) {
      status_recording_->SetText(message);
    } else {
      SayReady(message);
    }
  }

  void StopMic() {
    timeout_.Stop();
    silence_timer_.Stop();
    if (mic_ && mic_->streaming()) {
      mic_->StopStreaming();
    }
    mode_ = Mode::kIdle;
    if (test_) {
      test_->SetText(u"Test");
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
    const Screen was = screen_;
    StopMic();
    if (was == Screen::kRecording) {
      ShowScreen(HasVoices() ? Screen::kReady : Screen::kIntro);
    }
    SayReady(u"Stopped: Zep did not hear anything for a while.");
  }

  void OnAudio(base::span<const int16_t> pcm) {
    std::optional<std::vector<int16_t>> utterance = recorder_.Feed(pcm);
    if (recorder_.level() > 0.02f || recorder_.hearing_speech()) {
      heard_something_ = true;
    }
    if (level_->GetVisible()) {
      level_->SetValue(recorder_.level());
    }
    if (utterance) {
      // May stop the stream; nothing after this touches it.
      OnUtterance(std::move(*utterance));
    }
  }

  void OnUtterance(std::vector<int16_t> pcm) {
    if (mode_ == Mode::kEnrol) {
      agent::voice::SampleCheck check =
          agent::voice::AnalyseEnrollmentSample(pcm, takes_);
      if (check.problem != agent::voice::SampleProblem::kNone) {
        status_recording_->SetText(
            base::UTF8ToUTF16(agent::voice::DescribeProblem(check.problem)) +
            u" Try again.");
        return;
      }
      takes_.push_back(std::move(check.sample));
      UpdateDots();
      if (takes_.size() < agent::voice::kEnrollmentSamples) {
        status_recording_->SetText(u"Good. Once more.");
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
        SayReady(u"Heard \"Hey Zep\" from " +
                 base::UTF8ToUTF16(library_->profiles()[match.profile].name) + u".");
      } else if (match.wake) {
        SayReady(u"That sounded like \"Hey Zep\", but not like a voice Zep "
                 u"knows, so it would be ignored.");
      } else {
        SayReady(u"That did not sound like \"Hey Zep\". Say just those two "
                 u"words, clearly.");
      }
    }
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
      // Recording a voice is asking for this: switch hands-free on, so the
      // person is not left to find a second switch.
      if (!private_) {
        prefs_->SetBoolean(agent::kHandsFreePref, true);
        hands_free_row_->SetOn(true);
      }
      RebuildVoicesNow();
      ShowScreen(Screen::kReady);
      SayReady(u"Saved. Hands-free is on: just say \"Hey Zep\".");
    } else {
      ShowScreen(HasVoices() ? Screen::kReady : Screen::kIntro);
      SayReady(Describe(result));
    }
  }

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<PrefService> prefs_ = nullptr;
  raw_ptr<agent::VoiceLibrary> library_ = nullptr;
  base::WeakPtr<agent::ZephyrusHandsFree> hands_free_;
  bool private_ = false;
  bool paused_ = false;
  Screen screen_ = Screen::kIntro;

  raw_ptr<views::BoxLayoutView> intro_ = nullptr;
  raw_ptr<views::Label> intro_body_ = nullptr;
  raw_ptr<views::MdTextButton> record_first_ = nullptr;
  raw_ptr<views::BoxLayoutView> name_screen_ = nullptr;
  raw_ptr<m3::FilledField> name_field_ = nullptr;
  raw_ptr<views::BoxLayoutView> recording_ = nullptr;
  raw_ptr<views::Label> dots_ = nullptr;
  raw_ptr<views::ProgressBar> level_ = nullptr;
  raw_ptr<views::Label> status_recording_ = nullptr;
  raw_ptr<views::BoxLayoutView> ready_ = nullptr;
  raw_ptr<m3::SwitchRow> hands_free_row_ = nullptr;
  raw_ptr<views::View> voices_ = nullptr;
  raw_ptr<views::MdTextButton> test_ = nullptr;
  raw_ptr<views::MdTextButton> add_ = nullptr;
  raw_ptr<views::Label> status_ready_ = nullptr;
  raw_ptr<views::MdTextButton> more_toggle_ = nullptr;
  raw_ptr<views::BoxLayoutView> more_ = nullptr;
  raw_ptr<m3::SwitchRow> lock_row_ = nullptr;
  raw_ptr<views::Combobox> sensitivity_ = nullptr;

  std::vector<agent::AudioInputDevice> mic_devices_;
  std::vector<raw_ptr<views::Combobox>> mic_boxes_;
  bool heard_something_ = false;
  base::OneShotTimer silence_timer_;

  std::unique_ptr<agent::VoiceInput> mic_;
  agent::voice::PhraseRecorder recorder_;
  Mode mode_ = Mode::kIdle;
  std::string new_name_;
  std::vector<agent::voice::WakeTemplate> takes_;
  base::OneShotTimer timeout_;
  base::CallbackListSubscription subscription_;

  base::WeakPtrFactory<ZephyrusVoicePopup> weak_factory_{this};
};

BEGIN_METADATA(ZephyrusVoicePopup)
END_METADATA

namespace {
// Declared in this order so the widget is destroyed before its delegate.
struct PopupHolder {
  std::unique_ptr<views::BubbleDialogDelegate> delegate;
  std::unique_ptr<views::Widget> widget;
};
}  // namespace

void ShowVoiceSetup(BrowserView* browser_view, views::View* anchor) {
  if (!browser_view || !browser_view->browser() || !browser_view->GetWidget() ||
      !anchor || !anchor->GetWidget()) {
    return;
  }
  // Anchored to the button that opened it, and small: it is a control, not a
  // page. The same shape as every other Zephyrus popup.
  auto delegate = std::make_unique<views::BubbleDialogDelegate>(
      anchor, views::BubbleBorder::TOP_RIGHT, views::BubbleBorder::STANDARD_SHADOW,
      /*autosize=*/true);
  zephyrus::ConfigureBubble(delegate.get());
  delegate->SetShowCloseButton(false);
  delegate->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  delegate->set_margins(gfx::Insets::VH(16, 16));
  delegate->set_fixed_width(kWidth + 32);
  delegate->SetBackgroundColor(m3::Role(*anchor, kColorZephyrusSurfaceContainer));
  delegate->SetContentsView(std::make_unique<ZephyrusVoicePopup>(browser_view));

  // The popup owns itself: the delegate must outlive the widget, and the widget
  // may only be deleted after it has closed, never inside its own close.
  auto* holder = new PopupHolder;
  holder->delegate = std::move(delegate);
  views::BubbleDialogDelegate* raw = holder->delegate.get();
  holder->widget = views::BubbleDialogDelegate::CreateBubble(
      raw, base::BindOnce(
               [](PopupHolder* holder, views::Widget::ClosedReason) {
                 base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
                     FROM_HERE, holder);
               },
               holder));
  if (!holder->widget) {
    delete holder;
    return;
  }
  zephyrus::ApplyBubbleFrame(raw);
  holder->widget->Show();
}

}  // namespace zephyrus

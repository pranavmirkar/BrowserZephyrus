// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_settings.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "ui/display/screen.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/controls/scroll_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_memory_access.h"
#include "chrome/browser/zephyrus/agent/agent_memory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_controls.h"
#include "chrome/browser/ui/views/frame/zephyrus_voice_setup.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "chrome/browser/zephyrus/agent/bundled_keys.h"
#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_service.h"
#include "net/base/url_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/combobox_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace {

namespace m3 = zephyrus::m3;

constexpr int kFieldWidth = 360;
constexpr int kBodyInset = 20;

struct Provider {
  const char* kind;
  const char16_t* name;
  // Filled in as real text, not a placeholder. A placeholder that reads like a
  // model id looked filled in, and Save then said the field was empty.
  const char16_t* default_model;
};

constexpr std::array<Provider, 3> kProviders = {{
    {"anthropic", u"Anthropic", u"claude-sonnet-5-5"},
    {"openai", u"OpenAI-compatible (OpenAI, OpenRouter, a local server)", u""},
    {"gemini", u"Google Gemini", u""},
}};

class ProviderModel : public ui::ComboboxModel {
 public:
  size_t GetItemCount() const override { return kProviders.size(); }
  std::u16string GetItemAt(size_t index) const override {
    return kProviders[index].name;
  }
};

size_t IndexOf(const std::string& kind) {
  for (size_t i = 0; i < kProviders.size(); ++i) {
    if (kind == kProviders[i].kind) {
      return i;
    }
  }
  return 0;
}

}  // namespace

// At global scope: BubbleDialogDelegateView's constructor is private and its
// friend list names this class as ::ZephyrusAgentSettings.
class ZephyrusAgentSettings : public views::BubbleDialogDelegateView {
  METADATA_HEADER(ZephyrusAgentSettings, views::BubbleDialogDelegateView)

 public:
  ZephyrusAgentSettings(BrowserView* browser_view, views::View* anchor)
      : views::BubbleDialogDelegateView(anchor,
                                        views::BubbleBorder::TOP_RIGHT),
        browser_view_(browser_view) {
    zephyrus::ConfigureBubble(this);
    set_margins(gfx::Insets::VH(8, kBodyInset));
    set_fixed_width(kFieldWidth + 2 * kBodyInset);
    SetBackgroundColor(m3::Role(*anchor, kColorZephyrusSurfaceContainer));
    SetTitle(u"Agent model");
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
               static_cast<int>(ui::mojom::DialogButton::kCancel));
    SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
    SetAcceptCallbackWithClose(base::BindRepeating(
        &ZephyrusAgentSettings::Save, base::Unretained(this)));

    Profile* profile = browser_view_->browser()->profile();
    prefs_ = profile->GetPrefs();
    private_ = profile->IsOffTheRecord();
    if (auto* manager = browser_view_->zephyrus_workspace_manager()) {
      workspace_id_ = manager->current_workspace_id();
    }

    // The keystore answers asynchronously but at once; saving waits on it
    // rather than holding a pointer to the profile past this dialog.
    if (g_browser_process && g_browser_process->os_crypt_async()) {
      g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
          &ZephyrusAgentSettings::OnEncryptor, weak_factory_.GetWeakPtr()));
    }

    const std::optional<zephyrus::agent::CloudModelConfig> saved =
        zephyrus::agent::ReadCloudModelConfig(*prefs_);

    // The fields live in a body that scrolls, so the bubble is never taller than
    // the screen it is on -- it had grown past it -- while the title and the
    // Save / Cancel buttons stay where they are.
    SetLayoutManager(std::make_unique<views::FillLayout>());
    auto* scroll = AddChildView(std::make_unique<views::ScrollView>(
        views::ScrollView::ScrollWithLayers::kEnabled));
    scroll->SetBackgroundColor(std::nullopt);
    scroll->SetDrawOverflowIndicator(false);
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll->ClipHeightTo(0, MaxBodyHeight(anchor));
    auto body_owned = std::make_unique<views::View>();
    body_owned->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    views::View* body = scroll->SetContents(std::move(body_owned));

    // Only a build that ships provided keys has this switch. It is the answer to
    // "whose key is the agent using": on, the build's own (through its demo
    // service); off, only the user's.
    if (zephyrus::agent::HasBundledKey(zephyrus::agent::kBundledDefaultKind) ||
        zephyrus::agent::HasBundledKey(zephyrus::agent::kVoiceKeyKind)) {
      auto* provided_card = body->AddChildView(
          std::make_unique<m3::Card>(gfx::Insets::VH(4, 0)));
      provided_ = provided_card->AddChildView(std::make_unique<m3::SwitchRow>(
          u"Use Zephyrus provided keys",
          u"For AssemblyAI - Voice Agent Hackathon",
          prefs_->GetBoolean(zephyrus::agent::kUseProvidedKeysPref)));
      provided_->set_on_change(base::BindRepeating(
          &ZephyrusAgentSettings::OnProvidedChanged, base::Unretained(this)));
    }

    own_keys_text_ = body->AddChildView(MakeText(
        u"Use your own API key. It is encrypted by Windows and sent only to "
        u"the provider you choose.",
        m3::Type::kBodySmall));

    provider_label_ = body->AddChildView(
        std::make_unique<m3::SectionLabel>(u"Provider"));
    provider_ = body->AddChildView(std::make_unique<views::Combobox>(
        std::make_unique<ProviderModel>()));
    provider_->GetViewAccessibility().SetName(u"Provider");
    provider_->SetSelectedIndex(IndexOf(saved ? saved->kind : "anthropic"));
    provider_->SetCallback(base::BindRepeating(
        &ZephyrusAgentSettings::OnProviderChanged, base::Unretained(this)));

    model_ = body->AddChildView(
        std::make_unique<m3::FilledField>(u"Model", kFieldWidth));
    model_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    if (saved) {
      model_->field()->SetText(base::UTF8ToUTF16(saved->model));
    }

    endpoint_ = body->AddChildView(
        std::make_unique<m3::FilledField>(u"Endpoint", kFieldWidth));
    endpoint_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    endpoint_->field()->SetText(base::UTF8ToUTF16(
        saved && saved->kind == "openai"
            ? saved->base_url.spec()
            : zephyrus::agent::DefaultBaseUrl("openai").spec()));

    key_ = body->AddChildView(
        std::make_unique<m3::FilledField>(u"API key", kFieldWidth));
    key_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    key_->field()->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);

    voice_key_ = body->AddChildView(std::make_unique<m3::FilledField>(
        u"AssemblyAI key, for voice commands", kFieldWidth));
    voice_key_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    voice_key_->field()->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
    voice_key_->field()->SetPlaceholderText(
        zephyrus::agent::HasSavedKey(*prefs_, zephyrus::agent::kVoiceKeyKind)
            ? u"Saved. Leave empty to keep it"
            : u"Optional: paste to speak tasks");

    limit_ = body->AddChildView(std::make_unique<m3::FilledField>(
        u"Spending limit per task, US$", kFieldWidth));
    limit_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(6, 0, 0, 0));
    limit_->field()->SetText(base::UTF8ToUTF16(base::NumberToString(
        saved ? saved->max_usd_per_task : 1.0)));

    body->AddChildView(std::make_unique<m3::SectionLabel>(u"This workspace"));
    auto* card = body->AddChildView(
        std::make_unique<m3::Card>(gfx::Insets::VH(4, 0)));
    allow_here_ = card->AddChildView(std::make_unique<m3::SwitchRow>(
        u"Use the cloud model here", std::u16string(),
        !private_ &&
            zephyrus::agent::IsCloudAllowedInWorkspace(*prefs_, workspace_id_)));
    allow_here_->SetEnabled(!private_);
    screenshots_ = card->AddChildView(std::make_unique<m3::SwitchRow>(
        u"Show the model screenshots", std::u16string(),
        !saved || saved->send_screenshots));
    mascot_ = card->AddChildView(std::make_unique<m3::SwitchRow>(
        u"Show the mascot", std::u16string(),
        prefs_->GetBoolean(zephyrus::agent::kMascotPref)));
    memory_ = card->AddChildView(std::make_unique<m3::SwitchRow>(
        u"Remember what I tell it, across chats", std::u16string(),
        prefs_->GetBoolean(zephyrus::agent::kMemoryPref)));
    // One row for both what is remembered and how to forget it, at the size of
    // the other actions, rather than a paragraph and a button of its own.
    forget_memory_ = body->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&ZephyrusAgentSettings::ForgetMemory,
                            base::Unretained(this)),
        std::u16string()));
    forget_memory_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));
    forget_memory_->SetProperty(views::kCrossAxisAlignmentKey,
                                views::LayoutAlignment::kStart);
    RefreshMemoryCount();
    // Hands-free voice ("Hey Zep") and who it answers to have a dialog of their
    // own: recording a voice needs the microphone and room to explain itself.
    voices_button_ = body->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&ZephyrusAgentSettings::OpenVoiceSetup,
                            base::Unretained(this)),
        u"Hey Zep and voices..."));
    voices_button_->SetProperty(views::kMarginsKey,
                                gfx::Insets::TLBR(4, 0, 0, 0));
    voices_button_->SetProperty(views::kCrossAxisAlignmentKey,
                                views::LayoutAlignment::kStart);
    privacy_ = body->AddChildView(MakeText(std::u16string(), m3::Type::kBodySmall));

    forget_ = body->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&ZephyrusAgentSettings::ForgetKey,
                            base::Unretained(this)),
        u"Remove saved key"));
    forget_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(8, 0, 0, 0));
    forget_->SetProperty(views::kCrossAxisAlignmentKey,
                         views::LayoutAlignment::kStart);

    error_ = body->AddChildView(MakeText(std::u16string(), m3::Type::kBodyMedium));
    error_->SetVisible(false);

    OnProviderChanged();
    OnProvidedChanged();
  }

  // The tallest the body may be: the room the bubble really has, less the title,
  // the buttons and the bubble's own margins. Two limits, and the smaller wins --
  // the display below the anchor, and the browser window it belongs to, which on
  // a laptop is the tighter one. Never more than 520: past that a settings
  // sheet is a page, and it scrolls.
  static int MaxBodyHeight(views::View* anchor) {
    int room = 520;
    if (anchor && anchor->GetWidget()) {
      views::Widget* widget = anchor->GetWidget();
      room = std::min(room, widget->GetWindowBoundsInScreen().height() - 240);
      if (display::Screen* screen = display::Screen::Get()) {
        room = std::min(
            room, screen->GetDisplayNearestWindow(widget->GetNativeWindow())
                          .work_area()
                          .height() -
                      300);
      }
    }
    return std::max(room, 200);
  }

  void OnThemeChanged() override {
    views::BubbleDialogDelegateView::OnThemeChanged();
    if (error_) {
      error_->SetEnabledColor(m3::Role(*this, kColorZephyrusError));
    }
  }

 private:
  std::unique_ptr<views::Label> MakeText(const std::u16string& text,
                                         m3::Type type) {
    auto label = std::make_unique<views::Label>(text);
    label->SetFontList(m3::Font(type));
    label->SetMultiLine(true);
    label->SetMaximumWidth(kFieldWidth);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 4, 4, 4));
    return label;
  }

  std::string SelectedKind() const {
    return kProviders[provider_->GetSelectedIndex().value_or(0)].kind;
  }

  void OnProviderChanged() {
    const size_t index = provider_->GetSelectedIndex().value_or(0);
    const std::string kind = kProviders[index].kind;
    // Switching provider replaces a model id only if it was empty or another
    // provider's default; never one the user typed.
    const std::u16string current(model_->field()->GetText());
    bool is_a_default = current.empty();
    for (const Provider& provider : kProviders) {
      is_a_default |= current == provider.default_model;
    }
    if (is_a_default) {
      model_->field()->SetText(kProviders[index].default_model);
    }
    model_->field()->SetPlaceholderText(u"Type the model's id");
    // Only an OpenAI-compatible service is the user's to point somewhere.
    endpoint_->SetVisible(kind == "openai");
    const bool has_key = zephyrus::agent::HasSavedKey(*prefs_, kind);
    key_->field()->SetPlaceholderText(has_key ? u"Saved. Leave empty to keep it"
                                              : u"Paste your API key");
    forget_->SetVisible(has_key);
    privacy_->SetText(
        private_ ? u"Private Workspace never sends pages to a cloud model."
        : ProvidedKeysOn()
            ? u"While a task runs, pages in this workspace are sent to "
              u"Anthropic through the Zephyrus hackathon service. Passwords "
              u"and card numbers never are."
            : u"While a task runs, pages in this workspace are sent to " +
                  std::u16string(kProviders[index].name) +
                  u". Passwords and card numbers never are.");
    // Not while the constructor is still running: there is no widget yet,
    // and sizing one that does not exist is a null dereference.
    if (GetWidget()) {
      SizeToContents();
    }
  }

  void OnEncryptor(scoped_refptr<os_crypt_async::Encryptor> encryptor) {
    encryptor_ = std::move(encryptor);
  }

  bool ProvidedKeysOn() const { return provided_ && provided_->is_on(); }

  // With provided keys on, the user's own keys cannot be entered: the fields
  // stay in view but are disabled, and say what to do. A judge has to switch the
  // provided keys off to use their own. The spending limit and the workspace
  // switches apply either way.
  void OnProvidedChanged() {
    const bool own = !ProvidedKeysOn();
    own_keys_text_->SetText(
        own ? u"Use your own API key. It is encrypted by Windows and sent only "
              u"to the provider you choose."
            : u"Turn off Zephyrus provided keys above to use your own API key.");
    provider_->SetEnabled(own);
    model_->SetInputEnabled(own);
    endpoint_->SetInputEnabled(own);
    key_->SetInputEnabled(own);
    voice_key_->SetInputEnabled(own);
    forget_->SetEnabled(own);
    OnProviderChanged();  // endpoint shown for OpenAI only, and the privacy line
    if (GetWidget()) {
      SizeToContents();
    }
  }

  void ForgetKey() {
    zephyrus::agent::ForgetApiKey(*prefs_, SelectedKind());
    OnProviderChanged();
  }

  // The profile's long-term memory, looked up when needed and not held: this
  // dialog is short-lived and the memory is not its to keep.
  zephyrus::agent::LongTermMemory* Memory() {
    Profile* profile = browser_view_ && browser_view_->browser()
                           ? browser_view_->browser()->profile()
                           : nullptr;
    return zephyrus::agent::GetLongTermMemory(profile);
  }

  void RefreshMemoryCount() {
    zephyrus::agent::LongTermMemory* memory = Memory();
    const size_t count = memory ? memory->Count(workspace_id_) : 0;
    forget_memory_->SetText(
        count == 0 ? u"Nothing remembered here yet"
                   : base::StrCat({u"Forget what it remembers (",
                                   base::NumberToString16(count), u")"}));
    forget_memory_->SetEnabled(count > 0);
  }

  void OpenVoiceSetup() {
    // This bubble closes itself when the dialog takes focus; nothing here
    // touches it afterwards.
    zephyrus::ShowVoiceSetup(browser_view_);
  }

  void ForgetMemory() {
    if (zephyrus::agent::LongTermMemory* memory = Memory()) {
      memory->Clear(workspace_id_);
    }
    RefreshMemoryCount();
  }

  void ShowError(const std::u16string& text) {
    error_->SetText(text);
    error_->SetVisible(true);
    // Not while the constructor is still running: there is no widget yet,
    // and sizing one that does not exist is a null dereference.
    if (GetWidget()) {
      SizeToContents();
    }
  }

  // Returns false, keeping the dialog open, when something needs fixing.
  bool Save() {
    zephyrus::agent::CloudModelConfig config;
    const bool provided = ProvidedKeysOn();
    // Provided keys are for one provider; the hidden fields are not the user's
    // choice while the switch is on.
    config.kind = provided ? zephyrus::agent::kBundledDefaultKind
                           : SelectedKind();
    config.model = base::UTF16ToUTF8(
        base::TrimWhitespace(model_->field()->GetText(), base::TRIM_ALL));
    if (provided && (SelectedKind() != zephyrus::agent::kBundledDefaultKind ||
                     config.model.empty())) {
      config.model = zephyrus::agent::kBundledDefaultModel;
    }
    if (config.model.empty() || config.model.size() > 128) {
      ShowError(u"Enter the model's id.");
      return false;
    }
    config.base_url = config.kind == "openai"
                          ? GURL(base::UTF16ToUTF8(base::TrimWhitespace(
                                endpoint_->field()->GetText(), base::TRIM_ALL)))
                          : zephyrus::agent::DefaultBaseUrl(config.kind);
    const bool local = net::IsLocalhost(config.base_url);
    if (!config.base_url.is_valid() ||
        !(config.base_url.SchemeIs("https") ||
          (config.base_url.SchemeIs("http") && local))) {
      ShowError(u"The endpoint must be an https address (or http on this "
                u"computer).");
      return false;
    }
    double limit = 0;
    if (!base::StringToDouble(
            base::UTF16ToUTF8(base::TrimWhitespace(limit_->field()->GetText(),
                                                   base::TRIM_ALL)),
            &limit) ||
        limit < 0.01 || limit > 100) {
      ShowError(u"Set a spending limit between 0.01 and 100 dollars.");
      return false;
    }
    config.max_usd_per_task = limit;
    config.send_screenshots = screenshots_->is_on();
    if (provided_) {
      prefs_->SetBoolean(zephyrus::agent::kUseProvidedKeysPref,
                         provided_->is_on());
    }
    prefs_->SetBoolean(zephyrus::agent::kMascotPref, mascot_->is_on());
    prefs_->SetBoolean(zephyrus::agent::kMemoryPref, memory_->is_on());

    const std::string key = base::UTF16ToUTF8(
        base::TrimWhitespace(key_->field()->GetText(), base::TRIM_ALL));
    // Disabled fields are not saved: a key typed before the switch went on is
    // dropped, not stored behind the user's back.
    if (!key.empty() && !provided) {
      if (!encryptor_ ||
          !zephyrus::agent::StoreApiKey(*prefs_, *encryptor_, config.kind,
                                        key)) {
        ShowError(u"The system keystore is not available, so the key was not "
                  u"saved.");
        return false;
      }
    }
    const std::string voice_key = base::UTF16ToUTF8(
        base::TrimWhitespace(voice_key_->field()->GetText(), base::TRIM_ALL));
    if (!voice_key.empty() && !provided &&
        (!encryptor_ ||
         !zephyrus::agent::StoreApiKey(*prefs_, *encryptor_,
                                       zephyrus::agent::kVoiceKeyKind,
                                       voice_key))) {
      ShowError(u"The system keystore is not available, so the voice key was "
                u"not saved.");
      return false;
    }
    // Scrubbed from the fields whatever happens next.
    key_->field()->SetText(std::u16string());
    voice_key_->field()->SetText(std::u16string());

    zephyrus::agent::WriteCloudModelConfig(*prefs_, config);
    if (!private_) {
      zephyrus::agent::SetCloudAllowedInWorkspace(*prefs_, workspace_id_,
                                                  allow_here_->is_on());
    }
    return true;
  }

  const raw_ptr<BrowserView> browser_view_;
  raw_ptr<PrefService> prefs_ = nullptr;
  bool private_ = false;
  int workspace_id_ = 0;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;

  raw_ptr<m3::SwitchRow> provided_ = nullptr;
  raw_ptr<views::Label> own_keys_text_ = nullptr;
  raw_ptr<views::Label> provider_label_ = nullptr;
  raw_ptr<views::Combobox> provider_ = nullptr;
  raw_ptr<m3::FilledField> model_ = nullptr;
  raw_ptr<m3::FilledField> endpoint_ = nullptr;
  raw_ptr<m3::FilledField> key_ = nullptr;
  raw_ptr<m3::FilledField> voice_key_ = nullptr;
  raw_ptr<m3::FilledField> limit_ = nullptr;
  raw_ptr<m3::SwitchRow> allow_here_ = nullptr;
  raw_ptr<m3::SwitchRow> screenshots_ = nullptr;
  raw_ptr<m3::SwitchRow> mascot_ = nullptr;
  raw_ptr<m3::SwitchRow> memory_ = nullptr;
  raw_ptr<views::MdTextButton> forget_memory_ = nullptr;
  raw_ptr<views::MdTextButton> voices_button_ = nullptr;
  raw_ptr<views::Label> privacy_ = nullptr;
  raw_ptr<views::MdTextButton> forget_ = nullptr;
  raw_ptr<views::Label> error_ = nullptr;

  base::WeakPtrFactory<ZephyrusAgentSettings> weak_factory_{this};
};

BEGIN_METADATA(ZephyrusAgentSettings)
END_METADATA

namespace zephyrus {

void ShowAgentSettings(BrowserView* browser_view, views::View* anchor) {
  if (!browser_view || !anchor || !anchor->GetWidget()) {
    return;
  }
  views::BubbleDialogDelegateView::CreateBubble(
      std::make_unique<ZephyrusAgentSettings>(browser_view, anchor))
      ->Show();
}

}  // namespace zephyrus

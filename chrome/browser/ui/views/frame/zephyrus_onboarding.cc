// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_onboarding.h"

#include <array>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/strings/string_number_conversions.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/themes/theme_service.h"
#include "chrome/browser/themes/theme_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_mascot_overlay.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_settings.h"
#include "chrome/browser/ui/views/frame/zephyrus_voice_setup.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_controls.h"
#include "chrome/browser/ui/views/frame/zephyrus_onboarding_sky.h"
#include "chrome/browser/ui/views/frame/zephyrus_search_engine_picker.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_setup.h"
#include "chrome/browser/zephyrus/agent/bundled_keys.h"
#include "chrome/browser/zephyrus/agent/chrome_bookmarks_reader.h"
#include "chrome/browser/zephyrus/agent/lens_model.h"
#include "chrome/browser/zephyrus/agent/lens_mood_map.h"
#include "chrome/browser/zephyrus/agent/lens_painter.h"
#include "chrome/browser/zephyrus/agent/lens_random.h"
#include "chrome/browser/zephyrus/agent/mascot_rig.h"
#include "chrome/browser/zephyrus/agent/model_settings.h"
#include "chrome/browser/zephyrus/buildflags/dev_switches.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/shadow_value.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/view_tracker.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/view_utils.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"

namespace zephyrus {

namespace {

using zephyrus::agent::OnboardingStep;

// The column the screens sit in. Wide enough for a card of controls, narrow
// enough to read; the controls sit in a glass card inset by kCardPadding.
constexpr int kColumnWidth = 708;
constexpr int kCardPadding = 28;
// Room around the card, inside its view, for its shadow: a view cannot paint
// outside its own bounds, and a shadow clipped there showed as a pale strip.
constexpr int kCardShadow = 14;
constexpr int kContentWidth = kColumnWidth - 2 * (kCardPadding + kCardShadow);

struct StepText {
  const char16_t* title;
  const char16_t* body;
  const char16_t* next;
};

// The words of each screen, in order. Short on purpose: the controls carry the
// detail, and every choice can be changed later in Settings.
constexpr std::array<StepText, agent::kOnboardingStepCount> kSteps = {{
    {u"Hello. Meet Zephyrus.",
     u"A quiet, private browser with Zep, an assistant that does tasks on the "
     u"web for you. Five short steps and you are flying.",
     u"Get started"},
    {u"Make it yours",
     u"Pick how Zep looks and whether the browser is light or dark. You can "
     u"change any of this later in Settings.",
     u"Next"},
    {u"Spaces for every part of you",
     u"A workspace keeps its own tabs, its own sign-ins and its own colour, so "
     u"work, home and study never spill into each other.",
     u"Next"},
    {u"Set up your assistant",
     u"Choose how Zep reaches its model, and teach it your voice so you can "
     u"just say \"Hey Zep\". Both are optional.",
     u"Next"},
    {u"Your privacy, your things",
     u"Decide what gets blocked, pick a search engine, and bring your bookmarks "
     u"over from Chrome.",
     u"Finish"},
}};

}  // namespace

// Text on a layer that is not opaque cannot use subpixel (LCD) antialiasing:
// it comes out with coloured fringes, plainly visible on the white headline
// over the sky. The words and controls are on such a layer (so the clouds can
// move behind them without repainting them), so every label in it is told to
// use greyscale antialiasing instead.
void UseGreyscaleText(views::View* view) {
  if (auto* label = views::AsViewClass<views::Label>(view)) {
    label->SetSubpixelRenderingEnabled(false);
  }
  for (views::View* child : view->children()) {
    UseGreyscaleText(child);
  }
}

// The column the screens sit in: a fixed width, and as tall as its content is at
// that width. SetPreferredSize cannot say that (it takes a height too), and a
// height of 0 squashed every row of it to nothing.
class OnboardingColumn : public views::View {
  METADATA_HEADER(OnboardingColumn, views::View)

 public:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& /*available*/) const override {
    return gfx::Size(
        kColumnWidth,
        GetLayoutManager()->GetPreferredHeightForWidth(this, kColumnWidth));
  }
};

BEGIN_METADATA(OnboardingColumn)
END_METADATA

// The frosted card the controls sit on: the theme's surface, nearly opaque, a
// hairline of light around the edge and a soft shadow under it, so it floats on
// the sky in light and in dark.
class OnboardingGlass : public views::View {
  METADATA_HEADER(OnboardingGlass, views::View)

 public:
  OnboardingGlass() = default;

  void OnPaintBackground(gfx::Canvas* canvas) override {
    constexpr float kRadius = 28.0f;
    gfx::RectF card(GetLocalBounds());
    card.Inset(static_cast<float>(kCardShadow));
    // Shadow: a few widening, fading rounded rects. Cheap, and this card is on
    // its own layer, so it is painted once per screen, not per cloud frame.
    for (int i = 4; i >= 1; --i) {
      cc::PaintFlags shade;
      shade.setAntiAlias(true);
      shade.setColor(SkColorSetA(SkColorSetRGB(0x0B, 0x2A, 0x5B), 10));
      gfx::RectF r = card;
      r.Inset(gfx::InsetsF::TLBR(-i * 2.0f + 6, -i * 2.0f, -i * 3.0f - 4,
                                 -i * 2.0f));
      canvas->DrawRoundRect(r, kRadius + i * 2.0f, shade);
    }
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    // Opaque. A see-through card without a blur behind it showed the clouds
    // through it as grey blotches drifting under the controls.
    fill.setColor(m3::Role(*this, kColorZephyrusSurfaceContainer));
    canvas->DrawRoundRect(card, kRadius, fill);
    cc::PaintFlags edge;
    edge.setAntiAlias(true);
    edge.setStyle(cc::PaintFlags::kStroke_Style);
    edge.setStrokeWidth(1.0f);
    edge.setColor(SkColorSetA(SK_ColorWHITE, 0x40));
    gfx::RectF inner = card;
    inner.Inset(0.5f);
    canvas->DrawRoundRect(inner, kRadius - 0.5f, edge);
  }
};

BEGIN_METADATA(OnboardingGlass)
END_METADATA

// One option in a row of options: a title, a line under it, and a selected
// state. Used for the mascot's look and for light or dark.
class OnboardingChoice : public views::Button {
  METADATA_HEADER(OnboardingChoice, views::Button)

 public:
  OnboardingChoice(std::u16string title,
                   std::u16string subtitle,
                   PressedCallback callback)
      : views::Button(std::move(callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(14, 16), 4));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStart);
    title_ = AddChildView(std::make_unique<views::Label>(title));
    title_->SetFontList(m3::Font(m3::Type::kTitleMedium, /*emphasized=*/true));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle_ = AddChildView(std::make_unique<views::Label>(subtitle));
    subtitle_->SetFontList(m3::Font(m3::Type::kBodySmall));
    subtitle_->SetMultiLine(true);
    subtitle_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    subtitle_->SetVisible(!subtitle.empty());
    // The labels are for reading; the card is what is pressed.
    title_->SetCanProcessEventsWithinSubtree(false);
    subtitle_->SetCanProcessEventsWithinSubtree(false);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
    GetViewAccessibility().SetRole(ax::mojom::Role::kRadioButton);
    GetViewAccessibility().SetName(title);
    GetViewAccessibility().SetDescription(subtitle);
    GetViewAccessibility().SetCheckedState(ax::mojom::CheckedState::kFalse);
  }

  // A fixed width, so the cards of a row are the same size whatever they say.
  void SetFixedWidth(int width) {
    fixed_width_ = width;
    InvalidateLayout();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override {
    if (fixed_width_ <= 0) {
      return views::Button::CalculatePreferredSize(available);
    }
    return gfx::Size(
        fixed_width_,
        GetLayoutManager()->GetPreferredHeightForWidth(this, fixed_width_));
  }

  void SetSelected(bool selected) {
    selected_ = selected;
    GetViewAccessibility().SetCheckedState(
        selected ? ax::mojom::CheckedState::kTrue
                 : ax::mojom::CheckedState::kFalse);
    ApplyColors();
    SchedulePaint();
  }
  bool selected() const { return selected_; }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    ApplyColors();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const float radius = 16.0f;
    gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(m3::Role(*this, selected_ ? kColorZephyrusSecondaryContainer
                                            : kColorZephyrusSurfaceContainerHigh));
    canvas->DrawRoundRect(bounds, radius, fill);
    if (GetState() == STATE_HOVERED && !selected_) {
      cc::PaintFlags wash;
      wash.setAntiAlias(true);
      wash.setColor(m3::StateLayer(m3::Role(*this, kColorZephyrusOnSurface),
                                   m3::kHover));
      canvas->DrawRoundRect(bounds, radius, wash);
    }
    cc::PaintFlags line;
    line.setAntiAlias(true);
    line.setStyle(cc::PaintFlags::kStroke_Style);
    line.setStrokeWidth(selected_ ? 2.0f : 1.0f);
    line.setColor(m3::Role(*this, selected_ ? kColorZephyrusPrimary
                                            : kColorZephyrusOutlineVariant));
    bounds.Inset(selected_ ? 1.0f : 0.5f);
    canvas->DrawRoundRect(bounds, radius - 1.0f, line);
  }

 private:
  void ApplyColors() {
    if (!GetColorProvider()) {
      return;
    }
    title_->SetEnabledColor(m3::Role(
        *this, selected_ ? kColorZephyrusOnSecondaryContainer
                         : kColorZephyrusOnSurface));
    subtitle_->SetEnabledColor(m3::Role(
        *this, selected_ ? kColorZephyrusOnSecondaryContainer
                         : kColorZephyrusOnSurfaceVariant));
  }

  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> subtitle_ = nullptr;
  bool selected_ = false;
  int fixed_width_ = 0;
};

BEGIN_METADATA(OnboardingChoice)
END_METADATA

namespace {

std::unique_ptr<views::Label> OnboardingText(const std::u16string& text,
                                             m3::Type type,
                                             ui::ColorId color) {
  auto label = std::make_unique<views::Label>(text);
  label->SetFontList(m3::Font(type));
  label->SetMultiLine(true);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(2, 0, 2, 0));
  // The colour is read when the label is in a window (it has no provider yet).
  label->SetEnabledColor(color);
  return label;
}

// A row of equal-width choices. Pressing one selects it, deselects the others
// and reports its index; `selected` is the one showing on entry.
using ChoiceReport = base::RepeatingCallback<void(int)>;

views::View* AddChoiceRow(
    views::View* into,
    const std::vector<std::pair<std::u16string, std::u16string>>& options,
    int selected,
    ChoiceReport report) {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  // Shared by the row's buttons so each press can mark the others. They all
  // live and die together with the row, and the callback runs only from one of
  // them, so every pointer in here is alive whenever it is used.
  auto group = std::make_shared<std::vector<OnboardingChoice*>>();
  for (size_t i = 0; i < options.size(); ++i) {
    auto choice = std::make_unique<OnboardingChoice>(
        options[i].first, options[i].second,
        base::BindRepeating(
            [](std::shared_ptr<std::vector<OnboardingChoice*>> group,
               ChoiceReport report, int index) {
              for (size_t k = 0; k < group->size(); ++k) {
                (*group)[k]->SetSelected(static_cast<int>(k) == index);
              }
              report.Run(index);
            },
            group, report, static_cast<int>(i)));
    choice->SetSelected(static_cast<int>(i) == selected);
    const int gap = 12;
    choice->SetFixedWidth(
        (kContentWidth - gap * (static_cast<int>(options.size()) - 1)) /
        static_cast<int>(options.size()));
    group->push_back(row->AddChildView(std::move(choice)));
  }
  return into->AddChildView(std::move(row));
}

views::MdTextButton* AddOpenButton(
    views::View* into,
    const std::u16string& text,
    ui::ButtonStyle style,
    base::RepeatingCallback<void(views::View*)> open);

// The three looks the settings dialog also offers, in the same order.
constexpr std::array<agent::MascotLook, 3> kLooks = {
    agent::MascotLook::kZep, agent::MascotLook::kLensStatic,
    agent::MascotLook::kLensFluid};

// A tile that says one thing well: a big word and a line under it. Not a
// control; it shares the choice cards' shape so the screens feel of a piece.
std::unique_ptr<views::View> FeatureTile(const std::u16string& title,
                                         const std::u16string& line,
                                         int width) {
  auto tile = std::make_unique<OnboardingChoice>(title, line,
                                                 views::Button::PressedCallback());
  tile->SetFixedWidth(width);
  tile->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  tile->SetEnabled(false);
  tile->GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
  return tile;
}

void BuildWelcomeContent(views::View* into) {
  auto row = std::make_unique<views::View>();
  row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
  const int width = (kContentWidth - 24) / 3;
  row->AddChildView(FeatureTile(u"Private", u"Ads and trackers are blocked "
                                u"while you browse.", width));
  row->AddChildView(FeatureTile(u"Helpful", u"Zep fills in forms and finds the "
                                u"best price for you.", width));
  row->AddChildView(FeatureTile(u"Yours", u"Zep asks before paying and never "
                                u"types a password.", width));
  into->AddChildView(std::move(row));
}

// ---- Workspaces ------------------------------------------------------------

// The Workspaces screen IS the workspace editor (the same one the sidebar's +
// opens as a card): the live preview, name, icon, colour, light or dark. Next
// creates the workspace it describes; "Not now" moves on without one. Returns
// the editor, so Next can find it.
views::View* BuildWorkspacesContent(views::View* into,
                                    BrowserView* browser_view,
                                    base::RepeatingClosure not_now) {
  ZephyrusWorkspaceManager* manager = browser_view->zephyrus_workspace_manager();
  if (manager && !manager->workspaces().empty()) {
    std::u16string names;
    for (const ZephyrusWorkspace& space : manager->workspaces()) {
      names += (names.empty() ? u"" : u", ") + space.name;
    }
    into->AddChildView(OnboardingText(u"You already have: " + names + u".",
                                      m3::Type::kBodyMedium,
                                      kColorZephyrusOnSurfaceVariant));
  }
  std::unique_ptr<views::View> editor_owned =
      zephyrus::CreateWorkspaceEditor(browser_view);
  if (!editor_owned) {
    return nullptr;
  }
  views::View* editor = into->AddChildView(std::move(editor_owned));
  auto row = std::make_unique<views::View>();
  row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  auto later = std::make_unique<views::MdTextButton>(
      base::BindRepeating([](base::RepeatingClosure go) { go.Run(); },
                          std::move(not_now)),
      u"Not now, I'll make one later");
  later->SetStyle(ui::ButtonStyle::kText);
  row->AddChildView(std::move(later));
  into->AddChildView(std::move(row));
  return editor;
}

void BuildLookContent(views::View* into, Profile* profile) {
  PrefService* prefs = profile->GetPrefs();

  into->AddChildView(std::make_unique<m3::SectionLabel>(u"Zep"));
  const agent::MascotLook now = agent::ReadMascotLook(*prefs);
  int look_index = 0;
  for (size_t i = 0; i < kLooks.size(); ++i) {
    if (kLooks[i] == now) {
      look_index = static_cast<int>(i);
    }
  }
  AddChoiceRow(
      into,
      {{u"Pixel Zep", u"The little blue character."},
       {u"Lens", u"A glass lens with two eyes."},
       {u"Lens, flowing", u"The lens with moving colour."}},
      look_index,
      base::BindRepeating(
          [](base::WeakPtr<Profile> profile, int index) {
            if (profile && index >= 0 &&
                index < static_cast<int>(kLooks.size())) {
              // Live: the mascot on this very screen changes as you pick.
              agent::WriteMascotLook(*profile->GetPrefs(), kLooks[index]);
            }
          },
          profile->GetWeakPtr()));

  auto* card = into->AddChildView(
      std::make_unique<m3::Card>(gfx::Insets::VH(4, 0)));
  auto* show = card->AddChildView(std::make_unique<m3::SwitchRow>(
      u"Show Zep", u"Zep stays in the corner while you browse.",
      prefs->GetBoolean(agent::kMascotPref)));
  show->set_on_change(base::BindRepeating(
      [](base::WeakPtr<Profile> profile, m3::SwitchRow* row) {
        if (profile) {
          profile->GetPrefs()->SetBoolean(agent::kMascotPref, row->is_on());
        }
      },
      profile->GetWeakPtr(), base::Unretained(show)));

  into->AddChildView(std::make_unique<m3::SectionLabel>(u"Colours"));
  ThemeService* theme = ThemeServiceFactory::GetForProfile(profile);
  using Scheme = ThemeService::BrowserColorScheme;
  constexpr std::array<Scheme, 3> kSchemes = {Scheme::kSystem, Scheme::kLight,
                                               Scheme::kDark};
  int scheme_index = 0;
  for (size_t i = 0; i < kSchemes.size(); ++i) {
    if (theme->GetBrowserColorScheme() == kSchemes[i]) {
      scheme_index = static_cast<int>(i);
    }
  }
  AddChoiceRow(
      into,
      {{u"Match my computer", u"Light or dark, as your system is."},
       {u"Light", u"Always light."},
       {u"Dark", u"Always dark."}},
      scheme_index,
      base::BindRepeating(
          [](base::WeakPtr<Profile> profile, int index) {
            if (!profile) {
              return;
            }
            constexpr std::array<Scheme, 3> kSchemes = {
                Scheme::kSystem, Scheme::kLight, Scheme::kDark};
            if (index >= 0 && index < static_cast<int>(kSchemes.size())) {
              ThemeServiceFactory::GetForProfile(profile.get())
                  ->SetBrowserColorScheme(kSchemes[index]);
            }
          },
          profile->GetWeakPtr()));
}

// What the agent screen says about how Zep reaches its model, from what is
// really configured.
std::u16string AgentStatusText(const PrefService& prefs) {
  const bool bundled = agent::HasBundledKey(agent::kBundledDefaultKind);
  const bool own = agent::HasSavedKey(prefs, "anthropic") ||
                   agent::HasSavedKey(prefs, "openai") ||
                   agent::HasSavedKey(prefs, "gemini");
  switch (agent::AgentAccessFor(
      bundled, prefs.GetBoolean(agent::kUseProvidedKeysPref), own)) {
    case agent::AgentAccess::kBuiltIn:
      return u"Ready: Zep is using this build's built-in access.";
    case agent::AgentAccess::kOwnKey:
      return u"Ready: Zep is using your own key.";
    case agent::AgentAccess::kNoKeyYet:
      return u"No key yet. Zep can browse with you, but it cannot do tasks "
             u"until you add one.";
  }
  return std::u16string();
}

// Keeps a status line true while a dialog opened from this screen changes what
// it says. Stops by itself when the line is gone (the screen moved on).
void KeepStatusFresh(std::shared_ptr<views::ViewTracker> line,
                     base::WeakPtr<Profile> profile) {
  auto* label = static_cast<views::Label*>(line->view());
  if (!label || !profile) {
    return;
  }
  const std::u16string now = AgentStatusText(*profile->GetPrefs());
  if (label->GetText() != now) {
    label->SetText(now);
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&KeepStatusFresh, std::move(line), std::move(profile)),
      base::Seconds(1));
}

// A button that opens one of the existing dialogs, anchored to itself. The
// dialogs own everything about keys and voice; this screen never sees a secret.
views::MdTextButton* AddOpenButton(views::View* into,
                                   const std::u16string& text,
                                   ui::ButtonStyle style,
                                   base::RepeatingCallback<void(views::View*)> open) {
  auto button = std::make_unique<views::MdTextButton>(
      views::Button::PressedCallback(), text);
  button->SetStyle(style);
  views::MdTextButton* raw = button.get();
  button->SetCallback(base::BindRepeating(
      [](base::RepeatingCallback<void(views::View*)> open, views::View* anchor) {
        open.Run(anchor);
      },
      std::move(open), base::Unretained(raw)));
  // In a row of its own so it keeps its natural width instead of stretching
  // across the column like the cards do.
  auto row = std::make_unique<views::View>();
  row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  row->AddChildView(std::move(button));
  into->AddChildView(std::move(row));
  return raw;
}

void BuildAgentContent(views::View* into, BrowserView* browser_view) {
  Profile* profile = browser_view->browser()->profile();
  PrefService* prefs = profile->GetPrefs();
  const bool bundled = agent::HasBundledKey(agent::kBundledDefaultKind);

  into->AddChildView(std::make_unique<m3::SectionLabel>(u"Zep's brain"));
  if (bundled) {
    AddChoiceRow(
        into,
        {{u"Built-in access", u"Ready to use. Nothing to set up."},
         {u"My own key", u"Use your own key from Anthropic, OpenAI or Gemini."}},
        prefs->GetBoolean(agent::kUseProvidedKeysPref) ? 0 : 1,
        base::BindRepeating(
            [](base::WeakPtr<Profile> profile, int index) {
              if (profile) {
                profile->GetPrefs()->SetBoolean(agent::kUseProvidedKeysPref,
                                                index == 0);
              }
            },
            profile->GetWeakPtr()));
  }
  auto status = std::make_unique<views::Label>(AgentStatusText(*prefs));
  status->SetFontList(m3::Font(m3::Type::kBodyMedium));
  status->SetMultiLine(true);
  status->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status->SetEnabledColor(kColorZephyrusOnSurfaceVariant);
  views::Label* status_raw = into->AddChildView(std::move(status));

  AddOpenButton(
      into, bundled ? u"Add or change my key..." : u"Add my key...",
      bundled ? ui::ButtonStyle::kText : ui::ButtonStyle::kProminent,
      base::BindRepeating(
          [](BrowserView* bv, views::View* anchor) {
            zephyrus::ShowAgentSettings(bv, anchor);
          },
          base::Unretained(browser_view)));
  into->AddChildView(OnboardingText(
      u"Zep stops a task once it has spent $1, and asks before it pays for "
      u"anything. You can change the limit in Settings.",
      m3::Type::kBodySmall, kColorZephyrusOnSurfaceVariant));

  into->AddChildView(
      std::make_unique<m3::SectionLabel>(u"Hey Zep (optional)"));
  into->AddChildView(OnboardingText(
      u"Say \"Hey Zep\" and then what you want done. Zep learns your voice "
      u"from three recordings. They stay on this computer.",
      m3::Type::kBodyMedium, kColorZephyrusOnSurface));
  AddOpenButton(
      into, u"Set up my voice...", ui::ButtonStyle::kDefault,
      base::BindRepeating(
          [](BrowserView* bv, views::View* anchor) {
            zephyrus::ShowVoiceSetup(bv, anchor);
          },
          base::Unretained(browser_view)));

  // The status follows the dialogs: adding a key there updates the line here.
  KeepStatusFresh(std::make_shared<views::ViewTracker>(status_raw),
                  profile->GetWeakPtr());
}

// ---- Privacy and your things ------------------------------------------------

constexpr char kAdblockPref[] = "zephyrus.adblock.enabled";
constexpr char kPopupPref[] = "zephyrus.adblock.aggressive_popup";

std::u16string SearchLine(Profile* profile) {
  TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(profile);
  const TemplateURL* engine =
      service ? service->GetDefaultSearchProvider() : nullptr;
  return engine ? u"You search with " + engine->short_name() + u"."
                : u"No search engine is set.";
}

// One row of a card: what it is and how it stands on the left, the button that
// changes it on the right. Returns the status line, which the button's result
// rewrites.
views::Label* AddActionRow(views::View* card,
                           const std::u16string& title,
                           const std::u16string& status,
                           const std::u16string& action,
                           base::RepeatingCallback<void(views::View*)> act) {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(12, 16), 12));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto words = std::make_unique<views::View>();
  words->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  auto heading = std::make_unique<views::Label>(title);
  heading->SetFontList(m3::Font(m3::Type::kBodyLarge));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  heading->SetEnabledColor(kColorZephyrusOnSurface);
  words->AddChildView(std::move(heading));
  auto line = std::make_unique<views::Label>(status);
  line->SetFontList(m3::Font(m3::Type::kBodyMedium));
  line->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  line->SetMultiLine(true);
  line->SetMaximumWidth(kContentWidth - 260);
  line->SetEnabledColor(kColorZephyrusOnSurfaceVariant);
  views::Label* line_raw = words->AddChildView(std::move(line));
  views::View* words_raw = row->AddChildView(std::move(words));
  layout->SetFlexForView(words_raw, 1);
  auto button = std::make_unique<views::MdTextButton>(
      views::Button::PressedCallback(), action);
  button->SetStyle(ui::ButtonStyle::kTonal);
  views::MdTextButton* raw = button.get();
  button->SetCallback(base::BindRepeating(
      [](base::RepeatingCallback<void(views::View*)> act, views::View* self) {
        act.Run(self);
      },
      std::move(act), base::Unretained(raw)));
  row->AddChildView(std::move(button));
  card->AddChildView(std::move(row));
  return line_raw;
}

// Adds what was read under `parent`, in order.
void AddImported(bookmarks::BookmarkModel* model,
                 const bookmarks::BookmarkNode* parent,
                 const agent::ImportedBookmark& item) {
  const size_t at = parent->children().size();
  if (item.is_folder) {
    const bookmarks::BookmarkNode* folder =
        model->AddFolder(parent, at, item.title);
    for (const agent::ImportedBookmark& child : item.children) {
      AddImported(model, folder, child);
    }
    return;
  }
  model->AddURL(parent, at, item.title, item.url);
}

// Runs on the UI thread once Chrome's file has been read.
void OnChromeBookmarksRead(base::WeakPtr<Profile> profile,
                           std::shared_ptr<views::ViewTracker> status,
                           std::shared_ptr<views::ViewTracker> button,
                           std::optional<agent::ChromeBookmarks> read) {
  auto* line = static_cast<views::Label*>(status->view());
  auto say = [&](const std::u16string& text) {
    if (line) {
      line->SetText(text);
    }
  };
  if (!profile) {
    return;
  }
  if (!read) {
    say(u"Chrome's bookmarks were not found on this computer.");
    if (button->view()) {
      button->view()->SetEnabled(true);
    }
    return;
  }
  if (read->urls == 0) {
    say(u"Chrome has no bookmarks to bring over.");
    return;
  }
  bookmarks::BookmarkModel* model =
      BookmarkModelFactory::GetForBrowserContext(profile.get());
  if (!model || !model->loaded()) {
    say(u"Bookmarks are still loading. Try again in a moment.");
    if (button->view()) {
      button->view()->SetEnabled(true);
    }
    return;
  }
  // Into one folder of their own on the bookmarks bar, so nothing of the
  // person's existing bookmarks is touched or mixed in.
  const bookmarks::BookmarkNode* bar = model->bookmark_bar_node();
  const bookmarks::BookmarkNode* from_chrome =
      model->AddFolder(bar, bar->children().size(), u"From Chrome");
  for (const agent::ImportedBookmark& root : read->roots) {
    AddImported(model, from_chrome, root);
  }
  say(u"Brought over " +
      (read->urls == 1 ? std::u16string(u"1 bookmark")
                       : base::NumberToString16(read->urls) + u" bookmarks") +
      u" into the \"From Chrome\" folder on your bookmarks bar.");
}

std::u16string DefaultBrowserLine(shell_integration::DefaultWebClientState state) {
  switch (state) {
    case shell_integration::IS_DEFAULT:
      return u"Zephyrus is your default browser.";
    case shell_integration::NOT_DEFAULT:
    case shell_integration::OTHER_MODE_IS_DEFAULT:
      return u"Zephyrus is not your default browser yet.";
    default:
      return u"Windows did not say which browser is the default.";
  }
}

void BuildPrivacyContent(views::View* into, BrowserView* browser_view) {
  Profile* profile = browser_view->browser()->profile();
  PrefService* prefs = profile->GetPrefs();

  // -- Blocking: the two switches the adblock service watches live.
  into->AddChildView(std::make_unique<m3::SectionLabel>(u"Blocking"));
  auto* card = into->AddChildView(
      std::make_unique<m3::Card>(gfx::Insets::VH(4, 0)));
  for (const auto& [pref, title, line] :
       {std::tuple<const char*, const char16_t*, const char16_t*>{
            kAdblockPref, u"Block ads and trackers",
            u"Pages load faster and fewer companies follow you around."},
        std::tuple<const char*, const char16_t*, const char16_t*>{
            kPopupPref, u"Stop pop-ups that open by themselves",
            u"Windows a page opens without you asking."}}) {
    auto* row = card->AddChildView(std::make_unique<m3::SwitchRow>(
        title, line, prefs->GetBoolean(pref)));
    row->set_on_change(base::BindRepeating(
        [](base::WeakPtr<Profile> profile, const char* pref,
           m3::SwitchRow* row) {
          if (profile) {
            profile->GetPrefs()->SetBoolean(pref, row->is_on());
          }
        },
        profile->GetWeakPtr(), pref, base::Unretained(row)));
  }

  // -- Search, bookmarks, default browser: one card of action rows.
  into->AddChildView(std::make_unique<m3::SectionLabel>(u"Search and your things"));
  auto* actions = into->AddChildView(
      std::make_unique<m3::Card>(gfx::Insets::VH(4, 0)));

  // The same picker the address bar opens.
  auto search_line = std::make_shared<views::ViewTracker>();
  search_line->SetView(AddActionRow(
      actions, u"Search engine", SearchLine(profile), u"Change",
      base::BindRepeating(
          [](base::WeakPtr<Profile> profile,
             std::shared_ptr<views::ViewTracker> line, views::View* anchor) {
            if (!profile) {
              return;
            }
            ZephyrusSearchEnginePicker::Show(
                profile.get(), anchor,
                base::BindOnce(
                    [](base::WeakPtr<Profile> profile,
                       std::shared_ptr<views::ViewTracker> line) {
                      auto* label = static_cast<views::Label*>(line->view());
                      if (profile && label) {
                        label->SetText(SearchLine(profile.get()));
                      }
                    },
                    profile, line));
          },
          profile->GetWeakPtr(), search_line)));

  // Bookmarks from Chrome: read off the UI thread, added in one folder.
  auto import_line = std::make_shared<views::ViewTracker>();
  import_line->SetView(AddActionRow(
      actions, u"Bookmarks from Chrome",
      u"Brought into a folder of their own.", u"Bring them",
      base::BindRepeating(
          [](base::WeakPtr<Profile> profile,
             std::shared_ptr<views::ViewTracker> line, views::View* button) {
            // Once: a second press would make a second copy.
            button->SetEnabled(false);
            if (auto* label = static_cast<views::Label*>(line->view())) {
              label->SetText(u"Reading Chrome's bookmarks...");
            }
            base::ThreadPool::PostTaskAndReplyWithResult(
                FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
                base::BindOnce(&agent::ReadChromeBookmarks,
                               agent::ChromeBookmarksFilePath()),
                base::BindOnce(&OnChromeBookmarksRead, profile, line,
                               std::make_shared<views::ViewTracker>(button)));
          },
          profile->GetWeakPtr(), import_line)));

  // Default browser: Windows decides, in its own Settings; this asks it to.
  auto default_line = std::make_shared<views::ViewTracker>();
  default_line->SetView(AddActionRow(
      actions, u"Default browser", u"Checking...", u"Make default",
      base::BindRepeating(
          [](std::shared_ptr<views::ViewTracker> line, views::View* anchor) {
            base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
                ->StartSetAsDefault(base::BindOnce(
                    [](std::shared_ptr<views::ViewTracker> line,
                       shell_integration::DefaultWebClientState state) {
                      if (auto* label =
                              static_cast<views::Label*>(line->view())) {
                        label->SetText(DefaultBrowserLine(state));
                      }
                    },
                    line));
          },
          default_line)));
  base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
      ->StartCheckIsDefault(base::BindOnce(
          [](std::shared_ptr<views::ViewTracker> line,
             shell_integration::DefaultWebClientState state) {
            if (auto* label = static_cast<views::Label*>(line->view())) {
              label->SetText(DefaultBrowserLine(state));
            }
          },
          default_line));
}

}  // namespace

// One small dot per screen: the current one filled, the others an outline.
class OnboardingDots : public views::View {
  METADATA_HEADER(OnboardingDots, views::View)

 public:
  OnboardingDots() = default;

  void SetPosition(int current, int count) {
    current_ = current;
    count_ = count;
    InvalidateLayout();
    SchedulePaint();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& /*available*/) const override {
    return gfx::Size(count_ * kPitch, kDot + 4);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // White on the sky, whatever the theme: the dots sit above the card.
    const SkColor on = SK_ColorWHITE;
    const SkColor off = SkColorSetA(SK_ColorWHITE, 0x70);
    for (int i = 0; i < count_; ++i) {
      const float cx = static_cast<float>(i * kPitch + kPitch / 2);
      const float cy = static_cast<float>(height()) / 2;
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(i == current_ ? on : off);
      // The current screen is a longer pill, so the position reads without
      // relying on colour alone.
      const float half = i == current_ ? kDot * 1.5f : kDot / 2.0f;
      canvas->DrawRoundRect(
          gfx::RectF(cx - half, cy - kDot / 2.0f, half * 2, kDot),
          kDot / 2.0f, flags);
    }
  }

 private:
  static constexpr int kDot = 8;
  static constexpr int kPitch = 22;
  int current_ = 0;
  int count_ = agent::kOnboardingStepCount;
};

BEGIN_METADATA(OnboardingDots)
END_METADATA

// Zep, flying a slow loop through the onboarding's sky: drawn by the sky itself
// between its cloud layers, so the near clouds pass in front of it and the far
// ones behind. It is whichever Zep the person has chosen (pixel or lens, and the
// lens's fill), changes the moment they pick another on the "Make it yours"
// screen, and is not there at all when "Show Zep" is off.
class OnboardingFlyer {
 public:
  OnboardingFlyer() { rig_.SetMood(agent::MascotMood::kWaving); }
  OnboardingFlyer(const OnboardingFlyer&) = delete;
  OnboardingFlyer& operator=(const OnboardingFlyer&) = delete;

  void SetMood(agent::MascotMood mood) {
    if (mood == mood_) {
      return;
    }
    mood_ = mood;
    in_mood_ = 0;
    rig_.SetMood(mood);
  }

  // The loop Zep flies, as fractions of the window (see FlightPathAt).
  void SetOrbit(double cx, double cy, double rx, double ry) {
    cx_ = cx;
    cy_ = cy;
    rx_ = rx;
    ry_ = ry;
  }

  void Advance(double seconds, agent::MascotLook look, bool show) {
    seconds = std::clamp(seconds, 0.0, 0.1);
    t_ += seconds;
    in_mood_ += seconds;
    show_ = show;
    if (look != look_ || (look != agent::MascotLook::kZep && !lens_)) {
      look_ = look;
      if (look == agent::MascotLook::kZep) {
        lens_.reset();
        rng_.reset();
      } else {
        if (!lens_) {
          rng_ = std::make_unique<agent::SystemLensRandom>();
          lens_ = std::make_unique<agent::LensModel>(rng_.get());
        }
        lens_->SetFill(look == agent::MascotLook::kLensFluid
                           ? agent::LensFill::kFluid
                           : agent::LensFill::kStatic);
      }
    }
    point_ = agent::FlightPathAt(t_, cx_, cy_, rx_, ry_);
    rig_.SetFacingLeft(point_.vx < 0);
    rig_.SetSway(static_cast<float>(std::clamp(-point_.vx * 40.0, -1.0, 1.0)));
    rig_.Update(base::Seconds(seconds));
    if (lens_) {
      lens_->SetState(agent::LensStateForMood(mood_, in_mood_));
      lens_->SetManualSlant(std::clamp(point_.vx * 25.0, -0.7, 0.7));
      lens_->Update(seconds);
      frame_ = lens_->ComputeFrame();
    }
  }

  // In physical pixels: the sky has already undone the device scale.
  void Paint(gfx::Canvas* canvas, const gfx::Size& dip, float dsf) {
    if (!show_) {
      return;
    }
    const float cx = static_cast<float>(point_.x * dip.width()) * dsf;
    const float cy = static_cast<float>(point_.y * dip.height()) * dsf;
    if (lens_) {
      const int h = std::max(8, static_cast<int>(std::lround(kLensSide * dsf)));
      const int w = std::max(8, h * 311 / 316);
      const SkBitmap bitmap = agent::RenderLensToBitmap(frame_, w, h);
      canvas->DrawImageInt(gfx::ImageSkia::CreateFrom1xBitmap(bitmap),
                           static_cast<int>(std::lround(cx - w / 2.0f)),
                           static_cast<int>(std::lround(cy - h * 0.55f)));
      return;
    }
    // The pixel Zep, drawn the way the corner mascot draws it: whole device
    // pixels, so its blocks never blur. Its art is 160 wide with the feet at
    // (80, 141); the middle of the body is about 70 up from the feet.
    const float scale = kZepScale * dsf;
    const float ox = std::round(cx - 80.0f * scale);
    const float oy = std::round(cy - 70.0f * scale);
    for (const zephyrus_setup::Rect& r : rig_.Draw()) {
      const int left = static_cast<int>(std::lround(ox + r.x * scale));
      const int top = static_cast<int>(std::lround(oy + r.y * scale));
      const int right = static_cast<int>(std::lround(ox + (r.x + r.w) * scale));
      const int bottom = static_cast<int>(std::lround(oy + (r.y + r.h) * scale));
      if (right <= left || bottom <= top) {
        continue;
      }
      canvas->FillRect(gfx::Rect(left, top, right - left, bottom - top),
                       SkColorSetRGB((r.color >> 16) & 0xFF,
                                     (r.color >> 8) & 0xFF, r.color & 0xFF));
    }
  }

 private:
  static constexpr float kZepScale = 0.62f;
  static constexpr float kLensSide = 150.0f;

  double t_ = 0;
  double in_mood_ = 0;
  double cx_ = 0.5, cy_ = 0.48, rx_ = 0.40, ry_ = 0.36;
  bool show_ = true;
  agent::FlightPoint point_;
  agent::MascotMood mood_ = agent::MascotMood::kWaving;
  agent::MascotRig rig_;
  agent::MascotLook look_ = agent::MascotLook::kZep;
  std::unique_ptr<agent::SystemLensRandom> rng_;
  std::unique_ptr<agent::LensModel> lens_;
  agent::LensFrame frame_;
};

namespace {

// The sky for now: the person's local time, or the dev switch.
agent::SkyTime SkyForNow() {
  base::Time::Exploded now;
  base::Time::Now().LocalExplode(&now);
  agent::SkyTime sky = agent::SkyTimeForClock(now.hour, now.minute);
  const base::CommandLine& line = *base::CommandLine::ForCurrentProcess();
  if (zephyrus::DevSwitchesEnabled() &&
      line.HasSwitch(agent::kOnboardingSkySwitch)) {
    sky = agent::SkyTimeFromName(
        line.GetSwitchValueASCII(agent::kOnboardingSkySwitch), sky);
  }
  return sky;
}

// How Zep feels on each screen.
agent::MascotMood MoodFor(agent::OnboardingStep step) {
  switch (step) {
    case agent::OnboardingStep::kWelcome:
      return agent::MascotMood::kWaving;
    case agent::OnboardingStep::kLook:
      return agent::MascotMood::kHappy;
    case agent::OnboardingStep::kWorkspaces:
      return agent::MascotMood::kThinking;
    case agent::OnboardingStep::kAgentAndVoice:
      return agent::MascotMood::kListening;
    case agent::OnboardingStep::kPrivacyAndImport:
      return agent::MascotMood::kHappy;
  }
  return agent::MascotMood::kIdle;
}

}  // namespace

ZephyrusOnboarding::ZephyrusOnboarding(BrowserView* browser_view)
    : browser_view_(browser_view),
      sky_(std::make_unique<OnboardingSky>(SkyForNow())),
      flyer_(std::make_unique<OnboardingFlyer>()) {
  SetProperty(views::kViewIgnoredByLayoutKey, true);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(true);
  SetVisible(false);
  GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
  GetViewAccessibility().SetName(u"Welcome to Zephyrus");
  BuildChrome();
}

ZephyrusOnboarding::~ZephyrusOnboarding() = default;

void ZephyrusOnboarding::BuildChrome() {
  auto* outer = SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical));
  outer->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);
  outer->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  auto column = std::make_unique<OnboardingColumn>();
  auto* layout = column->SetLayoutManager(
      std::make_unique<views::BoxLayout>(views::BoxLayout::Orientation::kVertical,
                                         gfx::Insets(), 14));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  // Its own layer: the clouds repaint the window 30 times a second, and the
  // words and controls should not be repainted with them. It is also what the
  // step-to-step fade moves.
  column->SetPaintToLayer();
  column->layer()->SetFillsBoundsOpaquely(false);

  dots_ = column->AddChildView(std::make_unique<OnboardingDots>());
  dots_->SetProperty(views::kCrossAxisAlignmentKey, views::LayoutAlignment::kStart);

  // The headline: big, white, on the sky. A soft shadow keeps it readable over
  // the pale clouds as they pass behind it.
  const gfx::ShadowValues lift = {
      gfx::ShadowValue(gfx::Vector2d(0, 2), 14, SkColorSetA(SkColorSetRGB(0x06, 0x1E, 0x4A), 0x80)),
      gfx::ShadowValue(gfx::Vector2d(0, 0), 4, SkColorSetA(SkColorSetRGB(0x06, 0x1E, 0x4A), 0x50))};
  title_ = column->AddChildView(std::make_unique<views::Label>());
  title_->SetFontList(m3::Font(m3::Type::kDisplaySmall, /*emphasized=*/true));
  title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_->SetMultiLine(true);
  title_->SetShadows(lift);
  title_->SetAutoColorReadabilityEnabled(false);

  body_ = column->AddChildView(std::make_unique<views::Label>());
  body_->SetFontList(m3::Font(m3::Type::kTitleMedium));
  body_->SetMultiLine(true);
  body_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  body_->SetShadows(lift);
  body_->SetAutoColorReadabilityEnabled(false);
  body_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 10, 0));

  // The glass card: each screen's controls, then the footer.
  auto glass = std::make_unique<OnboardingGlass>();
  glass->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(kCardPadding + kCardShadow), 14))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  // Where each screen puts its controls, in a scroller that only scrolls when
  // the window is too short for them (see FitContent).
  auto scroll = std::make_unique<views::ScrollView>(
      views::ScrollView::ScrollWithLayers::kEnabled);
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetDrawOverflowIndicator(false);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  auto content = std::make_unique<views::View>();
  content->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 12));
  step_content_ = scroll->SetContents(std::move(content));
  content_scroll_ = glass->AddChildView(std::move(scroll));

  auto footer = std::make_unique<views::View>();
  auto* row = footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::TLBR(16, 0, 0, 0),
      12));
  row->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  skip_ = footer->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&ZephyrusOnboarding::OnSkip, base::Unretained(this)),
      u"Skip"));
  skip_->SetStyle(ui::ButtonStyle::kText);
  auto* spacer = footer->AddChildView(std::make_unique<views::View>());
  row->SetFlexForView(spacer, 1);
  back_ = footer->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&ZephyrusOnboarding::OnBack, base::Unretained(this)),
      u"Back"));
  back_->SetStyle(ui::ButtonStyle::kText);
  next_ = footer->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&ZephyrusOnboarding::OnNext, base::Unretained(this)),
      u"Next"));
  next_->SetStyle(ui::ButtonStyle::kProminent);
  glass->AddChildView(std::move(footer));
  column->AddChildView(std::move(glass));

  column_ = AddChildView(std::move(column));
}

void ZephyrusOnboarding::ShowIfDue() {
  if (!browser_view_ || !browser_view_->browser()) {
    return;
  }
  const bool forced =
      zephyrus::DevSwitchesEnabled() &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          agent::kOnboardingForceSwitch);
  PrefService* prefs = browser_view_->browser()->profile()->GetPrefs();
  if (agent::ShouldShowOnboarding(*prefs, forced, agent::kOnboardingEnabled)) {
    Start();
  }
}

void ZephyrusOnboarding::Start() {
  flow_ = agent::OnboardingFlow();
  SetVisible(true);
  if (auto* mascot = browser_view_ ? browser_view_->zephyrus_agent_mascot()
                                   : nullptr) {
    mascot->SetSuppressed(true);
  }
  // The clouds move only for people who have not asked for less motion, and
  // only while this is up.
  if (gfx::Animation::ShouldRenderRichAnimation()) {
    sky_last_ = base::TimeTicks::Now();
    sky_timer_.Start(FROM_HERE, base::Milliseconds(33), this,
                     &ZephyrusOnboarding::OnSkyTick);
  }
  ShowStep();
  if (parent()) {
    FitToParent();
  }
  next_->RequestFocus();
  // One step at once, so Zep is in place even with animations turned off.
  sky_last_ = base::TimeTicks::Now();
  OnSkyTick();
}

void ZephyrusOnboarding::FitToParent() {
  if (!parent()) {
    return;
  }
  SetBoundsRect(parent()->GetLocalBounds());
  FitContent();
}

void ZephyrusOnboarding::FitContent() {
  if (!content_scroll_ || height() <= 0) {
    return;
  }
  // A short window gets a smaller headline before anything has to scroll.
  const bool compact = height() < 820;
  const bool welcome = flow_.step() == agent::OnboardingStep::kWelcome;
  title_->SetFontList(m3::Font(
      compact ? m3::Type::kHeadlineMedium
              : (welcome ? m3::Type::kDisplayMedium : m3::Type::kDisplaySmall),
      /*emphasized=*/true));
  body_->SetFontList(m3::Font(compact ? m3::Type::kBodyLarge
                                      : m3::Type::kTitleMedium));
  // What the column needs besides the controls: the dots, the headline, the
  // line under it, the card's padding and its footer, and some sky above and
  // below. The controls get the rest, and scroll if they need more.
  const int words = dots_->GetPreferredSize().height() +
                    title_->GetHeightForWidth(kColumnWidth) +
                    body_->GetHeightForWidth(kColumnWidth) + 3 * 14 + 10;
  const int card_chrome = 2 * (kCardPadding + kCardShadow) + 14 + 56;
  const int sky_margin = compact ? 24 : 64;
  const int room = height() - words - card_chrome - sky_margin;
  content_scroll_->ClipHeightTo(0, std::max(140, room));
  InvalidateLayout();
}

void ZephyrusOnboarding::ShowStep() {
  const StepText& text = kSteps[static_cast<size_t>(flow_.index())];
  title_->SetText(text.title);
  body_->SetText(text.body);
  body_->SetMaximumWidth(kColumnWidth);
  title_->SetMaximumWidth(kColumnWidth);
  next_->SetText(flow_.step() == agent::OnboardingStep::kWorkspaces
                     ? u"Create and continue"
                     : text.next);
  // Each screen builds its own controls; the last screen's are dropped first.
  workspace_editor_ = nullptr;
  step_content_->RemoveAllChildViews();
  Profile* profile =
      browser_view_ && browser_view_->browser()
          ? browser_view_->browser()->profile()
          : nullptr;
  switch (flow_.step()) {
    case agent::OnboardingStep::kWelcome:
      BuildWelcomeContent(step_content_);
      break;
    case agent::OnboardingStep::kLook:
      if (profile) {
        BuildLookContent(step_content_, profile);
      }
      break;
    case agent::OnboardingStep::kWorkspaces:
      if (browser_view_) {
        workspace_editor_ = BuildWorkspacesContent(
            step_content_, browser_view_,
            base::BindRepeating(&ZephyrusOnboarding::ContinueWithoutWorkspace,
                                weak_factory_.GetWeakPtr()));
      }
      break;
    case agent::OnboardingStep::kPrivacyAndImport:
      if (browser_view_ && profile) {
        BuildPrivacyContent(step_content_, browser_view_);
      }
      break;
    case agent::OnboardingStep::kAgentAndVoice:
      if (browser_view_ && profile) {
        BuildAgentContent(step_content_, browser_view_);
      }
      break;
    default:
      break;
  }
  // Zep reacts to each screen as it flies.
  flyer_->SetMood(MoodFor(flow_.step()));
  back_->SetVisible(flow_.CanGoBack());
  dots_->SetPosition(flow_.index(), flow_.count());
  GetViewAccessibility().SetName(text.title);
  UseGreyscaleText(column_);
  content_scroll_->ScrollToOffset(gfx::PointF());
  FitContent();
  InvalidateLayout();
  AnimateIn();
}

void ZephyrusOnboarding::OnNext() {
  if (flow_.step() == agent::OnboardingStep::kWorkspaces && workspace_editor_) {
    zephyrus::CommitWorkspaceEditor(workspace_editor_);
  }
  flow_.Next();
  if (flow_.finished()) {
    Finish();
    return;
  }
  ShowStep();
}

void ZephyrusOnboarding::OnBack() {
  if (flow_.Back()) {
    ShowStep();
  }
}

void ZephyrusOnboarding::OnSkip() {
  flow_.Skip();
  Finish();
}

void ZephyrusOnboarding::ContinueWithoutWorkspace() {
  // Posted: the press came from a button on the screen this replaces.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<ZephyrusOnboarding> self) {
                       if (self && self->flow_.Next()) {
                         self->ShowStep();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}

void ZephyrusOnboarding::RebuildStepSoon() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusOnboarding::ShowStep,
                                weak_factory_.GetWeakPtr()));
}

void ZephyrusOnboarding::OnSkyTick() {
  const base::TimeTicks now = base::TimeTicks::Now();
  const double seconds = (now - sky_last_).InSecondsF();
  sky_last_ = now;
  // The sky leans away from the pointer, a little, for depth.
  if (display::Screen* screen = display::Screen::Get(); screen && width() > 0) {
    gfx::Point cursor = screen->GetCursorScreenPoint();
    views::View::ConvertPointFromScreen(this, &cursor);
    sky_->SetPointer((cursor.x() - width() / 2.0) / (width() / 2.0),
                     (cursor.y() - height() / 2.0) / (height() / 2.0));
  }
  sky_->Advance(seconds);
  // Zep loops round the column (headline and card), just outside it, so it is
  // never hidden behind the card. On a window too small for that it keeps to
  // the window's edge.
  if (column_ && width() > 0 && height() > 0) {
    const gfx::Rect c = column_->bounds();
    constexpr double kClear = 90.0;  // about half of Zep, and some air
    const int margin = width() - c.right();
    if (margin >= 2 * kClear) {
      // Room beside the card: a tall loop in the right-hand margin, so Zep is
      // always in full view and never crosses the controls.
      flyer_->SetOrbit((c.right() + margin / 2.0) / width(), 0.5,
                       std::max(0.01, (margin / 2.0 - kClear * 0.8) / width()),
                       0.36);
    } else {
      flyer_->SetOrbit(
          c.CenterPoint().x() / static_cast<double>(width()),
          c.CenterPoint().y() / static_cast<double>(height()),
          std::clamp((c.width() / 2.0 + kClear) / width(), 0.2, 0.46),
          std::clamp((c.height() / 2.0 + kClear) / height(), 0.2, 0.44));
    }
  }
  // Zep is the look the person has chosen, and only if they want Zep at all.
  if (browser_view_ && browser_view_->browser()) {
    const PrefService& prefs = *browser_view_->browser()->profile()->GetPrefs();
    flyer_->Advance(seconds, agent::ReadMascotLook(prefs),
                    prefs.GetBoolean(agent::kMascotPref));
  }
  SchedulePaint();
}

void ZephyrusOnboarding::AnimateIn() {
  if (!column_ || !column_->layer() ||
      !gfx::Animation::ShouldRenderRichAnimation()) {
    return;
  }
  // Each screen arrives: a short rise and fade, eased out.
  ui::Layer* layer = column_->layer();
  layer->GetAnimator()->StopAnimating();
  gfx::Transform below;
  below.Translate(0, 18);
  layer->SetOpacity(0.0f);
  layer->SetTransform(below);
  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTransitionDuration(base::Milliseconds(380));
  settings.SetTweenType(gfx::Tween::FAST_OUT_SLOW_IN_3);
  layer->SetOpacity(1.0f);
  layer->SetTransform(gfx::Transform());
}

void ZephyrusOnboarding::Finish() {
  sky_timer_.Stop();
  SetVisible(false);
  if (auto* mascot = browser_view_ ? browser_view_->zephyrus_agent_mascot()
                                   : nullptr) {
    mascot->SetSuppressed(false);
  }
  if (browser_view_ && browser_view_->browser()) {
    agent::MarkOnboardingDone(
        *browser_view_->browser()->profile()->GetPrefs());
  }
}

void ZephyrusOnboarding::OnThemeChanged() {
  views::View::OnThemeChanged();
  // Read here, not in the constructor: the colour provider is not there yet.
  // The headline is on the sky, so it is white in light and in dark.
  title_->SetEnabledColor(SK_ColorWHITE);
  body_->SetEnabledColor(SkColorSetA(SK_ColorWHITE, 0xEB));
}

void ZephyrusOnboarding::OnPaintBackground(gfx::Canvas* canvas) {
  // Opaque: this is the whole window until the person is done.
  const gfx::Size dip = size();
  OnboardingFlyer* flyer = flyer_.get();
  // Zep flies between the middle clouds and the near ones.
  sky_->Paint(canvas, dip, [canvas, dip, flyer](int layer, float dsf) {
    if (layer == 1) {
      flyer->Paint(canvas, dip, dsf);
    }
  });

  // The headline is white and the clouds are white. A soft, deep-blue haze
  // sits behind the words (an ellipse fading to nothing), so a cloud drifting
  // behind the title dims instead of swallowing it.
  if (column_ && title_ && body_ && column_->GetVisible()) {
    gfx::Rect words = title_->bounds();
    words.Union(body_->bounds());
    words = column_->ConvertRectToParent(words);
    words.Outset(gfx::Outsets::VH(56, 120));
    const gfx::PointF centre(words.CenterPoint());
    const float rx = words.width() / 2.0f;
    const float ry = std::max(1.0f, words.height() / 2.0f);
    const SkColor4f colors[] = {{0.03f, 0.16f, 0.42f, 0.42f},
                                {0.03f, 0.16f, 0.42f, 0.22f},
                                {0.03f, 0.16f, 0.42f, 0.0f}};
    const SkScalar stops[] = {0.0f, 0.55f, 1.0f};
    cc::PaintFlags haze;
    haze.setAntiAlias(true);
    haze.setShader(cc::PaintShader::MakeRadialGradient(
        SkPoint::Make(0, 0), ry, colors, stops, 3, SkTileMode::kClamp));
    canvas->Save();
    canvas->Translate(gfx::Vector2d(static_cast<int>(centre.x()),
                                    static_cast<int>(centre.y())));
    canvas->sk_canvas()->scale(rx / ry, 1.0f);
    canvas->sk_canvas()->drawRect(SkRect::MakeLTRB(-ry, -ry, ry, ry), haze);
    canvas->Restore();
  }
}

bool ZephyrusOnboarding::OnMousePressed(const ui::MouseEvent& event) {
  // Nothing behind it is for the taking while it is up.
  return true;
}

bool ZephyrusOnboarding::OnMouseWheel(const ui::MouseWheelEvent& event) {
  return true;
}

BEGIN_METADATA(ZephyrusOnboarding)
END_METADATA

}  // namespace zephyrus

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_sidebar_view.h"

#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_private_workspace.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/display/screen.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/color_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/image/image.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/image_button_factory.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/mouse_watcher_view_host.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

constexpr int kRowHeight = 36;
// Tab rows were full pills (kRowHeight / 2) while the action rows next to them
// used the system's 10 — two different row shapes in one panel. Both are 10
// now, matching every other Zephyrus row (workspace dropdown, Shield toggles).
constexpr int kRowCornerRadius = zephyrus::kCornerRadius;
constexpr int kPanelCornerRadius = 18;
constexpr int kFaviconSize = 16;
constexpr int kRowSpacing = 3;

// Sidebar row context-menu command ids.
constexpr int kCloseTabCommand = 1;
constexpr int kMoveToWorkspaceSubmenu = 2;
// Zephyrus hides the horizontal tab strip, so this menu is the only place a
// tab can be pinned — there is no strip to right-click.
constexpr int kPinTabCommand = 3;
constexpr int kMoveToWorkspaceBase = 100;  // + workspace index
// Springy asymmetric slide: the entrance decelerates PAST the resting point
// (overshoot) and settles back — an under-damped spring, the iOS-sheet feel —
// while the exit clears out quickly with no bounce (exits never spring).
// Both directions pair the slide with a fade.
constexpr base::TimeDelta kSlideInDuration = base::Milliseconds(210);
constexpr base::TimeDelta kSettleDuration = base::Milliseconds(150);
constexpr base::TimeDelta kSlideOutDuration = base::Milliseconds(150);
constexpr int kOvershootPx = 7;

// Transform that moves the sidebar fully off-screen to the left.
gfx::Transform TuckedTransform() {
  gfx::Transform transform;
  transform.Translate(-(ZephyrusSidebarView::kSidebarWidth + 24), 0);
  return transform;
}

gfx::ImageSkia GetTabFavicon(content::WebContents* contents) {
  if (auto* driver =
          favicon::ContentFaviconDriver::FromWebContents(contents)) {
    return driver->GetFavicon().AsImageSkia();
  }
  return gfx::ImageSkia();
}

std::u16string GetTabTitle(content::WebContents* contents) {
  std::u16string title = contents->GetTitle();
  if (title.empty()) {
    title = base::UTF8ToUTF16(contents->GetVisibleURL().spec());
  }
  return title;
}

// Colors a sidebar row adapts to (derived from the active page color).
struct ZephyrusRowColors {
  SkColor foreground;  // Text, favicon fallback, close glyph.
  SkColor active_bg;   // Active row background.
  SkColor hover_bg;    // Hovered row background.
};

// A quick-action / favorite row: monochrome icon + label, pill hover fill.
// (Matches the Zephyrus Browser Design mockup's sidebar rows.)
class ZephyrusActionRow : public views::LabelButton {
  METADATA_HEADER(ZephyrusActionRow, views::LabelButton)

 public:
  ZephyrusActionRow(PressedCallback callback,
                    const std::u16string& text,
                    const gfx::VectorIcon& icon,
                    SkColor foreground)
      : views::LabelButton(std::move(callback), text),
        foreground_(foreground),
        icon_(icon) {
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetImageLabelSpacing(10);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 12)));
    SetTextColor(views::Button::STATE_NORMAL, foreground);
    SetTextColor(views::Button::STATE_HOVERED, foreground);
    SetTextColor(views::Button::STATE_PRESSED, foreground);
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(icon, foreground, 16));
    label()->SetSubpixelRenderingEnabled(false);
    GetViewAccessibility().SetName(text.empty() ? u"Action" : text);
  }

  // views::LabelButton:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
    SetBackground(hovered
                      ? views::CreateRoundedRectBackground(
                            SkColorSetA(foreground_, 0x1A),
                            zephyrus::kCornerRadius)
                      : nullptr);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, 32);
  }

 private:
  const SkColor foreground_;
  const raw_ref<const gfx::VectorIcon> icon_;
};

BEGIN_METADATA(ZephyrusActionRow)
END_METADATA

// Small quiet section caption ("Favorites", "Tabs", ...).
std::unique_ptr<views::Label> MakeSectionHeader(const std::u16string& text,
                                                SkColor foreground) {
  auto header = std::make_unique<views::Label>(text);
  header->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  header->SetEnabledColor(SkColorSetA(foreground, 0x8C));
  header->SetAutoColorReadabilityEnabled(false);
  header->SetSubpixelRenderingEnabled(false);
  header->SetFontList(header->font_list().DeriveWithSizeDelta(-1).DeriveWithWeight(
      gfx::Font::Weight::SEMIBOLD));
  header->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(10, 12, 2, 12)));
  return header;
}

// A single tab row: favicon + title, with a close button revealed on hover.
// Clicking the row (outside the close button) activates the tab.
class ZephyrusTabRow : public views::Button {
  METADATA_HEADER(ZephyrusTabRow, views::Button)

 public:
  ZephyrusTabRow(const gfx::ImageSkia& favicon,
                 const std::u16string& title,
                 bool is_active,
                 int model_index,
                 const ZephyrusRowColors& colors,
                 bool audible,
                 bool muted,
                 base::RepeatingClosure mute_callback,
                 base::RepeatingClosure activate_callback,
                 base::RepeatingClosure close_callback)
      : views::Button(base::BindRepeating(
            [](base::RepeatingClosure cb, const ui::Event&) { cb.Run(); },
            std::move(activate_callback))),
        is_active_(is_active),
        model_index_(model_index),
        colors_(colors) {
    const std::u16string accessible_title = title.empty() ? u"Tab" : title;
    GetViewAccessibility().SetName(accessible_title);
    SetTooltipText(accessible_title);

    // Stay "hovered" while the cursor is over the child close button; otherwise
    // showing the close button under the cursor makes the row flip-flop between
    // hovered/normal, blinking the button.
    SetNotifyEnterExitOnChild(true);

    // Wider horizontal padding so content sits comfortably inside the pill.
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, 12), 8));

    favicon_view_ = AddChildView(std::make_unique<views::ImageView>());
    favicon_view_->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
    SetFaviconImage(favicon);

    title_label_ = AddChildView(std::make_unique<views::Label>(title));
    title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
    title_label_->SetEnabledColor(colors_.foreground);
    title_label_->SetAutoColorReadabilityEnabled(false);
    // The sidebar paints to a translucent layer, so subpixel text AA (which
    // requires an opaque backing) must be disabled.
    title_label_->SetSubpixelRenderingEnabled(false);
    SetFlexForView(title_label_, 1);

    // Audio indicator. Unlike the close button this is NOT hover-gated: the
    // whole point is seeing at a glance which tab is making noise, and being
    // able to silence it without hunting for the tab first.
    audio_button_ = AddChildView(std::make_unique<views::ImageButton>(
        base::BindRepeating(
            [](base::RepeatingClosure cb, const ui::Event&) { cb.Run(); },
            std::move(mute_callback))));
    audio_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(
            muted ? vector_icons::kVolumeOffIcon : vector_icons::kVolumeUpIcon,
            colors_.foreground, 16));
    audio_button_->SetImageHorizontalAlignment(
        views::ImageButton::ALIGN_CENTER);
    audio_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    // Shown while playing, and while muted so the user can undo it.
    audio_button_->SetVisible(audible || muted);
    const std::u16string audio_name = muted ? u"Unmute tab" : u"Mute tab";
    audio_button_->GetViewAccessibility().SetName(audio_name);
    audio_button_->SetTooltipText(audio_name);
    views::InstallCircleHighlightPathGenerator(audio_button_);

    close_button_ = AddChildView(std::make_unique<views::ImageButton>(
        base::BindRepeating(
            [](base::RepeatingClosure cb, const ui::Event&) { cb.Run(); },
            std::move(close_callback))));
    close_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(views::kCloseIcon, colors_.foreground,
                                       16));
    close_button_->SetImageHorizontalAlignment(
        views::ImageButton::ALIGN_CENTER);
    close_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    close_button_->SetVisible(false);
    close_button_->GetViewAccessibility().SetName(u"Close tab");
    close_button_->SetTooltipText(u"Close tab");
    views::InstallCircleHighlightPathGenerator(close_button_);

    UpdateBackground();
  }

  ZephyrusTabRow(const ZephyrusTabRow&) = delete;
  ZephyrusTabRow& operator=(const ZephyrusTabRow&) = delete;
  ~ZephyrusTabRow() override = default;

  // views::Button:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
    close_button_->SetVisible(hovered);
    UpdateBackground();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kRowHeight);
  }

  int model_index() const { return model_index_; }

  // Updates the row's favicon/title/active state in place (without recreating
  // the view), preserving hover state.
  void UpdateContent(const gfx::ImageSkia& favicon,
                     const std::u16string& title,
                     bool is_active) {
    SetFaviconImage(favicon);
    title_label_->SetText(title);
    const std::u16string accessible_title = title.empty() ? u"Tab" : title;
    GetViewAccessibility().SetName(accessible_title);
    SetTooltipText(accessible_title);
    if (is_active_ != is_active) {
      is_active_ = is_active;
      UpdateBackground();
    }
  }

  // Refreshed in place (rather than via a list rebuild) so the row under the
  // cursor isn't destroyed every time a tab starts or stops making noise.
  void SetAudioState(bool audible, bool muted) {
    if (!audio_button_) {
      return;
    }
    audio_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(
            muted ? vector_icons::kVolumeOffIcon : vector_icons::kVolumeUpIcon,
            colors_.foreground, 16));
    audio_button_->SetVisible(audible || muted);
    const std::u16string audio_name = muted ? u"Unmute tab" : u"Mute tab";
    audio_button_->GetViewAccessibility().SetName(audio_name);
    audio_button_->SetTooltipText(audio_name);
  }

 private:
  void SetFlexForView(views::View* view, int flex) {
    static_cast<views::BoxLayout*>(GetLayoutManager())
        ->SetFlexForView(view, flex);
  }

  void SetFaviconImage(const gfx::ImageSkia& favicon) {
    if (!favicon.isNull()) {
      favicon_view_->SetImage(ui::ImageModel::FromImageSkia(favicon));
    } else {
      favicon_view_->SetImage(ui::ImageModel::FromVectorIcon(
          vector_icons::kGlobeIcon, colors_.foreground, kFaviconSize));
    }
  }

  void UpdateBackground() {
    const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
    SkColor color = SK_ColorTRANSPARENT;
    if (is_active_) {
      color = colors_.active_bg;
    } else if (hovered) {
      color = colors_.hover_bg;
    }
    SetBackground(views::CreateRoundedRectBackground(color, kRowCornerRadius));
  }

  bool is_active_;
  int model_index_;
  ZephyrusRowColors colors_;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::ImageButton> audio_button_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
};

BEGIN_METADATA(ZephyrusTabRow)
END_METADATA

}  // namespace

ZephyrusSidebarHotZone::ZephyrusSidebarHotZone(base::RepeatingClosure on_enter)
    : on_enter_(std::move(on_enter)) {}

ZephyrusSidebarHotZone::~ZephyrusSidebarHotZone() = default;

void ZephyrusSidebarHotZone::OnMouseEntered(const ui::MouseEvent& event) {
  on_enter_.Run();
}

BEGIN_METADATA(ZephyrusSidebarHotZone)
END_METADATA

ZephyrusSidebarView::ZephyrusSidebarView(BrowserView* browser_view)
    : browser_view_(browser_view),
      tab_strip_model_(browser_view->browser()->tab_strip_model()) {
  // Frosted translucent panel (dark by default; adapts to the page color via
  // SetZephyrusColor()).
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kPanelCornerRadius));
  layer()->SetBackgroundBlur(24.0f);
  SetBackground(
      views::CreateRoundedRectBackground(GetPanelColor(), kPanelCornerRadius));

  auto* box_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(10), kRowSpacing));

  // ---- Quick actions, grouped on their own card ----------------------------
  // The three actions are a fixed group, unlike the lists below them, so they
  // sit on a raised card. Grouping by surface rather than by a heading or a
  // divider is how the rest of the system separates blocks (see the Shield
  // panel's stats card).
  const SkColor fg_ink = GetForegroundColor();
  auto* actions_card = AddChildView(std::make_unique<views::View>());
  actions_card->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetA(fg_ink, 0x0D), zephyrus::kCornerRadius));
  actions_card->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(4), 1));
  auto add_action = [&](const gfx::VectorIcon& icon, const char16_t* label,
                        int command) {
    actions_card->AddChildView(std::make_unique<ZephyrusActionRow>(
        base::BindRepeating(
            [](ZephyrusSidebarView* self, int cmd, const ui::Event&) {
              self->ExecuteBrowserCommand(cmd);
            },
            base::Unretained(this), command),
        label, icon, fg_ink));
  };
  // Private Workspace is not a browser command — it swaps this window for the
  // OTR-backed one rather than opening an incognito window, so it is wired
  // directly to the controller instead of going through ExecuteBrowserCommand.
  // The one row is a toggle: in a private window it reads (and does) "Exit",
  // otherwise "Open". EnterPrivateWorkspace() already picks Enter vs Leave.
  const bool in_private =
      ZephyrusPrivateWorkspace::IsPrivate(browser_view_->browser());
  actions_card->AddChildView(std::make_unique<ZephyrusActionRow>(
      base::BindRepeating(
          [](ZephyrusSidebarView* self, const ui::Event&) {
            self->EnterPrivateWorkspace();
          },
          base::Unretained(this)),
      in_private ? l10n_util::GetStringUTF16(IDS_ZEPHYRUS_EXIT_PRIVATE_WORKSPACE)
                 : l10n_util::GetStringUTF16(IDS_ZEPHYRUS_OPEN_PRIVATE_WORKSPACE),
      kZephyrusPrivateWorkspaceIcon, fg_ink));
  add_action(kZephyrusDownloadIcon, u"Downloads", IDC_SHOW_DOWNLOADS);
  add_action(kZephyrusHistoryIcon, u"History", IDC_SHOW_HISTORY);

  // ---- Favorites (bookmark bar entries) -------------------------------------
  // Kept so the heading can be hidden when there are no bookmarks, rather than
  // labelling an empty space.
  favorites_header_ = AddChildView(MakeSectionHeader(u"Favorites", fg_ink));
  favorites_container_ = AddChildView(std::make_unique<views::View>());
  favorites_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacing));
  RebuildFavorites();

  // ---- Tabs ------------------------------------------------------------------
  // No static heading here: RebuildTabList emits "Pinned tabs" and "Tabs"
  // itself, so the pinned group can sit above the workspace's own tabs.
  tab_list_container_ = AddChildView(std::make_unique<views::View>());
  tab_list_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacing));
  // The tab list takes the remaining height, so it pushes the version label to
  // the very bottom of the panel.
  box_layout->SetFlexForView(tab_list_container_, 1);

  // Zephyrus product version, quiet at the foot of the sidebar.
  auto* version = AddChildView(std::make_unique<views::Label>(
      u"Zephyrus " + std::u16string(zephyrus::kVersion)));
  version->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  version->SetAutoColorReadabilityEnabled(false);
  version->SetSubpixelRenderingEnabled(false);
  version->SetEnabledColor(SkColorSetA(fg_ink, 0x66));
  version->SetFontList(version->font_list().DeriveWithSizeDelta(-2));
  version->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(6, 0, 2, 0)));

  // Start tucked off-screen and transparent to events so it doesn't intercept
  // input over the web contents until revealed.
  layer()->SetTransform(TuckedTransform());
  layer()->SetOpacity(0.0f);
  SetCanProcessEventsWithinSubtree(false);

  // Extend the watched zone left of the panel so the cursor sitting on the
  // edge hot-zone (left of the panel's margin) doesn't immediately re-tuck it.
  mouse_watcher_ = std::make_unique<views::MouseWatcher>(
      std::make_unique<views::MouseWatcherViewHost>(
          this, gfx::Insets::TLBR(0, 16, 0, 0)),
      this);

  reveal_poll_timer_.Start(FROM_HERE, base::Milliseconds(100), this,
                           &ZephyrusSidebarView::OnRevealPoll);

  tab_strip_model_->AddObserver(this);
  RebuildTabList();
}

ZephyrusSidebarView::~ZephyrusSidebarView() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void ZephyrusSidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  RebuildTabList();
}

void ZephyrusSidebarView::MouseMovedOutOfHost() {
  TuckAway();
}

void ZephyrusSidebarView::Reveal() {
  if (revealed_) {
    return;
  }
  revealed_ = true;
  reveal_poll_timer_.Stop();
  SetCanProcessEventsWithinSubtree(true);
  // Refresh rows (titles/favicons/favorites) and clear any stale hover state.
  RebuildTabList();
  RebuildFavorites();

  ui::LayerAnimator* animator = layer()->GetAnimator();
  // Retarget from wherever the panel currently is (mid-exit included).
  animator->AbortAllAnimations();
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    layer()->SetTransform(gfx::Transform());
    layer()->SetOpacity(1.0f);
  } else {
    // Fade runs as its own single-element sequence.
    auto fade = ui::LayerAnimationElement::CreateOpacityElement(
        1.0f, base::Milliseconds(150));
    fade->set_tween_type(gfx::Tween::EASE_OUT);
    animator->StartAnimation(
        new ui::LayerAnimationSequence(std::move(fade)));
    // Slide: decelerate into a small overshoot, then settle back to rest.
    gfx::Transform overshoot;
    overshoot.Translate(kOvershootPx, 0);
    auto out = ui::LayerAnimationElement::CreateTransformElement(
        overshoot, kSlideInDuration);
    out->set_tween_type(gfx::Tween::EASE_OUT_2);
    auto settle = ui::LayerAnimationElement::CreateTransformElement(
        gfx::Transform(), kSettleDuration);
    settle->set_tween_type(gfx::Tween::EASE_IN_OUT);
    auto* slide = new ui::LayerAnimationSequence(std::move(out));
    slide->AddElement(std::move(settle));
    animator->StartAnimation(slide);
  }

  if (GetWidget()) {
    mouse_watcher_->Start(GetWidget()->GetNativeWindow());
  }
}

void ZephyrusSidebarView::TuckAway() {
  if (!revealed_) {
    return;
  }
  revealed_ = false;
  SetCanProcessEventsWithinSubtree(false);

  // Abort any in-flight reveal (incl. its overshoot/settle chain) so the exit
  // starts from the panel's current on-screen position.
  layer()->GetAnimator()->AbortAllAnimations();
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(gfx::Animation::ShouldRenderRichAnimation()
                                     ? kSlideOutDuration
                                     : base::TimeDelta());
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  layer()->SetTransform(TuckedTransform());
  layer()->SetOpacity(0.0f);

  reveal_poll_timer_.Start(FROM_HERE, base::Milliseconds(100), this,
                           &ZephyrusSidebarView::OnRevealPoll);
}

void ZephyrusSidebarView::OnRevealPoll() {
  if (revealed_ || !GetWidget() || !GetWidget()->IsActive()) {
    return;
  }
  const gfx::Point cursor =
      display::Screen::Get()->GetCursorScreenPoint();
  const gfx::Rect window = GetWidget()->GetWindowBoundsInScreen();
  const gfx::Rect panel = GetBoundsInScreen();  // logical (revealed) position.
  // A generous trigger band along the left side. Measured from the window's
  // left edge, which on a maximized window extends a few pixels off-screen, so
  // the band must be wide enough that an on-screen cursor near the edge counts.
  constexpr int kEdgeZone = 30;
  if (cursor.x() >= window.x() && cursor.x() < window.x() + kEdgeZone &&
      cursor.y() >= panel.y() && cursor.y() <= panel.bottom()) {
    Reveal();
  }
}

void ZephyrusSidebarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // While tucked away the list isn't visible and Reveal() rebuilds it anyway,
  // so skip the (comparatively expensive) row churn during background tab
  // activity — this keeps the hidden sidebar at zero cost.
  if (!revealed_) {
    return;
  }
  RebuildTabList();
}

void ZephyrusSidebarView::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                  int index) {
  if (!revealed_) {
    return;
  }
  // A newly pinned tab becomes visible in every workspace, so the list has to
  // be rebuilt rather than just refreshed in place.
  RebuildTabList();
}

void ZephyrusSidebarView::OnTabChangedAt(tabs::TabInterface* tab,
                                         int index,
                                         TabChangeType change_type) {
  if (!revealed_) {
    return;
  }
  // Update only the affected row's favicon/title in place. Rebuilding the whole
  // list here would destroy the row under the cursor on every loading tick,
  // causing the hover background and close button to flicker.
  content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
  if (!contents) {
    return;
  }
  // Rows are a filtered subset of the model, so find the row by model index.
  for (views::View* child : tab_list_container_->children()) {
    auto* row = views::AsViewClass<ZephyrusTabRow>(child);
    if (row && row->model_index() == index) {
      row->UpdateContent(GetTabFavicon(contents), GetTabTitle(contents),
                         index == tab_strip_model_->active_index());
      row->SetAudioState(contents->IsCurrentlyAudible(),
                         contents->IsAudioMuted());
      return;
    }
  }
}

void ZephyrusSidebarView::RebuildFavorites() {
  if (!favorites_container_) {
    return;
  }
  favorites_container_->RemoveAllChildViews();
  bookmarks::BookmarkModel* model =
      BookmarkModelFactory::GetForBrowserContext(
          browser_view_->browser()->profile());
  if (!model || !model->loaded()) {
    return;
  }
  const SkColor fg_ink = GetForegroundColor();
  const bookmarks::BookmarkNode* bar = model->bookmark_bar_node();
  size_t added = 0;
  for (const auto& node : bar->children()) {
    if (!node->is_url() || added >= 6) {
      continue;
    }
    const GURL url = node->url();
    favorites_container_->AddChildView(std::make_unique<ZephyrusActionRow>(
        base::BindRepeating(
            [](ZephyrusSidebarView* self, GURL url, const ui::Event&) {
              NavigateParams params(self->browser_view_->browser(), url,
                                    ui::PAGE_TRANSITION_AUTO_BOOKMARK);
              params.disposition = WindowOpenDisposition::CURRENT_TAB;
              Navigate(&params);
            },
            base::Unretained(this), url),
        node->GetTitle(), vector_icons::kGlobeIcon, fg_ink));
    ++added;
  }
  // Don't leave a "Favorites" heading standing over an empty space when the
  // user has no bookmarks.
  if (favorites_header_) {
    favorites_header_->SetVisible(added > 0);
  }
}

void ZephyrusSidebarView::RebuildTabList() {
  if (!tab_list_container_) {
    return;
  }
  tab_list_container_->RemoveAllChildViews();

  const SkColor fg = GetForegroundColor();
  const ZephyrusRowColors row_colors{
      .foreground = fg,
      .active_bg = SkColorSetA(fg, 0x33),
      .hover_bg = SkColorSetA(fg, 0x1A),
  };

  ZephyrusWorkspaceManager* workspace_manager =
      browser_view_->zephyrus_workspace_manager();
  const int active_index = tab_strip_model_->active_index();

  // Adds one row for the tab at `index`. Shared by both sections below.
  auto add_row = [&](int index) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
    auto* row = tab_list_container_->AddChildView(
        std::make_unique<ZephyrusTabRow>(
            GetTabFavicon(contents), GetTabTitle(contents),
            index == active_index, index, row_colors,
            contents->IsCurrentlyAudible(), contents->IsAudioMuted(),
            base::BindRepeating(&ZephyrusSidebarView::ToggleTabMuted,
                                base::Unretained(this), index),
            base::BindRepeating(&ZephyrusSidebarView::ActivateTab,
                                base::Unretained(this), index),
            base::BindRepeating(&ZephyrusSidebarView::CloseTab,
                                base::Unretained(this), index)));
    // Right-click a row -> "Pin tab" / "Move to workspace" / "Close tab".
    row->set_context_menu_controller(this);
  };

  // Collect first so the pinned section can be skipped entirely when empty
  // (rather than leaving a header with nothing under it).
  std::vector<int> pinned;
  std::vector<int> unpinned;
  for (int i = 0; i < tab_strip_model_->count(); ++i) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    // Show this workspace's tabs, plus the pinned tabs, which are global.
    if (workspace_manager &&
        !workspace_manager->IsContentsInCurrentWorkspace(contents)) {
      continue;
    }
    if (tab_strip_model_->IsTabPinned(i)) {
      pinned.push_back(i);
    } else {
      unpinned.push_back(i);
    }
  }

  const SkColor header_ink = GetForegroundColor();
  if (!pinned.empty()) {
    tab_list_container_->AddChildView(
        MakeSectionHeader(u"Pinned tabs", header_ink));
    for (int index : pinned) {
      add_row(index);
    }
  }
  if (!unpinned.empty()) {
    tab_list_container_->AddChildView(MakeSectionHeader(u"Tabs", header_ink));
    for (int index : unpinned) {
      add_row(index);
    }
  }
}

void ZephyrusSidebarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  auto* row = views::AsViewClass<ZephyrusTabRow>(source);
  if (!row) {
    return;
  }
  const int row_index = row->model_index();
  if (row_index < 0 || row_index >= tab_strip_model_->count()) {
    return;
  }
  // Remember the tab itself, not its index — see the member's comment.
  context_menu_contents_ = tab_strip_model_->GetWebContentsAt(row_index);
  if (!context_menu_contents_) {
    return;
  }

  ZephyrusWorkspaceManager* manager =
      browser_view_->zephyrus_workspace_manager();
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  context_menu_workspace_ids_.clear();

  if (manager && manager->workspaces().size() > 1) {
    const int current_ws =
        manager->GetWorkspaceForContents(context_menu_contents_);
    workspace_submenu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    for (const ZephyrusWorkspaceManager::Workspace& ws :
         manager->workspaces()) {
      const size_t idx = context_menu_workspace_ids_.size();
      context_menu_workspace_ids_.push_back(ws.id);
      std::u16string label =
          ws.emoji.empty() ? ws.name : (ws.emoji + u"  " + ws.name);
      workspace_submenu_model_->AddItem(kMoveToWorkspaceBase +
                                            static_cast<int>(idx),
                                        label);
      // A check-mark next to the tab's current workspace.
      if (ws.id == current_ws) {
        workspace_submenu_model_->SetEnabledAt(idx, false);
      }
    }
    context_menu_model_->AddSubMenu(kMoveToWorkspaceSubmenu,
                                    u"Move to workspace",
                                    workspace_submenu_model_.get());
    context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  // Pinned tabs are global: they stay visible in every workspace.
  const bool pinned = tab_strip_model_->IsTabPinned(row_index);
  context_menu_model_->AddItem(
      kPinTabCommand, pinned ? u"Unpin tab" : u"Pin tab (all workspaces)");
  context_menu_model_->AddItem(kCloseTabCommand, u"Close tab");

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
}

bool ZephyrusSidebarView::IsCommandIdChecked(int command_id) const {
  return false;
}

bool ZephyrusSidebarView::IsCommandIdEnabled(int command_id) const {
  return true;
}

void ZephyrusSidebarView::ExecuteCommand(int command_id, int event_flags) {
  // Re-resolve the tab by identity. If it went away while the menu was open,
  // do nothing rather than acting on whatever now sits at the old index.
  if (!context_menu_contents_) {
    return;
  }
  const int index =
      tab_strip_model_->GetIndexOfWebContents(context_menu_contents_);
  if (index == TabStripModel::kNoTab) {
    return;
  }
  if (command_id == kCloseTabCommand) {
    CloseTab(index);
    return;
  }
  if (command_id == kPinTabCommand) {
    // SetTabPinned moves the tab to the strip's pinned section and returns its
    // new index, so nothing else may rely on `index` after this.
    tab_strip_model_->SetTabPinned(index, !tab_strip_model_->IsTabPinned(index));
    return;
  }
  if (command_id >= kMoveToWorkspaceBase) {
    const size_t idx = static_cast<size_t>(command_id - kMoveToWorkspaceBase);
    if (idx >= context_menu_workspace_ids_.size()) {
      return;
    }
    if (ZephyrusWorkspaceManager* manager =
            browser_view_->zephyrus_workspace_manager()) {
      manager->MoveContentsToWorkspace(context_menu_contents_,
                                       context_menu_workspace_ids_[idx]);
    }
  }
}

void ZephyrusSidebarView::ActivateTab(int model_index) {
  if (model_index >= 0 && model_index < tab_strip_model_->count()) {
    tab_strip_model_->ActivateTabAt(model_index);
  }
}

void ZephyrusSidebarView::ToggleTabMuted(int model_index) {
  if (model_index < 0 || model_index >= tab_strip_model_->count()) {
    return;
  }
  content::WebContents* contents =
      tab_strip_model_->GetWebContentsAt(model_index);
  if (!contents) {
    return;
  }
  contents->SetAudioMuted(!contents->IsAudioMuted());
  // Update the row IN PLACE. Rebuilding here would destroy the very button
  // whose click callback is still on the stack — a use-after-free the moment
  // views touches the button again after this returns.
  for (views::View* child : tab_list_container_->children()) {
    auto* row = views::AsViewClass<ZephyrusTabRow>(child);
    if (row && row->model_index() == model_index) {
      row->SetAudioState(contents->IsCurrentlyAudible(),
                         contents->IsAudioMuted());
      break;
    }
  }
}

void ZephyrusSidebarView::CloseTab(int model_index) {
  if (model_index >= 0 && model_index < tab_strip_model_->count()) {
    tab_strip_model_->CloseWebContentsAt(model_index, CLOSE_USER_GESTURE);
  }
}

void ZephyrusSidebarView::ExecuteBrowserCommand(int command) {
  chrome::ExecuteCommand(browser_view_->browser(), command);
}

void ZephyrusSidebarView::EnterPrivateWorkspace() {
  Browser* browser = browser_view_->browser();
  if (!browser) {
    return;
  }
  // Already private: this row takes you back out, so one control toggles.
  if (ZephyrusPrivateWorkspace::IsPrivate(browser)) {
    if (auto* controller =
            ZephyrusPrivateWorkspace::GetForProfile(browser->profile())) {
      controller->Leave();
    }
    return;
  }
  if (auto* controller =
          ZephyrusPrivateWorkspace::GetForProfile(browser->profile())) {
    controller->Enter(browser);
  }
}

SkColor ZephyrusSidebarView::GetZephyrusBase() const {
  // BrowserView pushes the window's base color through SetZephyrusColor(), so
  // the sidebar picks up the Private Workspace variant automatically rather
  // than hardcoding the normal theme.
  return page_color_.value_or(BrowserView::kZephyrusThemeColor);
}

SkColor ZephyrusSidebarView::GetForegroundColor() const {
  // Max-contrast ink over the panel's base — white, on either theme. Every
  // other sidebar color (row hover/active fills, section headings, the
  // quick-actions card, favicon fallbacks) is derived from this by alpha, so
  // they all follow from this one value.
  return color_utils::GetColorWithMaxContrast(GetZephyrusBase());
}

SkColor ZephyrusSidebarView::GetPanelColor() const {
  // Dark frosted glass on the permanent theme. Still translucent, so the
  // layer's backdrop blur keeps the material feel over page content, but the
  // panel is now the same family as the title bar and the popovers instead of
  // the one light surface in the browser. Lifted slightly off the raw theme
  // color so it reads as a panel above the window rather than a hole in it.
  return SkColorSetA(
      color_utils::AlphaBlend(SK_ColorWHITE, GetZephyrusBase(), SkAlpha{0x14}),
      0xE6);
}

void ZephyrusSidebarView::SetZephyrusColor(std::optional<SkColor> page_color) {
  if (page_color_ == page_color) {
    return;
  }
  page_color_ = page_color;
  SetBackground(
      views::CreateRoundedRectBackground(GetPanelColor(), kPanelCornerRadius));
  // Row recolor only matters when visible; Reveal() rebuilds with the latest
  // colors anyway.
  if (revealed_) {
    RebuildTabList();
  }
}

BEGIN_METADATA(ZephyrusSidebarView)
END_METADATA

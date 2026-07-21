// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar/toolbar_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/command_line.h"
#include "base/containers/fixed_flat_map.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/i18n/number_formatting.h"
#include "base/i18n/rtl.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/notimplemented.h"
#include "base/strings/utf_string_conversions.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/actor/ui/actor_ui_metrics.h"
#include "chrome/browser/actor/ui/task_list_bubble/actor_task_list_bubble_controller.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/command_updater.h"
#include "chrome/browser/net/secure_dns_config.h"
#include "chrome/browser/glic/browser_ui/glic_actor_task_icon_manager_factory.h"
#include "chrome/browser/glic/browser_ui/glic_button_controller.h"
#include "chrome/browser/glic/browser_ui/glic_nudge_controller.h"
#include "chrome/browser/glic/public/features.h"
#include "chrome/browser/glic/public/glic_enabling.h"
#include "chrome/browser/glic/public/glic_invoke_options.h"
#include "chrome/browser/glic/public/glic_keyed_service.h"
#include "chrome/browser/glic/public/glic_keyed_service_factory.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/performance_manager/public/user_tuning/user_tuning_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/themes/theme_properties.h"
#include "chrome/browser/ui/actions/chrome_action_id.h"
#include "chrome/browser/ui/ai_overlay_dialog/ai_overlay_dialog_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_actions.h"
#include "chrome/browser/ui/browser_command_controller.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_content_setting_bubble_model_delegate.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/global_error/global_error_service.h"
#include "chrome/browser/ui/global_error/global_error_service_factory.h"
#include "chrome/browser/ui/intent_picker_tab_helper.h"
#include "chrome/browser/ui/layout_constants.h"
#include "chrome/browser/ui/omnibox/omnibox_view.h"
#include "chrome/browser/ui/page_action/page_action_properties_provider.h"
#include "chrome/browser/ui/tabs/public/tab_features.h"
#include "chrome/browser/ui/tabs/tab_strip_prefs.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/toolbar/chrome_labs/chrome_labs_prefs.h"
#include "chrome/browser/ui/toolbar/chrome_labs/chrome_labs_utils.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/bookmarks/bookmark_bubble_view.h"
#include "chrome/browser/ui/views/contextual_tasks/contextual_tasks_button.h"
#include "chrome/browser/ui/views/contextual_tasks/contextual_tasks_close_tab_button.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_button.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_coordinator.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_desktop.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/custom_corners_background.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_private_workspace.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_tab_helper.h"
#include "base/strings/string_number_conversions.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/mojom/dialog_button.mojom-shared.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "chrome/browser/ui/views/glic/glic_button_interface.h"
#include "chrome/browser/ui/views/global_media_controls/media_toolbar_button_contextual_menu.h"
#include "chrome/browser/ui/views/global_media_controls/media_toolbar_button_view.h"
#include "chrome/browser/ui/views/location_bar/intent_chip_button.h"
#include "chrome/browser/ui/views/location_bar/star_view.h"
#include "chrome/browser/ui/views/location_bar/webui_location_bar.h"
#include "chrome/browser/ui/views/page_action/page_action_container_view.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_container.h"
#include "chrome/browser/ui/views/page_action/page_action_icon_controller.h"
#include "chrome/browser/ui/views/page_action/page_action_view.h"
#include "chrome/browser/ui/views/page_action/page_action_view_interface.h"
#include "chrome/browser/ui/views/performance_controls/battery_saver_button.h"
#include "chrome/browser/ui/views/performance_controls/performance_intervention_button.h"
#include "chrome/browser/ui/views/side_panel/side_panel.h"
#include "chrome/browser/ui/views/tabs/glic/glic_and_actor_buttons_container.h"
#include "chrome/browser/ui/views/tabs/tab_strip.h"
#include "chrome/browser/ui/views/tabs/tab_strip_controller.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "chrome/browser/ui/views/toolbar/app_menu_control.h"
#include "chrome/browser/ui/views/toolbar/avatar_toolbar_button_interface.h"
#include "chrome/browser/ui/views/toolbar/back_forward_button.h"
#include "chrome/browser/ui/views/toolbar/browser_app_menu_button.h"
#include "chrome/browser/ui/views/toolbar/chrome_labs/chrome_labs_coordinator.h"
#include "chrome/browser/ui/views/toolbar/home_button.h"
#include "chrome/browser/ui/views/toolbar/pinned_toolbar_actions_container.h"
#include "chrome/browser/ui/views/toolbar/reload_button.h"
#include "chrome/browser/ui/views/toolbar/split_tabs_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_controller.h"
#include "chrome/browser/ui/views/toolbar/toolbar_divider.h"
#include "chrome/browser/ui/views/toolbar/toolbar_glic_actor_task_icon.h"
#include "chrome/browser/ui/views/toolbar/toolbar_glic_button.h"
#include "chrome/browser/ui/views/toolbar/toolbar_icon_container_view.h"
#include "chrome/browser/ui/views/toolbar/webui_back_forward_control.h"
#include "chrome/browser/ui/views/toolbar/webui_toolbar_web_view.h"
#include "chrome/browser/ui/views/zoom/zoom_view_controller.h"
#include "chrome/browser/ui/waap/initial_webui_window_metrics_manager.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/web_applications/link_capturing_features.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/gfx/color_utils.h"
#include "chrome/grit/theme_resources.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/contextual_tasks/public/features.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/send_tab_to_self/features.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/hit_test.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/theme_provider.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/window_open_disposition.h"
#include "ui/base/window_open_disposition_utils.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets_f.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/border.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/actions/action_view_controller.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/gfx/font_list.h"
#include "ui/views/background.h"
#include "ui/views/cascading_property.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/separator.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_view.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/mouse_watcher.h"
#include "ui/views/mouse_watcher_view_host.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/tooltip_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/vector_icons.h"
#include "ui/views/window/frame_caption_button.h"
#include "ui/views/window/vector_icons/vector_icons.h"
#include "base/numerics/safe_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/windows_icon_painter.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/mojom/dialog_button.mojom-shared.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"

#if defined(USE_AURA)
#include <array>

#include "ui/aura/window_occlusion_tracker.h"
#endif

using base::UserMetricsAction;
using content::WebContents;

// Zephyrus: drives the title bar "chameleon" crossfade — instead of snapping
// to each page's color, the bar lerps there over ~220ms (ease-out). Owned by
// ToolbarView; forward-declared in toolbar_view.h.
class ZephyrusColorTransition : public gfx::AnimationDelegate {
 public:
  ZephyrusColorTransition(base::RepeatingCallback<void(SkColor)> on_frame,
                          base::RepeatingClosure on_end)
      : on_frame_(std::move(on_frame)), on_end_(std::move(on_end)) {}
  ZephyrusColorTransition(const ZephyrusColorTransition&) = delete;
  ZephyrusColorTransition& operator=(const ZephyrusColorTransition&) = delete;
  ~ZephyrusColorTransition() override = default;

  // Retargetable: starting while animating simply lerps onward from |from|
  // (callers pass the currently-applied intermediate color), so rapid
  // navigation stays continuous instead of jump-cutting.
  void Start(SkColor from, SkColor to) {
    from_ = from;
    to_ = to;
    animation_.Start();
  }
  bool is_animating() const { return animation_.is_animating(); }

 private:
  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    const double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT,
                                                animation->GetCurrentValue());
    on_frame_.Run(gfx::Tween::ColorValueBetween(t, from_, to_));
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    on_end_.Run();
  }

  base::RepeatingCallback<void(SkColor)> on_frame_;
  base::RepeatingClosure on_end_;
  SkColor from_ = SK_ColorTRANSPARENT;
  SkColor to_ = SK_ColorTRANSPARENT;
  gfx::LinearAnimation animation_{base::Milliseconds(220), 60, this};
};

// Zephyrus: drives the omnibox pill's focus expansion (compact domain pill →
// comfortable editing width). A SlideAnimation so focus/blur mid-animation
// reverses smoothly from the current width instead of jumping. Owned by
// ToolbarView; forward-declared in toolbar_view.h.
class ZephyrusOmniboxFocusAnimation : public gfx::AnimationDelegate {
 public:
  explicit ZephyrusOmniboxFocusAnimation(base::RepeatingClosure on_frame)
      : on_frame_(std::move(on_frame)) {
    animation_.SetSlideDuration(base::Milliseconds(250));
    animation_.SetTweenType(gfx::Tween::EASE_OUT_3);
  }
  ZephyrusOmniboxFocusAnimation(const ZephyrusOmniboxFocusAnimation&) = delete;
  ZephyrusOmniboxFocusAnimation& operator=(
      const ZephyrusOmniboxFocusAnimation&) = delete;
  ~ZephyrusOmniboxFocusAnimation() override = default;

  void SetFocused(bool focused) {
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      animation_.Reset(focused ? 1.0 : 0.0);
      on_frame_.Run();
      return;
    }
    if (focused) {
      animation_.Show();
    } else {
      animation_.Hide();
    }
  }

  // 0 = compact steady pill, 1 = full editing width.
  double value() const { return animation_.GetCurrentValue(); }

 private:
  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    on_frame_.Run();
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    on_frame_.Run();
  }

  base::RepeatingClosure on_frame_;
  gfx::SlideAnimation animation_{this};
};

DEFINE_UI_CLASS_PROPERTY_KEY(bool, kActionItemUnderlineIndicatorKey, false)

namespace {

// Gets the display mode for a given browser.
ToolbarView::DisplayMode GetDisplayMode(Browser* browser) {
  // Checked in this order because even tabbed PWAs use the CUSTOM_TAB
  // display mode.
  if (web_app::AppBrowserController::IsWebApp(browser)) {
    return ToolbarView::DisplayMode::kCustomTab;
  }

  if (browser->SupportsWindowFeature(
          Browser::WindowFeature::kFeatureTabStrip)) {
    return ToolbarView::DisplayMode::kNormal;
  }

  return ToolbarView::DisplayMode::kLocation;
}

auto& GetViewCommandMap() {
  static constexpr auto kViewCommandMap = base::MakeFixedFlatMap<int, int>(
      {{VIEW_ID_BACK_BUTTON, IDC_BACK},
       {VIEW_ID_FORWARD_BUTTON, IDC_FORWARD},
       {VIEW_ID_HOME_BUTTON, IDC_HOME},
       {VIEW_ID_RELOAD_BUTTON, IDC_RELOAD},
       {VIEW_ID_AVATAR_BUTTON, IDC_SHOW_AVATAR_MENU}});
  return kViewCommandMap;
}

constexpr int kBrowserAppMenuRefreshExpandedMargin = 5;
constexpr int kBrowserAppMenuRefreshCollapsedMargin = 2;
constexpr int kLargeSpaceBetweenButtons = 6;
constexpr int kInsideBorderAroundGlicButtons = 2;
constexpr int kOutsideBorderAroundGlicButtons = 11;
constexpr int kGlicButtonMargin = 5;

// Returns whether `point` should be treated as part of the caption area in
// `view`. Recursively traverses into icon containers to correctly handle
// padding between buttons.
bool IsPositionInWindowCaptionForView(const views::View* view,
                                      const gfx::Point& point) {
  for (const views::View* child : view->children()) {
    if (!child->GetVisible() || !child->bounds().Contains(point)) {
      continue;
    }
    // Recurse into known icon container types to check their children.
    if (views::IsViewClass<ToolbarIconContainerView>(child) ||
        views::IsViewClass<page_actions::PageActionContainerView>(child)) {
      const gfx::Point point_in_child =
          views::View::ConvertPointToTarget(view, child, point);
      return IsPositionInWindowCaptionForView(child, point_in_child);
    }
    // Separators and dividers are non-interactive and should be treated
    // as caption area.
    if (views::IsViewClass<views::Separator>(child) ||
        views::IsViewClass<ToolbarDivider>(child)) {
      return true;
    }
    // The point hit an interactive control (button, location bar, etc.).
    return false;
  }
  // The point is not in any child's bounds — it's in empty space between
  // children, padding, or above/below a child. In VTS mode the toolbar is
  // at the very top of the window, so all non-interactive areas should be
  // draggable regardless of vertical position.
  return true;
}

void SetRefreshMargins(views::View* button, bool expanded) {
  button->SetProperty(
      views::kMarginsKey,
      gfx::Insets::VH(0, expanded ? kBrowserAppMenuRefreshExpandedMargin
                                  : kBrowserAppMenuRefreshCollapsedMargin));
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, public:

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(ToolbarView, kToolbarElementId);

ToolbarView::ToolbarView(Browser* browser, BrowserView* browser_view)
    : AnimationDelegateViews(this),
      browser_(browser),
      browser_view_(browser_view),
      app_menu_icon_controller_(browser->profile(), this),
      display_mode_(GetDisplayMode(browser)) {
  // WebApp type-browsers set their own ToolbarButtonProvider.
  if (!web_app::AppBrowserController::IsWebApp(browser)) {
    scoped_unowned_user_data_.emplace(browser_->GetUnownedUserDataHost(),
                                      *this);
  }

  SetID(VIEW_ID_TOOLBAR);
  SetProperty(views::kElementIdentifierKey, kToolbarElementId);

  GetViewAccessibility().SetRole(ax::mojom::Role::kToolbar);

  if (display_mode_ == DisplayMode::kNormal) {
    for (const auto& view_and_command : GetViewCommandMap()) {
      chrome::AddCommandObserver(browser_, view_and_command.second, this);
    }
  }
  views::SetCascadingColorProviderColor(this, views::kCascadingBackgroundColor,
                                        kColorToolbar);

  mouse_watcher_ = std::make_unique<views::MouseWatcher>(
      std::make_unique<views::MouseWatcherViewHost>(this, gfx::Insets()), this);

  glic::GlicNudgeController* glic_nudge_controller =
      browser_->browser_window_features()->glic_nudge_controller();

  // `glic_nudge_controller` will be null if feature is not enabled.
  if (glic_nudge_controller) {
    glic_nudge_controller->SetToolbarDelegate(this);
  }
}

ToolbarView::~ToolbarView() {
  if (display_mode_ != DisplayMode::kNormal) {
    return;
  }

  overflow_button_->set_toolbar_controller(nullptr);

  for (const auto& view_and_command : GetViewCommandMap()) {
    chrome::RemoveCommandObserver(browser_, view_and_command.second, this);
  }

  glic::GlicNudgeController* glic_nudge_controller =
      browser_->browser_window_features()->glic_nudge_controller();
  if (glic_nudge_controller) {
    glic_nudge_controller->SetToolbarDelegate(/*delegate=*/nullptr);
  }
}

void ToolbarView::Init() {
#if defined(USE_AURA)
  // Avoid generating too many occlusion tracking calculation events before this
  // function returns. The occlusion status will be computed only once once this
  // function returns.
  // See crbug.com/40171404#comment3
  aura::WindowOcclusionTracker::ScopedPause pause_occlusion;
#endif

  std::unique_ptr<LocationBarView> location_bar_view;
  std::unique_ptr<WebUILocationBar> webui_location_bar;
  if (features::IsWebUILocationBarEnabled() &&
      /* TODO(http://crbug.com/470042732): Figure out where we fit in other
       * modes. When doing this, we have to be careful of floating DevTools ---
       * that secretly has a hidden toolbar in location mode.*/
      display_mode_ == DisplayMode::kNormal) {
    webui_location_bar = std::make_unique<WebUILocationBar>(browser_, this);
  } else {
    location_bar_view = std::make_unique<LocationBarView>(
        browser_, browser_->profile(), browser_->command_controller(), this,
        display_mode_ != DisplayMode::kNormal);
  }

  // Make sure the toolbar shows by default.
  size_animation_.Reset(1);

  if (display_mode_ != DisplayMode::kNormal) {
    CHECK(location_bar_view)
        << "Alternate location bar impls need to handle this.";
    location_bar_view_ = AddChildView(std::move(location_bar_view));
    location_bar_ = location_bar_view_;
    location_bar_view_->Init();
  }

  if (display_mode_ == DisplayMode::kNormal) {
    SetBackground(std::make_unique<CustomCornersBackground>(
        *this, *browser_view_,
        /*primary_color=*/CustomCornersBackground::ToolbarTheme(),
        /*corner_color=*/CustomCornersBackground::FrameTheme()));
  } else if (display_mode_ == DisplayMode::kCustomTab) {
    custom_tab_bar_ =
        AddChildView(std::make_unique<CustomTabBarView>(browser_view_, this));
    SetLayoutManager(std::make_unique<views::FillLayout>());
    initialized_ = true;
    return;
  } else if (display_mode_ == DisplayMode::kLocation) {
    // Add the pinned toolbar actions container so that downloads can be shown
    // in popups.
    pinned_toolbar_actions_container_ = AddChildView(
        std::make_unique<PinnedToolbarActionsContainer>(browser_view_, this));
    pinned_toolbar_actions_ = pinned_toolbar_actions_container_;
    SetBackground(views::CreateSolidBackground(kColorLocationBarBackground));
    SetLayoutManager(std::make_unique<views::FlexLayout>())
        ->SetOrientation(views::LayoutOrientation::kHorizontal)
        .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
        .SetDefault(views::kFlexBehaviorKey,
                    views::FlexSpecification(
                        views::LayoutOrientation::kHorizontal,
                        views::MinimumFlexSizeRule::kPreferredSnapToZero))
        .SetFlexAllocationOrder(views::FlexAllocationOrder::kReverse);
    if (location_bar_view_) {
      location_bar_view_->SetProperty(
          views::kFlexBehaviorKey,
          views::FlexSpecification(views::LayoutOrientation::kHorizontal,
                                   views::MinimumFlexSizeRule::kScaleToZero,
                                   views::MaximumFlexSizeRule::kUnbounded));
    }
    initialized_ = true;
    return;
  }

  const auto callback = [](Browser* browser, int command,
                           const ui::Event& event) {
    chrome::ExecuteCommandWithDisposition(
        browser, command, ui::DispositionFromEventFlags(event.flags()));
  };

  PrefService* const prefs = browser_->profile()->GetPrefs();

  std::unique_ptr<ExtensionsToolbarDesktop> extensions_container;
  std::unique_ptr<ToolbarDivider> toolbar_divider;

  // Do not create the extensions or browser actions container if it is a guest
  // profile (only regular and incognito profiles host extensions).
  if (!browser_->profile()->IsGuestSession() &&
      !features::IsWebUIExtensionsContainerEnabled()) {
    extensions_container = std::make_unique<ExtensionsToolbarDesktop>(browser_);

    toolbar_divider = std::make_unique<ToolbarDivider>();
  }

  std::unique_ptr<MediaToolbarButtonView> media_button;
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  media_button = std::make_unique<MediaToolbarButtonView>(
      browser_view_,
      std::make_unique<MediaToolbarButtonContextualMenu>(browser_));
#endif

  // Always add children in order from left to right, for accessibility.
  if (!features::IsWebUIBackForwardButtonEnabled()) {
    back_ = AddChildView(std::make_unique<BackForwardButton>(
        BackForwardButton::Direction::kBack,
        base::BindRepeating(callback, browser_, IDC_BACK), browser_));
    forward_ = AddChildView(std::make_unique<BackForwardButton>(
        BackForwardButton::Direction::kForward,
        base::BindRepeating(callback, browser_, IDC_FORWARD), browser_));
  }

  if (base::FeatureList::IsEnabled(
          features::kWebUIToolbarProcessOverheadExperiment)) {
    detached_toolbar_webview_ = std::make_unique<WebUIToolbarWebView>(
        browser_, browser_->command_controller(), /*location_bar=*/nullptr);
  } else if (features::IsWebUIToolbarEnabled()) {
    toolbar_webview_ = AddChildView(std::make_unique<WebUIToolbarWebView>(
        browser_, browser_->command_controller(),
        std::move(webui_location_bar)));

    toolbar_webview_->SetProperty(views::kFlexBehaviorKey,
                                  toolbar_webview_->GetFlexSpecification());
  }

  if (!features::IsWebUIReloadButtonEnabled() ||
      base::FeatureList::IsEnabled(
          features::kWebUIToolbarProcessOverheadExperiment)) {
    reload_ = AddChildView(std::make_unique<ReloadButton>(
        browser_->profile(), browser_->command_controller(),
        InitialWebUIWindowMetricsManager::From(browser_)));
  }

  if (!features::IsWebUIHomeButtonEnabled()) {
    home_ = AddChildView(std::make_unique<HomeButton>(
        browser_, base::BindRepeating(callback, browser_, IDC_HOME)));
  }

  if (!features::IsWebUISplitTabsButtonEnabled()) {
    split_tabs_ =
        AddChildView(std::make_unique<SplitTabsToolbarButton>(browser_));
  }

  // Zephyrus: new tab (+) button in the toolbar's left group, since the tab
  // strip's new tab button is hidden.
  if (browser_->is_type_normal()) {
    auto new_tab_button = std::make_unique<ToolbarButton>(
        base::BindRepeating(callback, browser_, IDC_NEW_TAB));
    new_tab_button->SetVectorIcon(vector_icons::kAdd2Icon);
    const std::u16string new_tab_name =
        l10n_util::GetStringUTF16(IDS_TOOLTIP_NEW_TAB);
    new_tab_button->SetTooltipText(new_tab_name);
    new_tab_button->GetViewAccessibility().SetName(new_tab_name);
    zephyrus_new_tab_button_ = AddChildView(std::move(new_tab_button));
  }

  if (base::FeatureList::IsEnabled(contextual_tasks::kContextualTasks) &&
      contextual_tasks::kShowEntryPoint.Get() ==
          contextual_tasks::EntryPointOption::kToolbarEphemeralBranded) {
    auto button = std::make_unique<ContextualTasksButton>(browser_);
    auto* vts_controller =
        tabs::VerticalTabStripStateController::From(browser_);
    if (!vts_controller || !vts_controller->ShouldDisplayVerticalTabs()) {
      button->SetProperty(views::kMarginsKey, gfx::Insets());
    }
    AddChildViewAt(std::move(button), 0);
  }

  if (location_bar_view) {
    location_bar_view_ = AddChildView(std::move(location_bar_view));
    location_bar_ = location_bar_view_;
  } else {
    location_bar_ = toolbar_webview_->GetLocationBar();
  }

  bool is_glic_left_of_profile =
      features::kGlicToolbarButtonLocationParam.Get() ==
          features::GlicToolbarButtonLocation::kLeftOfProfileChip ||
      features::kGlicToolbarButtonLocationParam.Get() ==
          features::GlicToolbarButtonLocation::kLeftOfProfileChipWithBackground;
  if (glic::GlicEnabling::IsProfileEligible(browser_view_->GetProfile()) &&
      !is_glic_left_of_profile) {
    InitGlicContainer();

    glic_button_ = AddChildView(CreateGlicButton());
    std::unique_ptr<ToolbarDivider> glic_button_divider =
        std::make_unique<ToolbarDivider>();
    glic_button_divider_ = AddChildView(std::move(glic_button_divider));
    glic_button_divider_->SetProperty(
        views::kMarginsKey,
        gfx::Insets::VH(
            0, GetLayoutConstant(LayoutConstant::kToolbarDividerSpacing)));
  }

  if (extensions_container) {
    extensions_container_ = AddChildView(std::move(extensions_container));
    extensions_toolbar_coordinator_ =
        std::make_unique<ExtensionsToolbarCoordinator>(browser_,
                                                       extensions_container_);
  }

  if (toolbar_divider) {
    toolbar_divider_ = AddChildView(std::move(toolbar_divider));
  }

  if (!features::IsWebUIPinnedToolbarActionsEnabled()) {
    pinned_toolbar_actions_container_ = AddChildView(
        std::make_unique<PinnedToolbarActionsContainer>(browser_view_, this));
    pinned_toolbar_actions_ = pinned_toolbar_actions_container_;
  } else {
    pinned_toolbar_actions_ = toolbar_webview_->GetPinnedToolbarActions();
  }

  if (IsChromeLabsEnabled()) {
    UpdateChromeLabsNewBadgePrefs(browser_->profile());

    const bool should_show_chrome_labs_ui =
        ShouldShowChromeLabsUI(browser_->profile());
    if (should_show_chrome_labs_ui) {
      show_chrome_labs_button_.Init(
          chrome_labs_prefs::kBrowserLabsEnabledEnterprisePolicy, prefs,
          base::BindRepeating(&ToolbarView::OnChromeLabsPrefChanged,
                              base::Unretained(this)));
      CHECK(!features::IsWebUIPinnedToolbarActionsEnabled())
          << "WebUIPinnedToolbarActions does not support ChromeLabs.";
      // Set the visibility for the button based on initial enterprise policy
      // value. Only call OnChromeLabsPrefChanged if there is a change from
      // the initial value.
      pinned_toolbar_actions_container_->GetActionItemFor(kActionShowChromeLabs)
          ->SetVisible(show_chrome_labs_button_.GetValue() &&
                       should_show_chrome_labs_ui);
    }
  }

  // Only show the Battery Saver button when it is not controlled by the OS. On
  // ChromeOS the battery icon in the shelf shows the same information.
  if (!performance_manager::user_tuning::IsBatterySaverModeManagedByOS() &&
      !features::IsWebUIBatterySaverButtonEnabled()) {
    battery_saver_button_ =
        AddChildView(std::make_unique<BatterySaverButton>(browser_));
  }

  if (!features::IsWebUIPerformanceInterventionButtonEnabled()) {
    performance_intervention_button_ =
        AddChildView(std::make_unique<PerformanceInterventionButton>(browser_));
  }

  if (media_button) {
    media_button_ = AddChildView(std::move(media_button));
  }

  if (glic::GlicEnabling::IsProfileEligible(browser_view_->GetProfile())) {
    if (base::FeatureList::IsEnabled(features::kAiOverlayDialog) &&
        ttc::AiOverlayDialogController::From(browser_)) {
      actions::ActionItem* action_item =
          actions::ActionManager::Get().FindAction(
              kActionShowAiOverlayDialog, browser_->browser_window_features()
                                              ->browser_actions()
                                              ->root_action_item());
      if (action_item) {
        action_item->SetVisible(true);
        action_item->SetEnabled(true);
        PinnedToolbarActionsModel::Get(browser_->profile())
            ->UpdatePinnedState(kActionShowAiOverlayDialog, true);
      }
    }
  }

  if (is_glic_left_of_profile &&
      glic::GlicEnabling::IsProfileEligible(browser_view_->GetProfile())) {
    InitGlicContainer();

    glic_button_ = AddChildView(CreateGlicButton());
    // The left margin is needed to ensure proper spacing before the
    // separator. The right margin is needed for spacing between the glic and
    // actor icons. The space between glic and profile should also be 5 but that
    // is handled by the profile margins.
    glic_button_->SetProperty(views::kMarginsKey,
                              gfx::Insets()
                                  .set_left(kGlicButtonMargin)
                                  .set_right(kInsideBorderAroundGlicButtons));
    UpdateGlicButtonVisibility();
  }

  if (!features::IsWebUIAvatarButtonEnabled()) {
    avatar_ =
        AddChildView(std::make_unique<AvatarToolbarButton>(browser_view_));
    // Zephyrus: hide Google account button
    avatar_->SetVisible(false);
  }

  overflow_button_ = AddChildView(std::make_unique<OverflowButton>());
  overflow_button_->SetVisible(false);

  // WebUI app menu button handles these internally, so no need to set these
  // properties here, and the control is added as part of the WebUI toolbar.
  if (!features::IsWebUIAppMenuButtonEnabled()) {
    auto app_menu_button = std::make_unique<BrowserAppMenuButton>(this);
    app_menu_button->SetFlipCanvasOnPaintForRTLUI(true);
    app_menu_button->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_ACCNAME_APP));
    app_menu_button->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_APPMENU_TOOLTIP));
    app_menu_button->SetID(VIEW_ID_APP_MENU);
    app_menu_button_ = AddChildView(std::move(app_menu_button));
  }

  if (base::FeatureList::IsEnabled(contextual_tasks::kContextualTasks) &&
      contextual_tasks::GetExpandButtonOption() ==
          contextual_tasks::ExpandButtonOption::kToolbarCloseButton) {
    AddChildView(std::make_unique<ContextualTasksCloseTabButton>(browser_));
  }

  LoadImages();

  // Set the button icon based on the system state. Do this after
  // |app_menu_button_| has been added as a bubble may be shown that needs
  // the widget (widget found by way of app_menu_button_->GetWidget()).
  app_menu_icon_controller_.UpdateDelegate();

  if (location_bar_view_) {
    location_bar_view_->Init();
  } else {
    toolbar_webview_->GetLocationBar()->Init(toolbar_webview_.get());
  }

  show_forward_button_.Init(
      prefs::kShowForwardButton, prefs,
      base::BindRepeating(&ToolbarView::OnShowForwardButtonChanged,
                          base::Unretained(this)));

  SetForwardButtonVisibility(show_forward_button_.GetValue());

  show_home_button_.Init(
      prefs::kShowHomeButton, prefs,
      base::BindRepeating(&ToolbarView::OnShowHomeButtonChanged,
                          base::Unretained(this)));

  if (home_) {
    home_->SetVisible(show_home_button_.GetValue());
  }

  if (glic::GlicEnabling::IsProfileEligible(browser_view_->GetProfile())) {
    auto* vertical_tab_strip_state_controller =
        tabs::VerticalTabStripStateController::From(browser_view_->browser());
    if (vertical_tab_strip_state_controller) {
      vertical_tab_subscription_ =
          vertical_tab_strip_state_controller->RegisterOnModeChanged(
              base::BindRepeating(&ToolbarView::OnVerticalTabStripModeChanged,
                                  base::Unretained(this)));
      should_display_vertical_tabs_ =
          vertical_tab_strip_state_controller->ShouldDisplayVerticalTabs();
    }
    UpdateGlicButtonVisibility();
  }

  // Zephyrus: add window controls at the far right of the toolbar, which
  // doubles as the title bar for normal tabbed browser windows.
  if (browser_->is_type_normal()) {
    AddZephyrusWindowControls();
    AddZephyrusWorkspaceButton();
    AddZephyrusAdblockButton();
  }

  InitLayout();

  // Zephyrus: the pin toggle sits just right of the (now centered) omnibox, so
  // it must be inserted after InitLayout() has created the flanking spacers.
  if (browser_->is_type_normal() && location_bar_view_) {
    AddZephyrusPinButton();
  }

  for (auto* button : std::array<views::Button*, 5>{back_, forward_, reload_,
                                                    home_, avatar_}) {
    if (button) {
      button->set_tag(GetViewCommandMap().at(button->GetID()));
    }
  }

  initialized_ = true;
}

void ToolbarView::InitGlicContainer() {
  if (base::FeatureList::IsEnabled(features::kGlicActorUi) &&
      features::kGlicActorUiTaskIcon.Get()) {
    glic_actor_button_container_ =
        AddChildView(CreateGlicActorButtonContainer());
    glic_actor_task_icon_ =
        glic_actor_button_container_->AddChildView(CreateGlicActorTaskIcon());
    glic_actor_button_container_->SetVisible(false);
    glic_actor_task_icon_->SetVisible(false);
  }
}

void ToolbarView::OnVerticalTabStripModeChanged(
    tabs::VerticalTabStripStateController* controller) {
  should_display_vertical_tabs_ = controller->ShouldDisplayVerticalTabs();
  UpdateGlicButtonVisibility();
  UpdateGlicActorVisibility();
}

std::unique_ptr<GlicAndActorButtonsContainer>
ToolbarView::CreateGlicActorButtonContainer() {
  auto glic_actor_button_container =
      std::make_unique<GlicAndActorButtonsContainer>();

  // Should be hidden until a task starts.
  glic_actor_button_container->SetVisible(false);

  return glic_actor_button_container;
}

std::unique_ptr<glic::ToolbarGlicActorTaskIcon>
ToolbarView::CreateGlicActorTaskIcon() {
  std::unique_ptr<glic::ToolbarGlicActorTaskIcon> glic_actor_task_icon =
      std::make_unique<glic::ToolbarGlicActorTaskIcon>(
          browser_view_->browser(),
          base::BindRepeating(&ToolbarView::OnGlicActorTaskIconClicked,
                              base::Unretained(this)));

  // Add a MenuButtonController in order to keep the task icon pressed while the
  // bubble is visible.
  glic_actor_task_icon->SetButtonController(
      std::make_unique<views::MenuButtonController>(
          glic_actor_task_icon.get(),
          base::BindRepeating(&ToolbarView::OnGlicActorTaskIconClicked,
                              base::Unretained(this)),
          std::make_unique<views::Button::DefaultButtonControllerDelegate>(
              glic_actor_task_icon.get())));

  glic_actor_task_icon->SetProperty(views::kCrossAxisAlignmentKey,
                                    views::LayoutAlignment::kCenter);

  if (base::FeatureList::IsEnabled(features::kToolbarGlicButtonResizing)) {
    glic_actor_task_icon->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(
            views::MinimumFlexSizeRule::kPreferredSnapToMinimum,
            views::MaximumFlexSizeRule::kPreferred));
  }

  return glic_actor_task_icon;
}

void ToolbarView::OnGlicActorTaskIconClicked() {
  Profile* const profile = browser_view_->GetProfile();
  auto* icon_manager =
      glic::GlicActorTaskIconManagerFactory::GetForProfile(profile);
  CHECK(icon_manager);

  ActorTaskListBubbleController* controller =
      ActorTaskListBubbleController::From(browser_view_->browser());
  // Only show the bubble if the button is not currently pressed. Clicking on
  // the pressed button should dismiss the nudge.
  if (!glic_actor_task_icon_->GetIsPressed()) {
    controller->ShowBubble(glic_actor_task_icon_);
  }

  auto current_task_nudge_state = icon_manager->GetCurrentActorTaskNudgeState();
  actor::ui::LogGlobalTaskIndicatorClick(current_task_nudge_state);
}

std::unique_ptr<glic::ToolbarGlicButton> ToolbarView::CreateGlicButton() {
  glic::GlicKeyedService* service =
      glic::GlicKeyedService::Get(browser_view_->GetProfile());
  std::u16string tooltip_text = l10n_util::GetStringUTF16(
      service->instance_coordinator().IsAnyPanelShowing()
          ? IDS_GLIC_TAB_STRIP_BUTTON_TOOLTIP_CLOSE
          : IDS_GLIC_TAB_STRIP_BUTTON_TOOLTIP);
  std::unique_ptr<glic::ToolbarGlicButton> glic_button =
      std::make_unique<glic::ToolbarGlicButton>(
          browser_view_->browser(),
          base::BindRepeating(&ToolbarView::OnGlicButtonAnimationEnded,
                              base::Unretained(this)),
          tooltip_text,
          base::BindRepeating(&ToolbarView::OnGlicButtonClicked,
                              base::Unretained(this)));

  glic_button->SetProperty(views::kCrossAxisAlignmentKey,
                           views::LayoutAlignment::kCenter);

  return glic_button;
}

void ToolbarView::OnGlicButtonClicked() {
  // Indicate that the glic button was pressed so that we can either close the
  // IPH promo (if present) or note that it has already been used to prevent
  // unnecessarily displaying the promo.
  BrowserUserEducationInterface::From(browser_)->NotifyFeaturePromoFeatureUsed(
      feature_engagement::kIPHGlicPromoFeature,
      FeaturePromoFeatureUsedAction::kClosePromoIfPresent);

  std::optional<std::string> prompt_suggestion;
  glic::GlicNudgeController* glic_nudge_controller =
      browser_->browser_window_features()->glic_nudge_controller();
  if (glic_nudge_controller) {
    prompt_suggestion = glic_nudge_controller->GetPromptSuggestion();
    glic_nudge_controller->ClearPromptSuggestion();
  }

  glic::mojom::InvocationSource source;
  if (button_controller_) {
    source = button_controller_->GetInvocationSource(
        glic_button_->GetIsShowingNudge(), /*is_toolbar=*/true);
  } else {
    source = glic_button_->GetIsShowingNudge()
                 ? glic::mojom::InvocationSource::kNudge
                 : glic::mojom::InvocationSource::kToolbarButton;
  }

  auto* glic_service = glic::GlicKeyedServiceFactory::GetGlicKeyedService(
      browser_view_->GetProfile());
  const bool is_panel_showing =
      glic_service->IsPanelShowingForBrowser(*browser_view_->browser());
  if (!is_panel_showing && prompt_suggestion.has_value() &&
      !prompt_suggestion->empty()) {
    glic::GlicInvokeOptions options(glic::Target(browser()), source);
    options.prompts.push_back(std::move(*prompt_suggestion));
    glic_service->Invoke(std::move(options));
  } else {
    glic_service->ToggleUI(browser_view_->browser(),
                           /*prevent_close=*/false, source);
  }

  if (glic_button_->GetIsShowingNudge()) {
    glic_nudge_controller->OnNudgeActivity(
        glic::GlicNudgeActivity::kNudgeClicked);
  }

  ExecuteHideToolbarNudge(glic_button_);
  // Reset state manually since there wont be a mouse up event as the
  // animation moves the button out of the way.
  glic_button_->SetState(views::Button::ButtonState::STATE_NORMAL);
}

void ToolbarView::OnGlicButtonDismissed() {
  browser_->browser_window_features()->glic_nudge_controller()->OnNudgeActivity(
      glic::GlicNudgeActivity::kNudgeDismissed);

  // Force hide the button when pressed, bypassing locked expansion mode.
  ExecuteHideToolbarNudge(glic_button_);
}

void ToolbarView::OnGlicButtonAnimationEnded() {
  // TODO(crbug.com/484389669): ToolbarGlicButton animations
  return;
}

void ToolbarView::ShowToolbarNudge(glic::GlicButtonInterface* button) {
  if (IsMouseHovered()) {
    SetLockedExpansionMode(ExpansionMode::kWillShow, button);
    return;
  }
  if (locked_expansion_mode_ == ExpansionMode::kNone) {
    ExecuteShowToolbarNudge(button);
  }
}

void ToolbarView::HideToolbarNudge(glic::GlicButtonInterface* button) {
  if (this->IsMouseHovered()) {
    SetLockedExpansionMode(ExpansionMode::kWillHide, button);
    return;
  }
  if (locked_expansion_mode_ == ExpansionMode::kNone) {
    ExecuteHideToolbarNudge(button);
  }
}

bool ToolbarView::GetIsShowingGlicNudge() {
  return glic_button_ && glic_button_->GetIsShowingNudge();
}

bool ToolbarView::GetIsShowingGlicActorTaskIconNudge() {
  return glic_actor_task_icon_ && glic_actor_task_icon_->GetIsShowingNudge();
}

void ToolbarView::OnTriggerGlicNudgeUI(glic::NudgeParams params) {
  if (GetIsShowingGlicActorTaskIconNudge()) {
    return;
  }

  CHECK(glic_button_);
  if (!params.label.empty()) {
    glic_button_->SetNudgeLabel(std::move(params.label));
    ShowToolbarNudge(glic_button_);
  }
}

void ToolbarView::OnHideGlicNudgeUI() {
  if (glic_button_) {
    HideToolbarNudge(glic_button_);
  }
}

void ToolbarView::SetGlicActorNudgeLabel(const std::u16string& nudge_label) {
  glic_actor_task_icon()->ShowNudgeLabel(nudge_label);
}

void ToolbarView::TriggerGlicActorNudge(const std::u16string& nudge_text) {
  CHECK(glic_actor_task_icon_);
  if (GetIsShowingGlicNudge()) {
    // If the glic button is showing, start the hide animation in parallel to
    // the show actor nudge animation.
    HideToolbarNudge(glic_button_);
    OnGlicButtonAnimationEnded();
  }
  ShowGlicActorNudge(nudge_text);
}

bool ToolbarView::IsGlicAdded() {
  return glic_button_ && glic_actor_task_icon_;
}

void ToolbarView::ShowGlicActorNudge(const std::u16string nudge_text) {
  CHECK(glic_actor_task_icon_);
  // Start animation for minimizing the glic button.
  glic_button_->Collapse();
  ShowGlicActorTaskIcon();
  glic_actor_task_icon_->ShowNudgeLabel(nudge_text);
  ShowToolbarNudge(glic_actor_task_icon_);
}

void ToolbarView::ShowGlicActorTaskIcon() {
  CHECK(glic_actor_button_container_);
  CHECK(glic_button_);
  // If the nudge is showing (ex: previous state was CheckTasks), hide the
  // nudge.
  if (glic_actor_task_icon_->GetIsShowingNudge()) {
    HideToolbarNudge(glic_actor_task_icon_);
    return;
  }
  glic_button_ =
      glic_actor_button_container_->InsertGlicButton(glic_button_.get());
  SetGlicActorShowState(true);
  SetGlicShowState(true);
  glic_button_->Collapse();
  glic_button_->SetSplitButtonCornerStyling();
  UpdateGlicActorButtonContainerBorders();

  if (glic_actor_task_icon_->GetAnimationMode() ==
      glic::AnimationMode::kEntry) {
    // TODO(crbug.com/484389669): Create animation session to being animation of
    // nudge.
    glic_actor_task_icon_->SetAnimationMode(glic::AnimationMode::kNudge);
    glic_actor_task_icon_->SetWidthFactor(1.0);
  }
}

void ToolbarView::HideGlicActorTaskIcon() {
  CHECK(glic_actor_task_icon_);

  // If it's already hidden, do nothing.
  if (!glic_actor_task_icon_->GetVisible() &&
      !glic_actor_task_icon_->GetIsShowingNudge()) {
    return;
  }
  glic_actor_task_icon_->SetIsShowingNudge(false);

  // TODO(crbug.com/484389669): Toolbar glic actor animations
  if (glic_actor_task_icon_->GetAnimationMode() ==
      glic::AnimationMode::kNudge) {
    // TODO(crbug.com/484389669): Create animation session to being animation of
    // nudge.
    glic_actor_task_icon_->SetAnimationMode(glic::AnimationMode::kEntry);
    glic_actor_task_icon_->SetWidthFactor(0.0);
  }

  FinalizeHideGlicActorTaskIcon();
}

void ToolbarView::SetGlicActorNudgePressedState(bool pressed) {
  glic_actor_task_icon()->SetPressedState(pressed);
}

void ToolbarView::ShowActorTaskListBubble() {
  ActorTaskListBubbleController::From(browser_)->ShowBubble(
      glic_actor_task_icon());
}

void ToolbarView::FinalizeHideGlicActorTaskIcon() {
  CHECK(glic_actor_button_container_);
  CHECK(glic_button_);
  // Reset Nudge State
  if (glic_actor_task_icon_->GetIsShowingNudge()) {
    // TODO(crbug.com/484389669): Glic actor nudge animation
    glic_actor_task_icon_->SetIsShowingNudge(false);
  }
  glic_actor_task_icon_->SetVisible(false);
  glic_actor_task_icon_->SetTaskIconToDefault();

  size_t insertion_index = GetIndexOf(avatar_.get()).value();
  if (glic_button_divider_) {
    insertion_index = GetIndexOf(glic_button_divider_).value();
  }
  glic_button_ = AddChildViewAt(std::move(glic_button_.get()), insertion_index);
  glic_actor_button_container_->SetVisible(false);
  glic_button_->Expand();
  glic_button_->ResetSplitButtonCornerStyling();
  // Reset the animation mode for the next time the icon is shown.
  glic_actor_task_icon_->SetAnimationMode(glic::AnimationMode::kEntry);
  UpdateGlicActorButtonContainerBorders();
}

void ToolbarView::UpdateGlicActorButtonContainerBorders() {
  CHECK(glic_button_);
  gfx::Insets glic_border;

  // Ensure buttons look vertically centered by making the top and bottom insets
  // match.
  gfx::Insets border_insets = gfx::Insets();
  int min_vertical_inset =
      std::min(border_insets.top(), border_insets.bottom());
  border_insets.set_top_bottom(min_vertical_inset, min_vertical_inset);

  // GlicActorTaskIcon will only ever be shown alongside the GlicButton.
  if (glic_actor_task_icon_ && glic_actor_task_icon_->IsDrawn()) {
    gfx::Insets task_icon_border;
    const gfx::Insets right_icon_border =
        gfx::Insets().set_left_right(0, kOutsideBorderAroundGlicButtons);
    const gfx::Insets left_icon_border = gfx::Insets().set_left_right(
        kOutsideBorderAroundGlicButtons, kInsideBorderAroundGlicButtons);
    task_icon_border = right_icon_border + border_insets;
    glic_border = left_icon_border + border_insets;
    glic_actor_task_icon_->SetBorder(
        views::CreateEmptyBorder(task_icon_border));
    // Force a background repaint to account for the new border insets.
    glic_actor_task_icon_->RefreshBackground();
  } else {
    // Reset GlicButton border if Task Icon is hidden.
    glic_border = gfx::Insets().set_left_right(border_insets.top(),
                                               border_insets.bottom()) +
                  border_insets;
  }
  glic_button_->SetBorder(views::CreateEmptyBorder(glic_border));
  // Force a background repaint to account for the new border insets.
  glic_button_->RefreshBackground();
}

void ToolbarView::ExecuteShowToolbarNudge(glic::GlicButtonInterface* button) {
  // TODO(crbug.com/): Fix cases where we can't show modal ui during animation
  // session.
  button->SetIsShowingNudge(true);

  // Only change the margins between the GlicButton and nudges that are NOT
  // coming from the GlicActorTaskIcon.
  if (glic_button_ && glic_button_->GetVisible() && button != glic_button_ &&
      button != glic_actor_task_icon_) {
    const int space_between_buttons = kLargeSpaceBetweenButtons;
    gfx::Insets margin;
    margin.set_right(space_between_buttons);
    button->GetPropertyHandler()->SetProperty(views::kMarginsKey, margin);
  } else {
    // Reset the margins.
    button->GetPropertyHandler()->SetProperty(views::kMarginsKey,
                                              gfx::Insets());
  }
}

void ToolbarView::ExecuteHideToolbarNudge(glic::GlicButtonInterface* button) {
  if (!button->GetVisible()) {
    return;
  }

  // Since the glic button is still visible in it's hidden state we need to have
  // a special case to query if it's in its Hide state.
  if (button == glic_button_ && button->GetWidthFactor() == 0.0) {
    return;
  }

  button->SetIsShowingNudge(false);
}

void ToolbarView::UpdateGlicActorVisibility() {
  if (!glic_actor_task_icon_) {
    return;
  }

  bool is_glic_actor_visible =
      should_show_glic_actor_ &&
      (should_display_vertical_tabs_ ||
       base::FeatureList::IsEnabled(features::kGlicHorizontalTabToolbarButton));

  glic_actor_task_icon_->SetVisible(is_glic_actor_visible);
  if (glic_button_) {
    bool is_glic_left_of_profile =
        base::FeatureList::IsEnabled(features::kGlicToolbarButtonLocation) &&
        features::kGlicToolbarButtonLocationParam.Get() ==
            features::GlicToolbarButtonLocation::kLeftOfProfileChip;
    glic_button_->UpdateStyle(is_glic_left_of_profile &&
                              !is_glic_actor_visible);
  }
}

void ToolbarView::UpdateGlicButtonVisibility() {
  if (!glic_button_) {
    return;
  }

  bool is_glic_visible =
      should_show_glic_button_ &&
      (should_display_vertical_tabs_ ||
       base::FeatureList::IsEnabled(features::kGlicHorizontalTabToolbarButton));

  glic_button_->SetVisible(is_glic_visible);
  if (glic_button_divider_) {
    glic_button_divider_->SetVisible(is_glic_visible);
  }

  if (glic_actor_button_container_) {
    // glic_actor_button_container_ should only be visible at the same time as
    // glic_button_.
    glic_actor_button_container_->SetVisible(is_glic_visible);
  }
  bool is_glic_left_of_profile =
      base::FeatureList::IsEnabled(features::kGlicToolbarButtonLocation) &&
      features::kGlicToolbarButtonLocationParam.Get() ==
          features::GlicToolbarButtonLocation::kLeftOfProfileChip;
  bool is_task_icon_visible =
      glic_actor_task_icon_ && glic_actor_task_icon_->GetVisible();
  glic_button_->UpdateStyle(is_glic_left_of_profile && !is_task_icon_visible);
}

void ToolbarView::SetGlicActorShowState(bool show) {
  should_show_glic_actor_ = show;
  UpdateGlicActorVisibility();
}

void ToolbarView::SetButtonController(glic::GlicButtonController* controller) {
  button_controller_ = controller;
}

void ToolbarView::SetGlicShowState(bool show) {
  should_show_glic_button_ = show;
  UpdateGlicButtonVisibility();
}

void ToolbarView::SetGlicPanelIsOpen(bool open) {
  if (!glic_button_) {
    return;
  }

  glic_button_->SetGlicPanelIsOpen(open);
}

void ToolbarView::MouseMovedOutOfHost() {
  SetLockedExpansionMode(ExpansionMode::kNone, /*button=*/nullptr);
}

void ToolbarView::SetLockedExpansionMode(ExpansionMode mode,
                                         glic::GlicButtonInterface* button) {
  if (mode == ExpansionMode::kNone) {
    if (locked_expansion_mode_ == ExpansionMode::kWillShow) {
      ExecuteShowToolbarNudge(locked_expansion_button_);
    } else if (locked_expansion_mode_ == ExpansionMode::kWillHide) {
      ExecuteHideToolbarNudge(locked_expansion_button_);
    }
    locked_expansion_button_ = nullptr;
  } else {
    locked_expansion_button_ = button;
    mouse_watcher_->Start(GetWidget()->GetNativeWindow());
  }
  locked_expansion_mode_ = mode;
}

void ToolbarView::AnimationEnded(const gfx::Animation* animation) {
  if (animation->GetCurrentValue() == 0) {
    SetToolbarVisibility(false);
  }
  BrowserWindow::FromBrowser(browser())->ToolbarSizeChanged(
      /*is_animating=*/false);
}

void ToolbarView::AnimationProgressed(const gfx::Animation* animation) {
  BrowserWindow::FromBrowser(browser())->ToolbarSizeChanged(
      /*is_animating=*/true);
}

void ToolbarView::Update(WebContents* tab) {
  if (location_bar_) {
    location_bar_->Update(tab);
  }

  if (extensions_container_) {
    extensions_container_->UpdateAllIcons();
  }

  if (pinned_toolbar_actions_container_) {
    pinned_toolbar_actions_container_->UpdateAllIcons();
  }

  if (ReloadControl* reload_control = GetReloadButton(); reload_control) {
    reload_control->SetDevToolsStatus(
        chrome::IsDebuggerAttachedToCurrentTab(browser_));
  }
}

bool ToolbarView::UpdateSecurityState() {
  if (location_bar_ && location_bar_->HasSecurityStateChanged()) {
    Update(nullptr);
    return true;
  }

  return false;
}

void ToolbarView::SetToolbarVisibility(bool visible) {
  SetVisible(visible);
  views::View* bar = display_mode_ == DisplayMode::kCustomTab
                         ? static_cast<views::View*>(custom_tab_bar_)
                         : static_cast<views::View*>(location_bar_view_);
  CHECK(bar) << "Alternate location bar impls need to handle this.";
  bar->SetVisible(visible);
}

void ToolbarView::UpdateCustomTabBarVisibility(bool visible, bool animate) {
  DCHECK_EQ(display_mode_, DisplayMode::kCustomTab);

  if (!animate) {
    size_animation_.Reset(visible ? 1.0 : 0.0);
    SetToolbarVisibility(visible);
    BrowserWindow::FromBrowser(browser())->ToolbarSizeChanged(
        /*is_animating=*/false);
    return;
  }

  if (visible) {
    SetToolbarVisibility(true);
    size_animation_.Show();
  } else {
    size_animation_.Hide();
  }
}

void ToolbarView::ResetTabState(WebContents* tab) {
  if (location_bar_) {
    location_bar_->ResetTabState(tab);
  }
}

void ToolbarView::SetPaneFocusAndFocusAppMenu() {
  AppMenuControl* app_menu_control = GetAppMenuControl();
  if (app_menu_control) {
    app_menu_control->Focus(GetAsAccessiblePaneView());
  }
}

bool ToolbarView::GetAppMenuFocused() const {
  const AppMenuControl* app_menu_control = GetAppMenuControl();
  return app_menu_control && app_menu_control->HasFocus();
}

void ToolbarView::ShowIntentPickerBubble(
    std::vector<IntentPickerBubbleView::AppInfo> app_info,
    bool show_stay_in_chrome,
    bool show_remember_selection,
    IntentPickerBubbleView::BubbleType bubble_type,
    const std::optional<url::Origin>& initiating_origin,
    IntentPickerResponse callback) {
  std::optional<ui::ElementIdentifier> higlighted_element;
  if (bubble_type != IntentPickerBubbleView::BubbleType::kClickToCall) {
    if (GetIntentChipButton()) {
      higlighted_element = kIntentChipElementId;
    } else if (GetPageActionViewInterface(kActionShowIntentPicker)) {
      higlighted_element = kIntentPickerPageActionElementId;
    } else {
      return;
    }
  }

  // At this point, we either have a highlighted_element or it's a ClickToCall
  // bubble which doesn't have a corresponding page action button to highlight.
  IntentPickerBubbleView::ShowBubble(
      GetBubbleAnchor(std::nullopt), higlighted_element, bubble_type,
      GetWebContents(), std::move(app_info), show_stay_in_chrome,
      show_remember_selection, initiating_origin, std::move(callback));
}

void ToolbarView::ShowBookmarkBubble(const GURL& url, bool already_bookmarked) {
  page_actions::PageActionViewInterface* bookmark_star_icon = nullptr;
  if (!features::IsWebUILocationBarEnabled()) {
    bookmark_star_icon = GetPageActionViewInterface(kActionBookmarkThisTab);
    CHECK(bookmark_star_icon);
  }
  BookmarkBubbleView::ShowBubble(GetBubbleAnchor(std::nullopt),
                                 GetWebContents(), bookmark_star_icon, browser_,
                                 url, already_bookmarked);
}

bool ToolbarView::IsPositionInWindowCaption(
    const gfx::Point& test_point) const {
  return IsPositionInWindowCaptionForView(this, test_point);
}

views::Button* ToolbarView::GetChromeLabsButton() const {
  return ChromeLabsCoordinator::From(browser_)->GetChromeLabsButton();
}

ExtensionsToolbarButton* ToolbarView::GetExtensionsButton() const {
  return extensions_container_->GetExtensionsButton();
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, LocationBarView::Delegate implementation:

WebContents* ToolbarView::GetWebContents() {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

LocationBarModel* ToolbarView::GetLocationBarModel() {
  return browser_->GetFeatures().location_bar_model();
}

const LocationBarModel* ToolbarView::GetLocationBarModel() const {
  return browser_->GetFeatures().location_bar_model();
}

ContentSettingBubbleModelDelegate*
ToolbarView::GetContentSettingBubbleModelDelegate() {
  return browser_->GetFeatures().content_setting_bubble_model_delegate();
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, CommandObserver implementation:

void ToolbarView::EnabledStateChangedForCommand(int id, bool enabled) {
  DCHECK(display_mode_ == DisplayMode::kNormal);

  if ((id == IDC_BACK || id == IDC_FORWARD) &&
      features::IsWebUIBackForwardButtonEnabled()) {
    toolbar_webview_->SetBackForwardEnabled(id, enabled);
    return;
  }

  const std::array<views::Button*, 5> kButtons{back_, forward_, reload_, home_,
                                               avatar_};
  auto it = std::ranges::find_if(
      kButtons, [id](views::Button* b) { return b && b->tag() == id; });
  if (it != kButtons.end()) {
    (*it)->SetEnabled(enabled);
  }
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, ui::AcceleratorProvider implementation:

bool ToolbarView::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  return GetWidget()->GetAccelerator(command_id, accelerator);
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, views::View overrides:

gfx::Size ToolbarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  gfx::Size size;
  switch (display_mode_) {
    case DisplayMode::kCustomTab:
      size = custom_tab_bar_->GetPreferredSize();
      break;
    case DisplayMode::kLocation:
      size = location_bar_->PreferredSize();
      break;
    case DisplayMode::kNormal:
      size = AccessiblePaneView::CalculatePreferredSize(available_size);
      // Because there are odd cases where something causes one of the views in
      // the toolbar to report an unreasonable height (see crbug.com/41471763),
      // we cap the height at the size of known child views (location bar and
      // back button) plus margins.
      // TODO(crbug.com/40663413): Figure out why the height reports incorrectly
      // on some installations.
      if (layout_manager_ && location_bar_->IsVisible()) {
        const int max_height = std::max(location_bar_->PreferredSize().height(),
                                        GetBackForwardButtonSize().height()) +
                               layout_manager_->interior_margin().height();
        size.SetToMin({size.width(), max_height});
      }
  }
  size.set_height(size.height() * size_animation_.GetCurrentValue());
  return size;
}

gfx::Size ToolbarView::GetMinimumSize() const {
  gfx::Size size;
  switch (display_mode_) {
    case DisplayMode::kCustomTab:
      size = custom_tab_bar_->GetMinimumSize();
      break;
    case DisplayMode::kLocation:
      size = location_bar_->MinimumSize();
      break;
    case DisplayMode::kNormal:
      size = AccessiblePaneView::GetMinimumSize();
      // Because there are odd cases where something causes one of the views in
      // the toolbar to report an unreasonable height (see crbug.com/41471763),
      // we cap the height at the size of known child views (location bar and
      // back button) plus margins.
      // TODO(crbug.com/40663413): Figure out why the height reports incorrectly
      // on some installations.
      if (layout_manager_ && location_bar_->IsVisible()) {
        const int max_height =
            std::max(location_bar_->MinimumSize().height(),
                     GetBackForwardButtonSize(/*minimum_size=*/true).height()) +
            layout_manager_->interior_margin().height();
        size.SetToMin({size.width(), max_height});
      }
      // Overflow button must be part of minimum size calculation.
      if (browser_->is_type_normal() && !overflow_button_->GetVisible()) {
        const int default_margin =
            GetLayoutConstant(LayoutConstant::kToolbarIconDefaultMargin);
        size.Enlarge(
            default_margin + overflow_button_->GetMinimumSize().width(), 0);
      }
  }
  size.set_height(size.height() * size_animation_.GetCurrentValue());
  return size;
}

void ToolbarView::Layout(PassKey) {
  // If we have not been initialized yet just do nothing.
  if (!initialized_) {
    return;
  }

  if (display_mode_ == DisplayMode::kCustomTab) {
    custom_tab_bar_->SetBounds(0, 0, width(),
                               custom_tab_bar_->GetPreferredSize().height());
    CHECK(location_bar_view_)
        << "Alternate location bar impls need to handle this.";
    location_bar_view_->SetVisible(false);
    return;
  }

  if (display_mode_ == DisplayMode::kNormal) {
    LayoutCommon();
  }

  if (toolbar_controller_) {
    // Need to determine whether the overflow button should be visible, and only
    // update it if the visibility changes.
    const bool was_overflow_button_visible =
        toolbar_controller_->overflow_button()->GetVisible();
    const bool show_overflow_button =
        toolbar_controller_->ShouldShowOverflowButton(size());
    if (was_overflow_button_visible != show_overflow_button) {
      views::ManualLayoutUtil(layout_manager_)
          .SetViewHidden(toolbar_controller_->overflow_button(),
                         !show_overflow_button);
      base::RecordAction(base::UserMetricsAction(
          show_overflow_button ? "ResponsiveToolbar.OverflowButtonShown"
                               : "ResponsiveToolbar.OverflowButtonHidden"));
    }
  }

  // Call super implementation to ensure layout manager and child layouts
  // happen.
  LayoutSuperclass<AccessiblePaneView>(this);

  // Zephyrus: pin the window controls to the exact top-right corner, flush
  // and full title-bar height like native Win11 caption buttons (the floating
  // glass-pill variant was tried and reverted on user feedback).
  if (zephyrus_close_button_ && zephyrus_close_button_->GetVisible()) {
    constexpr int kCaptionButtonWidth = 46;
    int right = width();
    for (views::Button* button :
         {zephyrus_close_button_.get(), zephyrus_maximize_button_.get(),
          zephyrus_minimize_button_.get()}) {
      if (button && button->GetVisible()) {
        button->SetBounds(right - kCaptionButtonWidth, 0, kCaptionButtonWidth,
                          height());
        right -= kCaptionButtonWidth;
      }
    }
  }
  if (zephyrus_controls_backdrop_) {
    zephyrus_controls_backdrop_->SetVisible(false);
  }

  // Zephyrus: no container behind the navigation arrows. Every title bar glyph
  // sits bare on the flat theme, so the shared glass pill that used to group
  // back/forward stays hidden (as does the window-controls backdrop above).
  if (zephyrus_nav_pill_backdrop_) {
    zephyrus_nav_pill_backdrop_->SetVisible(false);
  }

  // Zephyrus: Safari-style compact address pill. Stock Chromium stretches the
  // location bar across every free pixel — the single strongest "this is
  // Chrome" tell. Instead, cap it at a comfortable reading width and center
  // it in the window, letting the (chameleon-tinted) title bar breathe on
  // both sides. The FlexLayout pass above has already reserved the full slot;
  // this shrinks the bar within that slot, so neighbors are never overlapped.
  if (location_bar_view_ && location_bar_view_->GetVisible() &&
      display_mode_ == DisplayMode::kNormal) {
    const gfx::Rect slot = location_bar_view_->bounds();
    // A small Safari-like domain pill that grows smoothly to a comfortable
    // editing width while the omnibox is focused (animated by
    // ZephyrusOmniboxFocusAnimation; 0 = steady, 1 = editing).
    // Figma spec: Window/Search is 552 wide on a 1920 screen (~29%).
    const double focus = zephyrus_omnibox_focus_animation_
                             ? zephyrus_omnibox_focus_animation_->value()
                             : 0.0;
    const int steady_width = std::min(552, width() * 29 / 100);
    const int editing_width = std::min(680, width() * 46 / 100);
    const int max_width =
        gfx::Tween::IntValueBetween(focus, steady_width, editing_width);
    if (slot.width() > max_width && max_width > 0) {
      // Prefer true window-centering (like Safari); fall back to centering
      // inside the slot when neighbors crowd the middle.
      int x = (width() - max_width) / 2;
      x = std::clamp(x, slot.x(), slot.right() - max_width);
      location_bar_view_->SetBounds(x, slot.y(), max_width, slot.height());
    }
  }
}

void ToolbarView::OnThemeChanged() {
  views::AccessiblePaneView::OnThemeChanged();
  if (!initialized_) {
    return;
  }

  if (display_mode_ == DisplayMode::kNormal) {
    LoadImages();
  }

  SchedulePaint();
}

bool ToolbarView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  const views::View* focused_view = focus_manager()->GetFocusedView();
  if (focused_view && (focused_view->GetID() == VIEW_ID_OMNIBOX)) {
    return false;  // Let the omnibox handle all accelerator events.
  }
  return AccessiblePaneView::AcceleratorPressed(accelerator);
}

void ToolbarView::ChildPreferredSizeChanged(views::View* child) {
  InvalidateLayout();
  if (size() != GetPreferredSize()) {
    PreferredSizeChanged();
  }
}

void ToolbarView::ChildVisibilityChanged(views::View* child) {
  if (child == home_) {
    if (!home_->GetVisible() && show_home_button_.GetValue()) {
      base::UmaHistogramBoolean("Toolbar.Overflow.HomeButton", true);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// ToolbarView, private:

// Override this so that when the user presses F6 to rotate toolbar panes,
// the location bar gets focus, not the first control in the toolbar - and
// also so that it selects all content in the location bar.
views::View* ToolbarView::GetDefaultFocusableChild() {
  return location_bar_view_;
}

void ToolbarView::InitLayout() {
  const int default_margin =
      GetLayoutConstant(LayoutConstant::kToolbarIconDefaultMargin);
  const int location_bar_margin =
      GetLayoutConstant(LayoutConstant::kLocationBarMargin);

  // Shift previously flex-able elements' order by `kOrderOffset`.
  // This will cause them to be the first ones to drop out or shrink to minimum.
  // Order 1 - kOrderOffset will be assigned to new flex-able elements.
  constexpr int kOrderOffset = 1000;
  // If kOmniboxResizingPrioritization is enabled, give the location bar the
  // highest priority as it will first shrink down to its soft minimum but won't
  // hit its hard minimum until all other items have dropped out.
  const int location_bar_flex_order =
      base::FeatureList::IsEnabled(features::kOmniboxResizingPrioritization)
          ? 1
          : kOrderOffset + 1;
  constexpr int kToolbarActionsFlexOrder = kOrderOffset + 2;
  constexpr int kExtensionsFlexOrder = kOrderOffset + 3;

  const views::FlexSpecification location_bar_flex_rule =
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kUnbounded)
          .WithOrder(location_bar_flex_order);

  layout_manager_ = SetLayoutManager(std::make_unique<views::FlexLayout>());

  layout_manager_->SetOrientation(views::LayoutOrientation::kHorizontal)
      .SetCrossAxisAlignment(views::LayoutAlignment::kCenter)
      .SetCollapseMargins(true)
      .SetDefault(views::kMarginsKey, gfx::Insets::VH(0, default_margin));

  if (location_bar_view_) {
    // Zephyrus: center the omnibox in the toolbar at a capped width, flanked by
    // two flexible spacers, to match the centered URL bar in the design. The
    // custom flex rule lets the omnibox grow up to a maximum and shrink when
    // space is tight; the spacers absorb the remaining space on both sides.
    constexpr int kZephyrusOmniboxMaxWidth = 720;
    constexpr int kZephyrusSpacerOrder = kOrderOffset + 5;
    const views::FlexRule zephyrus_centered_omnibox_rule =
        base::BindRepeating(
            [](const views::View* view, const views::SizeBounds& bounds) {
              const int height = view->GetPreferredSize(bounds).height();
              const int width = std::max(
                  bounds.width().min_of(kZephyrusOmniboxMaxWidth), 0);
              return gfx::Size(width, height);
            });
    location_bar_view_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(zephyrus_centered_omnibox_rule)
            .WithOrder(location_bar_flex_order));
    location_bar_view_->SetProperty(views::kMarginsKey,
                                    gfx::Insets::VH(0, location_bar_margin));

    const views::FlexSpecification zephyrus_spacer_flex =
        views::FlexSpecification(
            views::MinimumFlexSizeRule::kPreferredSnapToZero,
            views::MaximumFlexSizeRule::kUnbounded)
            .WithOrder(kZephyrusSpacerOrder)
            .WithWeight(1);
    const size_t leading_index = GetIndexOf(location_bar_view_).value();
    views::View* leading_spacer =
        AddChildViewAt(std::make_unique<views::View>(), leading_index);
    leading_spacer->SetProperty(views::kFlexBehaviorKey, zephyrus_spacer_flex);
    // Let events fall through to the toolbar so these areas are draggable
    // caption (handled in BrowserView::NonClientHitTest).
    leading_spacer->SetCanProcessEventsWithinSubtree(false);
    const size_t trailing_index = GetIndexOf(location_bar_view_).value() + 1;
    views::View* trailing_spacer =
        AddChildViewAt(std::make_unique<views::View>(), trailing_index);
    trailing_spacer->SetProperty(views::kFlexBehaviorKey, zephyrus_spacer_flex);
    trailing_spacer->SetCanProcessEventsWithinSubtree(false);
  } else {
    // If the location bar is part of a WebView, make that stretchable.
    toolbar_webview_->SetProperty(views::kFlexBehaviorKey,
                                  location_bar_flex_rule);
  }

  if (extensions_container_) {
    const views::FlexSpecification extensions_flex_rule =
        views::FlexSpecification(
            extensions_container_->GetAnimatingLayoutManager()
                ->GetDefaultFlexRule())
            .WithOrder(kExtensionsFlexOrder);

    extensions_container_->SetProperty(views::kFlexBehaviorKey,
                                       extensions_flex_rule);
  }

  if (pinned_toolbar_actions_container_) {
    pinned_toolbar_actions_container_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(
            pinned_toolbar_actions_container_->GetAnimatingLayoutManager()
                ->GetDefaultFlexRule())
            .WithOrder(kToolbarActionsFlexOrder));
  }

  if (toolbar_divider_) {
    toolbar_divider_->SetProperty(
        views::kMarginsKey,
        gfx::Insets::VH(
            0, GetLayoutConstant(LayoutConstant::kToolbarDividerSpacing)));
  }

  if (glic_button_ &&
      base::FeatureList::IsEnabled(features::kToolbarGlicButtonResizing)) {
    glic_button_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(
            views::MinimumFlexSizeRule::kPreferredSnapToMinimum,
            views::MaximumFlexSizeRule::kPreferred));
  }

  if (app_menu_button_ &&
      base::FeatureList::IsEnabled(features::kToolbarAppMenuLabelResizing)) {
    app_menu_button_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                                 views::MaximumFlexSizeRule::kPreferred));
  }

  if (avatar_ &&
      base::FeatureList::IsEnabled(features::kToolbarProfileChipResizing)) {
    // Flex order for the profile avatar button is determined by the
    // `toolbar_controller`.
    avatar_->SetProperty(
        views::kFlexBehaviorKey,
        views::FlexSpecification(
            views::MinimumFlexSizeRule::kScaleToMinimumSnapToZero,
            views::MaximumFlexSizeRule::kPreferred));
  }

  // Order 1 is reserved for the location bar if kOmniboxResizingPrioritization
  // is enabled.
  constexpr int kToolbarFlexOrderStart = 2;

  // TODO(crbug.com/40929989): Ignore containers till issue addressed.
  toolbar_controller_ = std::make_unique<ToolbarController>(
      ToolbarController::GetDefaultResponsiveElements(browser_),
      ToolbarController::GetDefaultOverflowOrder(), kToolbarFlexOrderStart,
      this, toolbar_webview_.get(), overflow_button_, pinned_toolbar_actions_,
      PinnedToolbarActionsModel::Get(browser_view_->GetProfile()));
  overflow_button_->set_toolbar_controller(toolbar_controller_.get());

  LayoutCommon();
}

namespace {

// A Windows 11-style caption button (minimize / maximize-restore / close) that
// paints the authentic Win11 glyphs via Windows11IconPainter, with rectangular
// hover backgrounds (red for close) like the native title bar buttons.
class ZephyrusWin11CaptionButton : public views::Button {
  METADATA_HEADER(ZephyrusWin11CaptionButton, views::Button)

 public:
  enum class Kind { kMinimize, kMaximizeRestore, kClose };

  ZephyrusWin11CaptionButton(PressedCallback callback,
                             Kind kind,
                             const std::u16string& name)
      : views::Button(std::move(callback)), kind_(kind) {
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
    SetAnimateOnStateChange(false);
    // Height matches the full title bar (location bar height + the interior
    // margin that the negative margins below cancel out), so the buttons fill
    // the title bar flush to the top/bottom edges.
    SetPreferredSize(gfx::Size(46, 36));
  }

  void SetMaximized(bool maximized) {
    if (maximized_ != maximized) {
      maximized_ = maximized;
      SchedulePaint();
    }
  }

  void SetForeground(SkColor color) {
    if (foreground_ != color) {
      foreground_ = color;
      SchedulePaint();
    }
  }

  // views::Button:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    SkColor bg = SK_ColorTRANSPARENT;
    const bool hovered = GetState() == STATE_HOVERED;
    const bool pressed = GetState() == STATE_PRESSED;
    if (kind_ == Kind::kClose) {
      if (pressed) {
        bg = SkColorSetRGB(0xF1, 0x70, 0x7A);
      } else if (hovered) {
        bg = SkColorSetRGB(0xC4, 0x2B, 0x1C);
      }
    } else if (pressed) {
      bg = SkColorSetA(foreground_, 0x3A);
    } else if (hovered) {
      bg = SkColorSetA(foreground_, 0x24);
    }
    if (bg != SK_ColorTRANSPARENT) {
      canvas->FillRect(GetLocalBounds(), bg);
    }
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    SkColor symbol_color = foreground_;
    if (kind_ == Kind::kClose &&
        (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED)) {
      symbol_color = SK_ColorWHITE;
    }

    // Zephyrus: custom pixel-block glyphs for close and maximize/restore.
    // Minimize keeps the crisp pixel-snapped Win11 dash below.
    if (kind_ == Kind::kClose || kind_ == Kind::kMaximizeRestore) {
      constexpr int kGlyphSize = 12;
      const gfx::VectorIcon& icon = kind_ == Kind::kClose
                                        ? kZephyrusCloseIcon
                                        : kZephyrusMaximizeIcon;
      const gfx::ImageSkia image =
          gfx::CreateVectorIcon(icon, kGlyphSize, symbol_color);
      const gfx::Point center = GetContentsBounds().CenterPoint();
      canvas->DrawImageInt(image, center.x() - kGlyphSize / 2,
                           center.y() - kGlyphSize / 2);
      return;
    }

    gfx::ScopedCanvas scoped_canvas(canvas);
    const float scale = canvas->UndoDeviceScaleFactor();
    const int symbol_size_pixels = base::ClampRound(10 * scale);
    gfx::RectF bounds_rect(GetContentsBounds());
    bounds_rect.Scale(scale);
    gfx::Rect symbol_rect(gfx::ToEnclosingRect(bounds_rect));
    symbol_rect.ClampToCenteredSize(
        gfx::Size(symbol_size_pixels, symbol_size_pixels));

    cc::PaintFlags flags;
    flags.setAntiAlias(false);
    flags.setColor(symbol_color);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    const int stroke_width = base::ClampRound(scale);
    flags.setStrokeWidth(stroke_width);

    switch (kind_) {
      case Kind::kMinimize:
        painter_.PaintMinimizeIcon(canvas, symbol_rect, flags);
        break;
      case Kind::kMaximizeRestore:
        if (maximized_) {
          painter_.PaintRestoreIcon(canvas, symbol_rect, flags);
        } else {
          painter_.PaintMaximizeIcon(canvas, symbol_rect, flags);
        }
        break;
      case Kind::kClose: {
        const float halo =
            stroke_width * (symbol_color == SK_ColorWHITE ? 0.1f : 0.05f);
        flags.setStrokeWidth(stroke_width + halo);
        painter_.PaintCloseIcon(canvas, symbol_rect, flags);
        break;
      }
    }
  }

 private:
  Kind kind_;
  bool maximized_ = false;
  SkColor foreground_ = SK_ColorWHITE;
  Windows11IconPainter painter_;
};

BEGIN_METADATA(ZephyrusWin11CaptionButton)
END_METADATA

// A ToolbarButton whose icon color can be forced to a value that contrasts with
// the current title bar, so the pin glyph stays visible on light page colors.
class ZephyrusPinButton : public ToolbarButton {
  METADATA_HEADER(ZephyrusPinButton, ToolbarButton)

 public:
  explicit ZephyrusPinButton(PressedCallback callback)
      : ToolbarButton(std::move(callback)) {}

  void SetZephyrusForeground(SkColor color) {
    if (foreground_ == color) {
      return;
    }
    foreground_ = color;
    UpdateIcon();
  }

  // ToolbarButton:
  SkColor GetForegroundColor(ButtonState state) const override {
    return foreground_.value_or(ToolbarButton::GetForegroundColor(state));
  }

 private:
  std::optional<SkColor> foreground_;
};

BEGIN_METADATA(ZephyrusPinButton)
END_METADATA

// Zephyrus: modal confirmation for a destructive workspace delete. The Figma
// file has no dialog component, so the surface is derived from the design
// system already in use — the workspace dropdown's lifted panel color and the
// 10px card radius — rather than inventing a second visual language.
constexpr int kZephyrusDialogRadius = zephyrus::kCornerRadius;
constexpr int kZephyrusDialogWidth = 360;
// Entrance is generous enough to be read as an arrival; the exit is quicker,
// because waiting on a dialog you have already dismissed is what makes an
// interface feel slow. Both stay inside the sub-300ms UI budget.
constexpr base::TimeDelta kZephyrusDialogEnterDuration =
    base::Milliseconds(200);
constexpr base::TimeDelta kZephyrusDialogExitDuration = base::Milliseconds(130);

// A settings row in the Shield panel: highlights on hover and forwards a click
// anywhere on the row to its toggle, matching the workspace dropdown's rows.
class ZephyrusShieldToggleRow : public views::View {
  METADATA_HEADER(ZephyrusShieldToggleRow, views::View)

 public:
  explicit ZephyrusShieldToggleRow(SkColor hover) : hover_(hover) {}

  // `toggle` is used only for hit-testing; `on_activate` does the flipping, so
  // the row never has to reach into the button's private callback.
  void SetToggle(views::ToggleButton* toggle,
                 base::RepeatingClosure on_activate) {
    toggle_ = toggle;
    on_activate_ = std::move(on_activate);
  }

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    SetBackground(
        views::CreateRoundedRectBackground(hover_, kZephyrusDialogRadius));
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    SetBackground(nullptr);
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    return event.IsOnlyLeftMouseButton();
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    // Only count as a click if the release lands inside the row, so dragging
    // off to abort behaves the way a button does. A press on the toggle itself
    // is the toggle's own event — don't double-flip it.
    if (!on_activate_ || !event.IsOnlyLeftMouseButton() ||
        !HitTestPoint(event.location()) ||
        (toggle_ && toggle_->bounds().Contains(event.location()))) {
      return;
    }
    on_activate_.Run();
  }

 private:
  SkColor hover_;
  raw_ptr<views::ToggleButton> toggle_ = nullptr;
  base::RepeatingClosure on_activate_;
};

BEGIN_METADATA(ZephyrusShieldToggleRow)
END_METADATA

// A dialog action button. The native dialog button row is not used (it paints
// on the platform frame, which is light-themed regardless of our permanent
// dark theme), so the buttons live inside the panel and are styled from the
// same lifted-surface model as the workspace dropdown rows.
class ZephyrusDialogButton : public views::LabelButton {
  METADATA_HEADER(ZephyrusDialogButton, views::LabelButton)

 public:
  ZephyrusDialogButton(const std::u16string& text,
                       SkColor foreground,
                       SkColor fill,
                       SkColor hovered_fill,
                       PressedCallback callback)
      : views::LabelButton(std::move(callback), text),
        fill_(fill),
        hovered_fill_(hovered_fill),
        focus_color_(foreground) {
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(9, 16)));
    SetTextColor(views::Button::STATE_NORMAL, foreground);
    SetTextColor(views::Button::STATE_HOVERED, foreground);
    SetTextColor(views::Button::STATE_PRESSED, foreground);
    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
    // The stock focus ring is drawn from a native color id, which still tracks
    // the OS light/dark setting in this tree — it lands as a loud violet halo
    // on our permanently dark panel. Painted below in the panel's own ink
    // instead. Removed, not disabled: focus stays visible for keyboard users.
    views::FocusRing::Remove(this);
    // The layer backs the press-scale transform. It can't fill its bounds
    // opaquely (the fills are translucent and the corners are rounded), and
    // subpixel text AA samples the r,g,b channels of whatever is underneath —
    // which is garbage over a transparent layer. So LCD text has to go off for
    // this button's own label, exactly as it is for the title and body labels.
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    label()->SetSubpixelRenderingEnabled(false);
    UpdateFill();
  }

  // views::Button:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    UpdateFill();
    UpdatePressFeedback();
  }

  // views::Button: OnPaint() is final; this is the paint hook it exposes.
  void PaintButtonContents(gfx::Canvas* canvas) override {
    views::LabelButton::PaintButtonContents(canvas);
    if (!HasFocus()) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(kFocusStroke);
    flags.setColor(SkColorSetA(focus_color_, 0xCC));
    gfx::RectF ring(GetLocalBounds());
    ring.Inset(kFocusStroke / 2.0f);
    canvas->DrawRoundRect(ring, kZephyrusDialogRadius - kFocusStroke / 2.0f,
                          flags);
  }

  void OnFocus() override {
    views::LabelButton::OnFocus();
    SchedulePaint();
  }
  void OnBlur() override {
    views::LabelButton::OnBlur();
    SchedulePaint();
  }

 private:
  static constexpr float kFocusStroke = 2.0f;

  void UpdateFill() {
    const bool hot = GetState() == views::Button::STATE_HOVERED ||
                     GetState() == views::Button::STATE_PRESSED;
    SetBackground(views::CreateRoundedRectBackground(
        hot ? hovered_fill_ : fill_, kZephyrusDialogRadius));
  }

  // Presses scale the button down slightly so it feels like it heard the
  // click. Subtle and fast — this is feedback, not decoration.
  void UpdatePressFeedback() {
    if (!layer() || !gfx::Animation::ShouldRenderRichAnimation()) {
      return;
    }
    gfx::Transform transform;
    if (GetState() == views::Button::STATE_PRESSED) {
      transform = gfx::GetScaleTransform(gfx::Rect(size()).CenterPoint(), 0.97f);
    }
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(base::Milliseconds(120));
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    layer()->SetTransform(transform);
  }

  SkColor fill_;
  SkColor hovered_fill_;
  SkColor focus_color_;
};

BEGIN_METADATA(ZephyrusDialogButton)
END_METADATA

// The dialog's contents. `views::DialogDelegateView`'s constructor is
// pass-key-gated in this tree, so the delegate is a plain
// `views::DialogDelegate` with this as its contents view (the same pattern
// `ZephyrusSettingsPopup` uses).
class ZephyrusDeleteWorkspaceContents : public views::View {
  METADATA_HEADER(ZephyrusDeleteWorkspaceContents, views::View)

 public:
  ZephyrusDeleteWorkspaceContents(const std::u16string& workspace_name,
                                  int tab_count,
                                  SkColor panel,
                                  SkColor foreground,
                                  base::RepeatingClosure on_confirm,
                                  base::RepeatingClosure on_cancel) {
    SetBackground(
        views::CreateRoundedRectBackground(panel, kZephyrusDialogRadius));
    // A hairline top edge. The panel sits on whatever the page happens to be —
    // often near-black — where a drop shadow contributes nothing, so the
    // separation has to come from a lit edge instead.
    SetBorder(views::CreatePaddedBorder(
        views::CreateRoundedRectBorder(1, kZephyrusDialogRadius,
                                       SkColorSetA(foreground, 0x1F)),
        gfx::Insets::TLBR(23, 23, 19, 23)));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* title = AddChildView(
        std::make_unique<views::Label>(u"Delete “" + workspace_name +
                                       u"”?"));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetEnabledColor(foreground);
    title->SetAutoColorReadabilityEnabled(false);
    title->SetSubpixelRenderingEnabled(false);
    title->SetFontList(title->font_list()
                           .DeriveWithSizeDelta(2)
                           .DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));

    // Name the real consequence, with the actual number of tabs at stake.
    std::u16string body;
    if (tab_count == 0) {
      body = u"This workspace will be removed. This can’t be undone.";
    } else if (tab_count == 1) {
      body = u"The tab inside it will be closed. This can’t be undone.";
    } else {
      body = u"All " + base::NumberToString16(tab_count) +
             u" tabs inside it will be closed. This can’t be undone.";
    }
    auto* detail = AddChildView(std::make_unique<views::Label>(body));
    detail->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    detail->SetMultiLine(true);
    detail->SetEnabledColor(SkColorSetA(foreground, 0xB0));
    detail->SetAutoColorReadabilityEnabled(false);
    detail->SetSubpixelRenderingEnabled(false);

    // The body is the last thing read before deciding, so it gets a clear gap
    // from the buttons — bigger than the title-to-body gap, so the block reads
    // as "message, then choice" rather than three evenly spaced rows.
    auto* actions = AddChildView(std::make_unique<views::View>());
    actions->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(18, 0, 0, 0));
    auto* actions_layout =
        actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    actions_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kEnd);

    const SkColor quiet = SkColorSetA(foreground, 0x14);
    const SkColor quiet_hot = SkColorSetA(foreground, 0x24);
    auto* cancel = actions->AddChildView(std::make_unique<ZephyrusDialogButton>(
        u"Cancel", foreground, quiet, quiet_hot, std::move(on_cancel)));
    // Destructive intent is carried by color, and Cancel takes focus so a
    // stray Enter can't close a workspace's tabs. The red is pulled off full
    // saturation: on a dark panel a pure #D93B3B vibrates against the surface.
    actions->AddChildView(std::make_unique<ZephyrusDialogButton>(
        u"Delete workspace", SK_ColorWHITE, SkColorSetRGB(0xC7, 0x3A, 0x40),
        SkColorSetRGB(0xD8, 0x46, 0x4C), std::move(on_confirm)));
    default_focus_ = cancel;
  }

  views::View* default_focus() { return default_focus_; }

  // views::View:
  // Fixed width, height from the layout. This must delegate to the base
  // implementation rather than call GetHeightForWidth(): that helper is itself
  // defined as GetPreferredSize(SizeBounds(w, {})), so calling it from here
  // recurses until the stack overflows.
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return views::View::CalculatePreferredSize(
        views::SizeBounds(kZephyrusDialogWidth, available_size.height()));
  }

 private:
  raw_ptr<views::View> default_focus_ = nullptr;
};

BEGIN_METADATA(ZephyrusDeleteWorkspaceContents)
END_METADATA

// Owns the modal. Two lifetime hazards make this a subclass rather than a bare
// `views::DialogDelegate`:
//
//  1. `WidgetDelegate::owned_by_widget_` defaults to false in this tree and
//     `SetOwnedByWidget` is pass-key gated, while
//     `CreateBrowserModalDialogViews` takes the unique_ptr and `release()`s it.
//     So the delegate must delete itself, exactly as ZephyrusSettingsPopup
//     does.
//  2. Confirming runs `DeleteWorkspace`, which closes tabs synchronously and
//     re-enters browser-window layout. Running that from a button callback
//     (or from `AcceptDialog()`) would mutate the tab strip while the modal's
//     own teardown is still on the stack. It is deferred instead.
class ZephyrusDeleteWorkspaceDialog : public views::DialogDelegate {
 public:
  ZephyrusDeleteWorkspaceDialog(Browser* browser,
                                const std::u16string& workspace_name,
                                int tab_count,
                                SkColor panel,
                                SkColor foreground,
                                base::OnceClosure on_confirm)
      : on_confirm_(std::move(on_confirm)),
        browser_(browser ? browser->AsWeakPtr()
                         : base::WeakPtr<Browser>()) {
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_corner_radius(kZephyrusDialogRadius);
    set_margins(gfx::Insets());

    auto contents = std::make_unique<ZephyrusDeleteWorkspaceContents>(
        workspace_name, tab_count, panel, foreground,
        base::BindRepeating(&ZephyrusDeleteWorkspaceDialog::CloseDialog,
                            base::Unretained(this), true),
        base::BindRepeating(&ZephyrusDeleteWorkspaceDialog::CloseDialog,
                            base::Unretained(this), false));
    views::View* initial_focus = contents->default_focus();
    SetContentsView(std::move(contents));
    SetInitiallyFocusedView(initial_focus);
  }

  // views::WidgetDelegate:
  // Holds the close for one exit animation, then lets it through. This hook
  // (rather than doing the work in CloseDialog) is what makes Escape and the
  // frame's own close animate too, not just the buttons. It is marked
  // deprecated upstream in favour of Widget::MakeCloseSynchronous(), but that
  // requires CLIENT_OWNS_WIDGET ownership, which constrained_window does not
  // give us.
  bool OnCloseRequested(views::Widget::ClosedReason close_reason) override {
    views::Widget* widget = GetWidget();
    if (exiting_ || !widget || !gfx::Animation::ShouldRenderRichAnimation()) {
      return true;
    }
    exiting_ = true;

    // The scrim lifts on the same curve and duration, so the dialog and the
    // dimming leave as one motion rather than in two steps.
    if (browser_ && browser_->window()) {
      if (BrowserView* browser_view =
              BrowserView::GetBrowserViewForBrowser(browser_.get())) {
        browser_view->FadeOutZephyrusScrim(kZephyrusDialogExitDuration);
      }
    }

    // Exits are shorter than entrances and settle *inward* — the mirror of the
    // 0.97 entrance — so the dialog reads as receding rather than being yanked.
    ui::Layer* layer = widget->GetLayer();
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(kZephyrusDialogExitDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    layer->SetOpacity(0.0f);
    layer->SetTransform(gfx::GetScaleTransform(
        gfx::Rect(widget->GetWindowBoundsInScreen().size()).CenterPoint(),
        0.98f));

    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusDeleteWorkspaceDialog::FinishClose,
                       weak_factory_.GetWeakPtr()),
        kZephyrusDialogExitDuration);
    return false;
  }

  void WindowClosing() override {
    views::DialogDelegate::WindowClosing();
    // Hazard 2: let the widget teardown unwind completely before any tab is
    // closed.
    if (confirmed_ && on_confirm_) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, std::move(on_confirm_));
    }
    // Hazard 1: deferred so the teardown on the stack never touches a freed
    // delegate.
    if (!delete_scheduled_) {
      delete_scheduled_ = true;
      base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                   this);
    }
  }

 private:
  void CloseDialog(bool confirmed) {
    if (exiting_) {
      return;  // Already leaving; ignore a second click during the animation.
    }
    confirmed_ = confirmed;
    if (views::Widget* widget = GetWidget();
        widget && !widget->IsClosed()) {
      widget->Close();
    }
  }

  // Second pass through OnCloseRequested, which now lets the close through.
  void FinishClose() {
    if (views::Widget* widget = GetWidget();
        widget && !widget->IsClosed()) {
      widget->Close();
    }
  }

  base::OnceClosure on_confirm_;
  base::WeakPtr<Browser> browser_;
  bool confirmed_ = false;
  bool exiting_ = false;
  bool delete_scheduled_ = false;
  base::WeakPtrFactory<ZephyrusDeleteWorkspaceDialog> weak_factory_{this};
};

void ShowZephyrusDeleteWorkspaceDialog(Browser* browser,
                                       const std::u16string& workspace_name,
                                       int tab_count,
                                       SkColor panel,
                                       SkColor foreground,
                                       base::OnceClosure on_confirm) {
  if (!browser || !browser->window()) {
    return;
  }
  auto delegate = std::make_unique<ZephyrusDeleteWorkspaceDialog>(
      browser, workspace_name, tab_count, panel, foreground,
      std::move(on_confirm));
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(delegate), browser->window()->GetNativeWindow());
  widget->Show();
  // Browser-modal placement pins dialogs to the window's top edge, which reads
  // as misaligned in the frameless Zephyrus UI. Center it instead.
  const gfx::Rect browser_bounds = browser->window()->GetBounds();
  gfx::Rect bounds = widget->GetWindowBoundsInScreen();
  bounds.set_origin(
      gfx::Point(browser_bounds.x() +
                     (browser_bounds.width() - bounds.width()) / 2,
                 browser_bounds.y() +
                     (browser_bounds.height() - bounds.height()) / 2));
  widget->SetBounds(bounds);

  // Entrance: a modal is not anchored to a trigger, so it scales from its own
  // center rather than an origin. Starts at 0.97 (never scale(0) — nothing
  // real appears from nothing) and eases out, so it reads as arriving rather
  // than blinking into place. 200ms keeps it inside the sub-300ms UI budget.
  if (ui::Layer* layer = widget->GetLayer();
      layer && gfx::Animation::ShouldRenderRichAnimation()) {
    layer->SetOpacity(0.0f);
    layer->SetTransform(
        gfx::GetScaleTransform(gfx::Rect(bounds.size()).CenterPoint(), 0.97f));
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(kZephyrusDialogEnterDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT_3);
    layer->SetOpacity(1.0f);
    layer->SetTransform(gfx::Transform());
  }
}

// A floating "liquid glass" pill painted behind a group of toolbar controls
// (per the Zephyrus Browser Design Figma: Window/Button Group, LiquidGlass).
// Translucent white fill + hairline, fully rounded. Positioned manually in
// ToolbarView::Layout and ignored by the FlexLayout pass.
class ZephyrusGlassPill : public views::View {
  METADATA_HEADER(ZephyrusGlassPill, views::View)

 public:
  ZephyrusGlassPill() {
    SetCanProcessEventsWithinSubtree(false);
    SetProperty(views::kViewIgnoredByLayoutKey, true);
  }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    const float radius = height() / 2.0f;
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(SkColorSetA(SK_ColorWHITE, 0x24));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), radius, fill);
    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    stroke.setStrokeWidth(1.0f);
    stroke.setColor(SkColorSetA(SK_ColorWHITE, 0x2E));
    gfx::RectF hairline(GetLocalBounds());
    hairline.Inset(0.5f);
    canvas->DrawRoundRect(hairline, radius - 0.5f, stroke);
  }
};

BEGIN_METADATA(ZephyrusGlassPill)
END_METADATA

}  // namespace

void ToolbarView::AddZephyrusWindowControls() {
  // Glass backdrops paint underneath the controls, so they go in at index 0
  // (first paint order). Bounds are set in Layout().
  zephyrus_nav_pill_backdrop_ =
      AddChildViewAt(std::make_unique<ZephyrusGlassPill>(), 0);
  zephyrus_controls_backdrop_ =
      AddChildViewAt(std::make_unique<ZephyrusGlassPill>(), 1);
  using Kind = ZephyrusWin11CaptionButton::Kind;
  auto add_button = [&](Kind kind, int accessible_name_id,
                        views::Button::PressedCallback callback) {
    const std::u16string name = l10n_util::GetStringUTF16(accessible_name_id);
    return AddChildView(std::make_unique<ZephyrusWin11CaptionButton>(
        std::move(callback), kind, name));
  };

  zephyrus_minimize_button_ = add_button(
      Kind::kMinimize, IDS_APP_ACCNAME_MINIMIZE,
      base::BindRepeating(
          [](ToolbarView* toolbar) {
            if (views::Widget* widget = toolbar->GetWidget()) {
              widget->Minimize();
            }
          },
          base::Unretained(this)));

  zephyrus_maximize_button_ = add_button(
      Kind::kMaximizeRestore, IDS_APP_ACCNAME_MAXIMIZE,
      base::BindRepeating(
          [](ToolbarView* toolbar) {
            views::Widget* widget = toolbar->GetWidget();
            if (!widget) {
              return;
            }
            if (widget->IsMaximized()) {
              widget->Restore();
            } else {
              widget->Maximize();
            }
          },
          base::Unretained(this)));

  zephyrus_close_button_ = add_button(
      Kind::kClose, IDS_APP_ACCNAME_CLOSE,
      base::BindRepeating(
          [](ToolbarView* toolbar) {
            if (views::Widget* widget = toolbar->GetWidget()) {
              widget->CloseWithReason(
                  views::Widget::ClosedReason::kCloseButtonClicked);
            }
          },
          base::Unretained(this)));

  // Reserve full-height, edge-hugging slots in the flex pass. NOTE: the
  // authoritative flush-to-corner positioning happens in ToolbarView::Layout(),
  // which overrides these buttons' bounds after the layout manager runs —
  // FlexLayout does not reliably honor negative margins past the interior
  // margin, which left the controls ~6dip shy of the corner. The properties
  // below still matter: they size the flex slots so neighbors don't overlap.
  // (TOOLBAR_INTERIOR_MARGIN = VH(3, 6).)
  for (views::Button* button :
       {zephyrus_minimize_button_.get(), zephyrus_maximize_button_.get(),
        zephyrus_close_button_.get()}) {
    button->SetProperty(views::kCrossAxisAlignmentKey,
                        views::LayoutAlignment::kStretch);
  }
  zephyrus_minimize_button_->SetProperty(views::kMarginsKey,
                                         gfx::Insets::TLBR(-3, 0, -3, 0));
  zephyrus_maximize_button_->SetProperty(views::kMarginsKey,
                                         gfx::Insets::TLBR(-3, 0, -3, 0));
  zephyrus_close_button_->SetProperty(views::kMarginsKey,
                                      gfx::Insets::TLBR(-3, 0, -3, -6));
}

void ToolbarView::SetZephyrusOmniboxFocused(bool focused) {
  if (!zephyrus_omnibox_focus_animation_) {
    zephyrus_omnibox_focus_animation_ =
        std::make_unique<ZephyrusOmniboxFocusAnimation>(base::BindRepeating(
            [](ToolbarView* toolbar) { toolbar->InvalidateLayout(); },
            base::Unretained(this)));
  }
  zephyrus_omnibox_focus_animation_->SetFocused(focused);
}

void ToolbarView::SetZephyrusTitlebarColor(std::optional<SkColor> color) {
  if (!background()) {
    return;
  }
  const bool animating =
      zephyrus_color_transition_ && zephyrus_color_transition_->is_animating();
  // Callers fire on every navigation signal; everything below repaints or
  // rebuilds backgrounds, so bail when nothing changed. The first call always
  // applies so highlights get initialized even for the default (no) color.
  // While a crossfade runs, zephyrus_titlebar_color_ holds a lerped
  // intermediate, so dedupe against the animation target instead.
  if (zephyrus_titlebar_color_applied_ &&
      (animating ? color == zephyrus_color_target_
                 : color == zephyrus_titlebar_color_)) {
    return;
  }
  const SkColor default_color =
      GetColorProvider() ? GetColorProvider()->GetColor(kColorToolbar)
                         : SK_ColorDKGRAY;
  const SkColor from = zephyrus_titlebar_color_.value_or(default_color);
  const SkColor to = color.value_or(default_color);
  const bool first = !zephyrus_titlebar_color_applied_;
  zephyrus_titlebar_color_applied_ = true;
  zephyrus_color_target_ = color;
  // Snap (no crossfade) on the very first application, when the color isn't
  // actually changing, or when the OS asks for reduced motion.
  if (first || from == to || !gfx::Animation::ShouldRenderRichAnimation()) {
    zephyrus_titlebar_color_ = color;
    ApplyZephyrusTitlebarColor(color);
    return;
  }
  if (!zephyrus_color_transition_) {
    zephyrus_color_transition_ = std::make_unique<ZephyrusColorTransition>(
        base::BindRepeating(
            [](ToolbarView* toolbar, SkColor lerped) {
              toolbar->zephyrus_titlebar_color_ = lerped;
              toolbar->ApplyZephyrusTitlebarColor(lerped);
            },
            base::Unretained(this)),
        base::BindRepeating(
            [](ToolbarView* toolbar) {
              // Settle on the true target (which may be nullopt = themed).
              toolbar->zephyrus_titlebar_color_ = toolbar->zephyrus_color_target_;
              toolbar->ApplyZephyrusTitlebarColor(toolbar->zephyrus_color_target_);
            },
            base::Unretained(this)));
  }
  zephyrus_color_transition_->Start(from, to);
}

void ToolbarView::ApplyZephyrusTitlebarColor(std::optional<SkColor> color) {
  if (!background()) {
    return;
  }
  if (auto* corners_background = background()->AsA<CustomCornersBackground>()) {
    corners_background->SetExplicitPrimaryColor(color);
  }

  // Let the omnibox adapt its background/text to the same color.
  if (location_bar_view_) {
    location_bar_view_->SetZephyrusTitlebarColor(color);
  }

  // Adapt the workspace switcher label to the title bar color.
  UpdateZephyrusWorkspaceButton();

  SkColor effective = color.value_or(
      GetColorProvider() ? GetColorProvider()->GetColor(kColorToolbar)
                         : SK_ColorDKGRAY);
  UpdateZephyrusNavButtonBackgrounds(effective);

  // Adapt the Win11 caption-button glyph color to the title bar.
  const SkColor caption_fg = color_utils::IsDark(effective)
                                 ? SK_ColorWHITE
                                 : SkColorSetRGB(0x1A, 0x1A, 0x1A);
  for (views::Button* button :
       {zephyrus_minimize_button_.get(), zephyrus_maximize_button_.get(),
        zephyrus_close_button_.get()}) {
    if (button) {
      static_cast<ZephyrusWin11CaptionButton*>(button)->SetForeground(
          caption_fg);
    }
  }

  // Keep the pin glyph (and its hover ink-drop) contrasting with the title bar.
  if (zephyrus_pin_button_) {
    static_cast<ZephyrusPinButton*>(zephyrus_pin_button_.get())
        ->SetZephyrusForeground(
            color_utils::GetColorWithMaxContrast(effective));
  }
  // Same for the ad-block shield glyph.
  if (zephyrus_adblock_button_) {
    static_cast<ZephyrusPinButton*>(zephyrus_adblock_button_.get())
        ->SetZephyrusForeground(
            color_utils::GetColorWithMaxContrast(effective));
  }
}

void ToolbarView::UpdateZephyrusNavButtonBackgrounds(SkColor titlebar_color) {
  // Zephyrus: every title bar glyph sits BARE — no resting container chip
  // behind the navigation arrows, new tab, pin or the shield. (The arrows were
  // already quiet; these action buttons used to carry a resting circular chip.)
  //
  // Clearing the highlight also drops the tint it used to apply to the hover
  // ink drop, so derive the ink drop's base color from the title bar instead.
  // Otherwise it would follow Chromium's own light/dark theme and could wash
  // out against our fixed title bar, leaving the buttons with no press or hover
  // feedback at all.
  const SkColor ink = color_utils::GetColorWithMaxContrast(titlebar_color);
  ToolbarButton* const nav_buttons[] = {
      home_.get(), zephyrus_new_tab_button_.get(), zephyrus_pin_button_.get(),
      zephyrus_adblock_button_.get()};
  for (ToolbarButton* button : nav_buttons) {
    if (!button) {
      continue;
    }
    // Both parameters empty clears any previously set highlight.
    button->SetHighlight(std::u16string(), std::nullopt);
    views::InkDrop::Get(button)->SetBaseColor(ink);
  }
  SchedulePaint();
}

void ToolbarView::AddZephyrusPinButton() {
  auto pin = std::make_unique<ZephyrusPinButton>(base::BindRepeating(
      [](ToolbarView* toolbar) {
        if (BrowserView* browser_view =
                BrowserView::GetBrowserViewForBrowser(toolbar->browser_)) {
          browser_view->ToggleZephyrusTitlebarPinned();
        }
      },
      base::Unretained(this)));
  pin->SetVectorIcon(kZephyrusPinIcon);
  // Sit in the right cluster, immediately to the LEFT of the app menu
  // (three-dots), so the title-bar pin lives with the other window-level
  // controls rather than floating beside the centered omnibox.
  const size_t index = app_menu_button_
                           ? GetIndexOf(app_menu_button_).value()
                           : GetIndexOf(location_bar_view_).value() + 1;
  zephyrus_pin_button_ = AddChildViewAt(std::move(pin), index);
  UpdateZephyrusPinButton();
}

void ToolbarView::UpdateZephyrusPinButton() {
  if (!zephyrus_pin_button_) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  const bool pinned =
      !browser_view || browser_view->IsZephyrusTitlebarPinned();
  // Zephyrus: single custom pin glyph for both pinned and unpinned states.
  zephyrus_pin_button_->SetVectorIcon(kZephyrusPinIcon);
  const std::u16string name =
      pinned ? u"Unpin title bar (auto-hide)" : u"Pin title bar";
  zephyrus_pin_button_->SetTooltipText(name);
  zephyrus_pin_button_->GetViewAccessibility().SetName(name);
}

void ToolbarView::AddZephyrusAdblockButton() {
  auto shield = std::make_unique<ZephyrusPinButton>(base::BindRepeating(
      [](ToolbarView* toolbar) { toolbar->ShowZephyrusAdblockBubble(); },
      base::Unretained(this)));
  shield->SetVectorIcon(kZephyrusShieldIcon);
  const std::u16string name = u"Zephyrus Shield — ad & tracker blocker";
  shield->SetTooltipText(name);
  shield->GetViewAccessibility().SetName(name);
  // Place it in the left group, just after the workspace switcher.
  size_t index = children().size();
  if (zephyrus_workspace_button_) {
    index = GetIndexOf(zephyrus_workspace_button_).value() + 1;
  } else if (zephyrus_new_tab_button_) {
    index = GetIndexOf(zephyrus_new_tab_button_).value() + 1;
  }
  zephyrus_adblock_button_ = AddChildViewAt(std::move(shield), index);
}

namespace {

// Springy entrance for anchored Zephyrus bubbles: fade in while scaling from
// 95% about the top edge (so the bubble grows out of the toolbar control that
// opened it), overshooting to ~102% before settling — a quick under-damped
// pop. Compositor-only (opacity/transform); skipped under OS reduced motion.
void AnimateZephyrusBubbleIn(views::Widget* widget) {
  if (!widget) {
    return;
  }
  ui::Layer* layer = widget->GetLayer();
  if (!layer || !gfx::Animation::ShouldRenderRichAnimation()) {
    return;
  }
  const gfx::Transform settled = layer->transform();
  const gfx::Point origin(layer->bounds().width() / 2, 0);
  layer->SetOpacity(0.0f);
  layer->SetTransform(gfx::GetScaleTransform(origin, 0.95f));
  ui::LayerAnimator* animator = layer->GetAnimator();
  auto fade = ui::LayerAnimationElement::CreateOpacityElement(
      1.0f, base::Milliseconds(120));
  fade->set_tween_type(gfx::Tween::EASE_OUT);
  animator->StartAnimation(new ui::LayerAnimationSequence(std::move(fade)));
  auto pop = ui::LayerAnimationElement::CreateTransformElement(
      gfx::GetScaleTransform(origin, 1.02f), base::Milliseconds(150));
  pop->set_tween_type(gfx::Tween::EASE_OUT_2);
  auto settle = ui::LayerAnimationElement::CreateTransformElement(
      settled, base::Milliseconds(110));
  settle->set_tween_type(gfx::Tween::EASE_IN_OUT);
  auto* spring = new ui::LayerAnimationSequence(std::move(pop));
  spring->AddElement(std::move(settle));
  animator->StartAnimation(spring);
}

}  // namespace

void ToolbarView::ShowZephyrusAdblockBubble() {
  if (!zephyrus_adblock_button_) {
    return;
  }
  zephyrus_adblock::ZephyrusAdblockService* service =
      zephyrus_adblock::ZephyrusAdblockServiceFactory::GetForBrowserContext(
          browser_->profile());
  if (!service) {
    return;
  }
  int this_page = 0;
  if (content::WebContents* wc =
          browser_->tab_strip_model()->GetActiveWebContents()) {
    if (auto* helper =
            zephyrus_adblock::ZephyrusAdblockTabHelper::FromWebContents(wc)) {
      this_page = helper->blocked_this_page();
    }
  }

  // Surface derived from the permanent theme by the same lift() model as the
  // workspace dropdown and the delete dialog, so all three cards are literally
  // the same material. The old hardcoded #18181C was a neutral grey that
  // belonged to no theme and read as a foreign panel next to them.
  const SkColor kBase = BrowserView::kZephyrusThemeColor;
  const SkColor kOverlay =
      color_utils::IsDark(kBase) ? SK_ColorWHITE : SK_ColorBLACK;
  auto lift = [&](SkAlpha a) {
    return color_utils::AlphaBlend(kOverlay, kBase, a);
  };
  const SkColor kCardBg = lift(0x22);       // Same level as the dropdown panel.
  const SkColor kInsetCard = lift(0x2E);    // Raised group inside the card.
  const SkColor kRowHover = lift(0x3A);     // Same hover level as menu rows.
  const SkColor kFg = color_utils::GetColorWithMaxContrast(kBase);
  const SkColor kMuted = SkColorSetA(kFg, 0xB0);  // System body alpha.
  const SkColor kFaint = SkColorSetA(kFg, 0x8A);
  constexpr SkColor kAccent = zephyrus::kAccent;
  constexpr int kWidth = 288;

  auto content = std::make_unique<views::View>();
  content->SetBackground(
      views::CreateRoundedRectBackground(kCardBg, kZephyrusDialogRadius));
  auto* col = content->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(18, 18), 0));
  col->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  auto make_label = [](const std::u16string& text, SkColor color,
                       int size_delta, gfx::Font::Weight weight) {
    auto label = std::make_unique<views::Label>(text);
    label->SetEnabledColor(color);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetSubpixelRenderingEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetFontList(
        label->font_list().DeriveWithSizeDelta(size_delta).DeriveWithWeight(
            weight));
    return label;
  };
  auto add_spacer = [&content](int height) {
    content->AddChildView(std::make_unique<views::View>())
        ->SetPreferredSize(gfx::Size(1, height));
  };

  // ---- Header: shield glyph + title -----------------------------------------
  auto* header = content->AddChildView(std::make_unique<views::View>());
  auto* hl = header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 9));
  hl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  header->AddChildView(std::make_unique<views::ImageView>(
      ui::ImageModel::FromVectorIcon(kZephyrusShieldIcon, kAccent, 18)));
  // Title weight/size matches the delete dialog's title, so a Zephyrus panel
  // header reads the same wherever it appears.
  auto* title = header->AddChildView(
      make_label(u"Zephyrus Shield", kFg, 2, gfx::Font::Weight::SEMIBOLD));
  hl->SetFlexForView(title, 1);

  add_spacer(14);

  // ---- Hero: blocked-on-this-page count -------------------------------------
  // The number carries the whole message, so it gets the size and the caption
  // sits tight underneath it rather than floating a line away.
  content->AddChildView(make_label(base::NumberToString16(this_page), kFg, 20,
                                   gfx::Font::Weight::BOLD));
  content->AddChildView(make_label(u"trackers & ads blocked on this page",
                                   kMuted, 0, gfx::Font::Weight::NORMAL));

  add_spacer(16);

  // ---- Secondary stats, grouped on a raised card ----------------------------
  // Previously two full-bleed 1px dividers chopped the card into bands. The
  // system groups by raising a surface instead, which reads as one object and
  // needs no lines.
  auto* stats = content->AddChildView(std::make_unique<views::View>());
  stats->SetBackground(
      views::CreateRoundedRectBackground(kInsetCard, kZephyrusDialogRadius));
  auto* sl = stats->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(11, 14), 0));
  auto add_stat_col = [&](const std::u16string& value,
                          const std::u16string& caption) {
    auto* c = stats->AddChildView(std::make_unique<views::View>());
    c->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    c->AddChildView(make_label(value, kFg, 3, gfx::Font::Weight::MEDIUM));
    // Sentence case, not ALL CAPS — nothing else in the system shouts.
    c->AddChildView(make_label(caption, kFaint, -1, gfx::Font::Weight::NORMAL));
    sl->SetFlexForView(c, 1);
  };
  add_stat_col(base::NumberToString16(service->total_blocked()),
               u"Blocked total");
  add_stat_col(base::NumberToString16(service->rule_count()), u"Filter rules");

  add_spacer(14);

  // ---- Toggle rows ----------------------------------------------------------
  // Rows highlight on hover at the same lift level and 10px radius as the
  // workspace dropdown's rows, so a settings row behaves identically wherever
  // you meet one. Clicking anywhere on the row flips the switch — a 40px
  // target instead of asking for the toggle itself.
  auto add_toggle = [&](const std::u16string& text, bool is_on,
                        const base::RepeatingCallback<void(bool)>& on_change) {
    auto* row = content->AddChildView(
        std::make_unique<ZephyrusShieldToggleRow>(kRowHover));
    auto* rl = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(7, 10), 8));
    rl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* label = row->AddChildView(
        make_label(text, kFg, 0, gfx::Font::Weight::NORMAL));
    rl->SetFlexForView(label, 1);
    auto* toggle = row->AddChildView(std::make_unique<views::ToggleButton>());
    toggle->SetIsOn(is_on);
    toggle->SetTrackOnColor(kAccent);
    toggle->GetViewAccessibility().SetName(text);
    toggle->SetCallback(base::BindRepeating(
        [](views::ToggleButton* t,
           base::RepeatingCallback<void(bool)> cb) { cb.Run(t->GetIsOn()); },
        base::Unretained(toggle), on_change));
    // ToggleButton::SetIsOn() is programmatic and does not fire the callback,
    // so the row runs the change handler itself.
    row->SetToggle(
        toggle, base::BindRepeating(
                    [](views::ToggleButton* t,
                       base::RepeatingCallback<void(bool)> cb) {
                      t->SetIsOn(!t->GetIsOn());
                      cb.Run(t->GetIsOn());
                    },
                    base::Unretained(toggle), on_change));
  };
  add_toggle(u"Block ads & trackers", service->enabled(),
             base::BindRepeating(
                 [](zephyrus_adblock::ZephyrusAdblockService* s,
                    bool on) { s->SetEnabled(on); },
                 base::Unretained(service)));
  add_spacer(4);
  add_toggle(u"Aggressive pop-up blocking",
             service->aggressive_popup_blocking(),
             base::BindRepeating(
                 [](zephyrus_adblock::ZephyrusAdblockService* s,
                    bool on) { s->SetAggressivePopupBlocking(on); },
                 base::Unretained(service)));
  add_spacer(4);
  add_toggle(u"Block third-party cookies",
             service->third_party_cookie_blocking(),
             base::BindRepeating(
                 [](zephyrus_adblock::ZephyrusAdblockService* s,
                    bool on) { s->SetThirdPartyCookieBlocking(on); },
                 base::Unretained(service)));

  // Advanced: force encrypted DNS (DoH "secure"). DoH is a single browser-wide
  // setting — it can't be scoped to one workspace — so this affects every tab.
  // Default is "automatic" (DoH when the network supports it, safe fallback);
  // "secure" always uses DoH and fails closed, which can break captive-portal
  // and DoH-blocking networks. Off the record, this pref reads through to local
  // state, so read/write the real local state directly.
  if (PrefService* local_state = g_browser_process->local_state()) {
    const bool secure_dns =
        local_state->GetString(prefs::kDnsOverHttpsMode) ==
        SecureDnsConfig::kModeSecure;
    add_toggle(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_ALWAYS_ENCRYPTED_DNS), secure_dns,
        base::BindRepeating([](bool on) {
          if (PrefService* ls = g_browser_process->local_state()) {
            ls->SetString(prefs::kDnsOverHttpsMode,
                          on ? SecureDnsConfig::kModeSecure
                             : SecureDnsConfig::kModeAutomatic);
          }
        }));
  }

  content->SetPreferredSize(
      gfx::Size(kWidth, content->GetHeightForWidth(kWidth)));

  auto bubble = std::make_unique<views::BubbleDialogDelegate>(
      zephyrus_adblock_button_, views::BubbleBorder::TOP_RIGHT,
      views::BubbleBorder::STANDARD_SHADOW, /*autosize=*/true);
  bubble->SetShowCloseButton(false);
  bubble->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble->set_margins(gfx::Insets());
  zephyrus::ConfigureBubble(bubble.get());
  bubble->SetBackgroundColor(kCardBg);
  bubble->SetContentsView(std::move(content));
  views::BubbleDialogDelegate* bubble_ptr = bubble.get();
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  zephyrus::ApplyBubbleFrame(bubble_ptr);
  widget->Show();
  AnimateZephyrusBubbleIn(widget);
}

void ToolbarView::UpdateZephyrusWindowControls() {
  if (!zephyrus_maximize_button_) {
    return;
  }
  views::Widget* widget = GetWidget();
  const bool maximized = widget && widget->IsMaximized();
  auto* maximize =
      static_cast<ZephyrusWin11CaptionButton*>(zephyrus_maximize_button_.get());
  maximize->SetMaximized(maximized);
  const std::u16string maximize_name = l10n_util::GetStringUTF16(
      maximized ? IDS_APP_ACCNAME_RESTORE : IDS_APP_ACCNAME_MAXIMIZE);
  maximize->GetViewAccessibility().SetName(maximize_name);
  maximize->SetTooltipText(maximize_name);
}

// Page-colored dropdown listing workspaces, anchored to the title-bar button.
// At global scope (not anonymous) so BubbleDialogDelegateView can friend it.
// Figma (Workspaces_dropdown): a row whose grey pill background and trailing
// controls ("edit", ✕) appear on hover — always shown for the active row.
class ZephyrusHoverRevealRow : public views::View {
  METADATA_HEADER(ZephyrusHoverRevealRow, views::View)

 public:
  ZephyrusHoverRevealRow(SkColor pill_color, bool always_on)
      : pill_color_(pill_color), always_on_(always_on) {
    SetNotifyEnterExitOnChild(true);
  }

  // Registers a trailing control revealed on hover; call after adding it.
  void AddRevealView(views::View* view) {
    reveal_views_.push_back(view);
    view->SetVisible(always_on_);
  }

  // Applies the resting visuals once construction is done.
  void FinishInit() { UpdateVisuals(false); }

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override {
    UpdateVisuals(true);
  }
  void OnMouseExited(const ui::MouseEvent& event) override {
    UpdateVisuals(false);
  }

 private:
  void UpdateVisuals(bool hovered) {
    const bool on = hovered || always_on_;
    SetBackground(on ? views::CreateRoundedRectBackground(pill_color_, 7.0f)
                     : nullptr);  // Figma row-highlight radius.
    for (views::View* view : reveal_views_) {
      view->SetVisible(on);
    }
    InvalidateLayout();
    SchedulePaint();
  }

  const SkColor pill_color_;
  const bool always_on_;
  std::vector<raw_ptr<views::View>> reveal_views_;
};

BEGIN_METADATA(ZephyrusHoverRevealRow)
END_METADATA

// An elevated card that paints a real soft drop shadow (Figma Edit_Workspace:
// the edited workspace "comes out" wider than the dropdown with a shadow).
// Reserves |kMargin| padding on every side so the shadow isn't clipped and so
// the card can overflow past the dropdown's row column; children lay out
// inside that padding.
class ZephyrusEditCardView : public views::View {
  METADATA_HEADER(ZephyrusEditCardView, views::View)

 public:
  static constexpr int kMargin = 10;  // shadow bleed + overflow room.
  static constexpr float kRadius = 7.0f;

  explicit ZephyrusEditCardView(SkColor fill) : fill_(fill) {
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(3, kMargin)));
  }

  // views::View:
  void OnPaintBackground(gfx::Canvas* canvas) override {
    gfx::RectF card(GetContentsBounds());
    card.Inset(gfx::InsetsF::VH(-3, -(kMargin - 4)));  // card bleeds past text.
    std::vector<gfx::ShadowValue> shadows;
    shadows.emplace_back(gfx::Vector2d(0, 4), 16,
                         SkColorSetA(SK_ColorBLACK, 0x66));
    shadows.emplace_back(gfx::Vector2d(0, 1), 4,
                         SkColorSetA(SK_ColorBLACK, 0x40));
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill_);
    flags.setLooper(gfx::CreateShadowDrawLooper(shadows));
    canvas->DrawRoundRect(card, kRadius, flags);
    // Hairline top edge to lift it off the panel.
    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    stroke.setStrokeWidth(1);
    stroke.setColor(color_utils::BlendTowardMaxContrast(fill_, 0x20));
    canvas->DrawRoundRect(card, kRadius, stroke);
  }

 private:
  const SkColor fill_;
};

BEGIN_METADATA(ZephyrusEditCardView)
END_METADATA

// Borderless underlined rename field on the edit card. Textfield's colors are
// ColorId-bound (the forced-dark theme resolves text white + a dark
// background), so both the ink and the fill are re-applied dynamically after
// every theme update so the field is a transparent-looking, correctly-inked
// input on the card surface.
class ZephyrusInlineNameField : public views::Textfield {
  METADATA_HEADER(ZephyrusInlineNameField, views::Textfield)

 public:
  // |ink| paints the text; |fill| matches the card so the field reads as
  // transparent.
  void SetZephyrusColors(SkColor ink, SkColor fill) {
    ink_ = ink;
    fill_ = fill;
    ApplyColors();
  }

  // views::Textfield:
  void OnThemeChanged() override {
    views::Textfield::OnThemeChanged();
    ApplyColors();
  }

 private:
  void ApplyColors() {
    GetRenderText()->SetColor(ink_);
    SetBackgroundColor(fill_);  // ColorVariant from SkColor; matches the card.
    SchedulePaint();
  }

  SkColor ink_ = SkColorSetRGB(0x1a, 0x1a, 0x1e);
  SkColor fill_ = SK_ColorWHITE;
};

BEGIN_METADATA(ZephyrusInlineNameField)
END_METADATA

// LabelButton with its label() accessor exposed (protected upstream), so the
// menu can underline the "edit" link per the Figma mockup.
// A compact text glyph button whose hover affordances match the Figma:
// "edit" underlines only on hover; ✕ gets a rounded square fill only on hover.
class ZephyrusGlyphButton : public views::LabelButton {
  METADATA_HEADER(ZephyrusGlyphButton, views::LabelButton)

 public:
  using views::LabelButton::LabelButton;
  using views::LabelButton::label;

  void SetUnderlineOnHover(bool on) {
    underline_on_hover_ = on;
    ApplyHoverState();
  }
  // |fill| is painted (radius |radius|) only while hovered/pressed.
  void SetHoverFill(SkColor fill, float radius) {
    hover_fill_ = fill;
    hover_radius_ = radius;
    has_hover_fill_ = true;
    ApplyHoverState();
  }

  // views::LabelButton:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    ApplyHoverState();
  }

 private:
  bool IsHot() const {
    return GetState() == views::Button::STATE_HOVERED ||
           GetState() == views::Button::STATE_PRESSED;
  }
  void ApplyHoverState() {
    const bool hot = IsHot();
    if (underline_on_hover_) {
      label()->SetFontList(hot ? label()->font_list().DeriveWithStyle(
                                     gfx::Font::UNDERLINE)
                               : label()->font_list().DeriveWithStyle(
                                     gfx::Font::NORMAL));
    }
    if (has_hover_fill_) {
      SetBackground(hot ? views::CreateRoundedRectBackground(hover_fill_,
                                                             hover_radius_)
                        : nullptr);
    }
  }

  bool underline_on_hover_ = false;
  bool has_hover_fill_ = false;
  SkColor hover_fill_ = SK_ColorTRANSPARENT;
  float hover_radius_ = 0.0f;
};

BEGIN_METADATA(ZephyrusGlyphButton)
END_METADATA

class ZephyrusWorkspaceMenu : public views::BubbleDialogDelegateView,
                              public gfx::AnimationDelegate,
                              public views::TextfieldController {
  METADATA_HEADER(ZephyrusWorkspaceMenu, views::BubbleDialogDelegateView)

 public:
  ZephyrusWorkspaceMenu(views::View* anchor,
                        ZephyrusWorkspaceManager* manager,
                        std::optional<SkColor> page_color,
                        base::RepeatingClosure on_action,
                        base::RepeatingClosure on_closed)
      : views::BubbleDialogDelegateView(anchor,
                                        views::BubbleBorder::TOP_LEFT),
        manager_(manager),
        on_action_(std::move(on_action)),
        on_closed_(std::move(on_closed)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_margins(gfx::Insets(8));
    zephyrus::ConfigureBubble(this);  // Figma Workspaces_dropdown card radius.
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));

    // Figma Workspaces_dropdown, colored by the SAME dynamic-theme model as
    // the title bar (page color + progressively stronger subtle overlay), so
    // the card, its hover rows, and the lifted edit card all belong to the
    // chameleon surface. Ink contrasts with the page.
    const SkColor base = page_color.value_or(SkColorSetRGB(0x16, 0x16, 0x18));
    const bool dark = color_utils::IsDark(base);
    const SkColor overlay = dark ? SK_ColorWHITE : SK_ColorBLACK;
    auto lift = [&](SkAlpha a) {
      return color_utils::AlphaBlend(overlay, base, a);
    };
    panel_ = lift(dark ? 0x22 : 0x18);
    foreground_ = color_utils::GetColorWithMaxContrast(base);
    row_hover_ = lift(dark ? 0x3A : 0x2C);
    edit_card_ = lift(dark ? 0x5A : 0x44);
    SetBackgroundColor(panel_);

    expand_animation_.SetSlideDuration(base::Milliseconds(220));
    expand_animation_.SetTweenType(gfx::Tween::EASE_OUT_3);

    RebuildList();
  }

  ~ZephyrusWorkspaceMenu() override {
    if (on_closed_) {
      on_closed_.Run();
    }
  }

  // views::WidgetDelegate: the bubble frame exists by now, which is what
  // ApplyBubbleFrame() needs.
  void OnWidgetInitialized() override {
    views::BubbleDialogDelegateView::OnWidgetInitialized();
    zephyrus::ApplyBubbleFrame(this);
  }

  // The Figma "full width jump": the card starts at the anchor pill's width
  // and glides to its full width (left edge fixed at the anchor).
  void StartZephyrusEntrance(int anchor_width) {
    views::Widget* widget = GetWidget();
    if (!widget || !gfx::Animation::ShouldRenderRichAnimation()) {
      return;
    }
    final_bounds_ = widget->GetWindowBoundsInScreen();
    start_width_ = std::min(final_bounds_.width(),
                            std::max(anchor_width, 60));
    expand_animation_.Show();
    AnimationProgressed(&expand_animation_);
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    views::Widget* widget = GetWidget();
    if (!widget) {
      return;
    }
    gfx::Rect bounds = final_bounds_;
    bounds.set_width(gfx::Tween::IntValueBetween(
        animation->GetCurrentValue(), start_width_, final_bounds_.width()));
    widget->SetBounds(bounds);
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    if (views::Widget* widget = GetWidget()) {
      widget->SetBounds(final_bounds_);
    }
  }

 private:
  static constexpr int kRowWidth = 218;

  // A small circular color swatch.
  std::unique_ptr<views::View> MakeDot(SkColor color, int diameter) {
    auto dot = std::make_unique<views::View>();
    dot->SetPreferredSize(gfx::Size(diameter, diameter));
    dot->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetA(color, SK_AlphaOPAQUE), diameter / 2.0f));
    return dot;
  }

  // A compact glyph button ("edit" / ✕ / ✓) with adaptive foreground.
  ZephyrusGlyphButton* AddGlyphButton(views::View* parent,
                                      const std::u16string& glyph,
                                      base::RepeatingClosure action) {
    auto* button = parent->AddChildView(std::make_unique<ZephyrusGlyphButton>(
        base::BindRepeating(
            [](ZephyrusWorkspaceMenu* self, base::RepeatingClosure a,
               const ui::Event&) { a.Run(); },
            base::Unretained(this), std::move(action)),
        glyph));
    button->SetTextColor(views::Button::STATE_NORMAL,
                         SkColorSetA(foreground_, 0xB0));
    button->SetTextColor(views::Button::STATE_HOVERED, foreground_);
    button->SetMinSize(gfx::Size(30, 30));
    return button;
  }

  void Relayout() {
    if (GetWidget()) {
      SizeToContents();
    }
  }

  // ---- List mode -----------------------------------------------------------
  void RebuildList() {
    edit_card_view_ = nullptr;
    name_field_ = nullptr;
    RemoveAllChildViews();

    // In Private Workspace the dropdown offers exactly one thing: the way out.
    // The normal workspaces belong to a different profile and must not be
    // listed, switched to, edited, deleted, or created from here — the only
    // door back to them is Exit.
    if (ZephyrusPrivateWorkspace::IsPrivate(
            manager_ ? manager_->browser() : nullptr)) {
      auto* exit_row = AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&ZephyrusWorkspaceMenu::OnTogglePrivate,
                              base::Unretained(this)),
          u"←  " + l10n_util::GetStringUTF16(IDS_ZEPHYRUS_EXIT_PRIVATE_WORKSPACE)));
      exit_row->SetTextColor(views::Button::STATE_NORMAL, foreground_);
      exit_row->SetTextColor(views::Button::STATE_HOVERED, foreground_);
      exit_row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      exit_row->SetMinSize(gfx::Size(kRowWidth, 34));
      Relayout();
      return;
    }

    const auto& workspaces = manager_->workspaces();
    const bool can_delete = workspaces.size() > 1;
    // Dynamic-theme hover pill + trailing "edit"/✕ appear on hover (always on
    // the active row).
    for (const ZephyrusWorkspaceManager::Workspace& ws : workspaces) {
      const bool active = ws.id == manager_->current_workspace_id();
      auto* row = AddChildView(
          std::make_unique<ZephyrusHoverRevealRow>(row_hover_, active));
      auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8), 8));
      layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      row->SetPreferredSize(gfx::Size(kRowWidth, 30));

      row->AddChildView(MakeDot(ws.color, 10));

      const std::u16string text =
          ws.emoji.empty() ? ws.name : (ws.emoji + u"  " + ws.name);
      auto* switch_button =
          row->AddChildView(std::make_unique<views::LabelButton>(
              base::BindRepeating(
                  &ZephyrusWorkspaceMenu::OnSwitch, base::Unretained(this),
                  ws.id),
              text));
      switch_button->SetTextColor(views::Button::STATE_NORMAL, foreground_);
      switch_button->SetTextColor(views::Button::STATE_HOVERED, foreground_);
      switch_button->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      layout->SetFlexForView(switch_button, 1);

      // Figma: "edit" is a small grey link that underlines only on hover; ✕
      // gets a rounded-3px square fill only on hover. Both appear on row hover.
      ZephyrusGlyphButton* edit = AddGlyphButton(
          row, u"edit",
          base::BindRepeating(&ZephyrusWorkspaceMenu::RebuildEditor,
                              base::Unretained(this), ws.id));
      edit->SetUnderlineOnHover(true);
      row->AddRevealView(edit);
      if (can_delete) {
        ZephyrusGlyphButton* close = AddGlyphButton(
            row, u"✕",
            base::BindRepeating(&ZephyrusWorkspaceMenu::OnDelete,
                                base::Unretained(this), ws.id));
        // Neutral square that reads on the card, appearing only on hover.
        close->SetHoverFill(
            color_utils::BlendTowardMaxContrast(panel_, 0x40), 3.0f);
        close->SetMinSize(gfx::Size(22, 22));
        row->AddRevealView(close);
      }
      row->FinishInit();
    }

    auto* add_row = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&ZephyrusWorkspaceMenu::OnAddWorkspace,
                            base::Unretained(this)),
        u"＋  New workspace"));
    add_row->SetTextColor(views::Button::STATE_NORMAL, foreground_);
    add_row->SetTextColor(views::Button::STATE_HOVERED, foreground_);
    add_row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    add_row->SetMinSize(gfx::Size(kRowWidth, 34));

    // Private Workspace is not one of the stored workspaces — it lives on the
    // OTR profile, which has its own store entirely — so it is a synthetic row
    // rather than an entry in `workspaces`. A hairline sets it apart, because
    // choosing it changes which profile you are browsing in, not just which
    // tabs you see.
    auto* rule = AddChildView(std::make_unique<views::View>());
    rule->SetPreferredSize(gfx::Size(kRowWidth, 1));
    rule->SetProperty(views::kMarginsKey, gfx::Insets::VH(5, 0));
    rule->SetBackground(
        views::CreateSolidBackground(SkColorSetA(foreground_, 0x1F)));

    Browser* const browser = manager_ ? manager_->browser() : nullptr;
    const bool in_private = ZephyrusPrivateWorkspace::IsPrivate(browser);
    auto* private_row = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&ZephyrusWorkspaceMenu::OnTogglePrivate,
                            base::Unretained(this)),
        in_private ? l10n_util::GetStringUTF16(IDS_ZEPHYRUS_LEAVE_PRIVATE_WORKSPACE)
                   : l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVATE_WORKSPACE)));
    private_row->SetTextColor(views::Button::STATE_NORMAL, foreground_);
    private_row->SetTextColor(views::Button::STATE_HOVERED, foreground_);
    // Dedicated lock icon as the leading mark, replacing the 🔒 emoji.
    private_row->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(kZephyrusPrivateWorkspaceIcon,
                                       foreground_, 14));
    private_row->SetImageLabelSpacing(8);
    private_row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    private_row->SetMinSize(gfx::Size(kRowWidth, 34));

    // The lock toggle sits directly under the row it governs, so the setting is
    // found where the decision is made rather than buried in settings.
    const bool lock_on =
        browser && ZephyrusPrivateWorkspace::IsLockEnabled(browser->profile());
    auto* lock_row = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&ZephyrusWorkspaceMenu::OnToggleLock,
                            base::Unretained(this)),
        (lock_on ? u"✓  " : u"     ") +
            l10n_util::GetStringUTF16(IDS_ZEPHYRUS_REQUIRE_UNLOCK)));
    // Touch-ID/biometric lock as the leading mark; dimmed when the setting is
    // off, matching the label's own on/off alpha.
    const SkColor lock_ink = SkColorSetA(foreground_, lock_on ? 0xFF : 0xB0);
    lock_row->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(kZephyrusTouchIdIcon, lock_ink, 15));
    lock_row->SetImageLabelSpacing(8);
    lock_row->SetTextColor(views::Button::STATE_NORMAL, lock_ink);
    lock_row->SetTextColor(views::Button::STATE_HOVERED, foreground_);
    lock_row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    lock_row->SetMinSize(gfx::Size(kRowWidth, 30));

    // PW-6 experiment: opens a private tab IN THIS WINDOW instead of swapping
    // to a second one. Kept alongside the working window-swap row rather than
    // replacing it, so a fault here doesn't cost the feature that works.
    auto* inline_row = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&ZephyrusWorkspaceMenu::OnOpenPrivateTabInWindow,
                            base::Unretained(this)),
        u"⚗  Private tab here (test)"));
    inline_row->SetTextColor(views::Button::STATE_NORMAL,
                             SkColorSetA(foreground_, 0xB0));
    inline_row->SetTextColor(views::Button::STATE_HOVERED, foreground_);
    inline_row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    inline_row->SetMinSize(gfx::Size(kRowWidth, 30));
    Relayout();
  }

  // ---- Inline edit mode (Figma Edit_Workspace) ----------------------------
  void RebuildEditor(int workspace_id) {
    const ZephyrusWorkspaceManager::Workspace* ws =
        manager_->GetWorkspace(workspace_id);
    if (!ws) {
      RebuildList();
      return;
    }
    editing_id_ = workspace_id;
    emoji_field_ = nullptr;
    RemoveAllChildViews();

    // Same list, but the edited row becomes an underlined name field with a
    // dark square check to confirm - rename happens in place, per the mockup.
    for (const ZephyrusWorkspaceManager::Workspace& item :
         manager_->workspaces()) {
      auto* row = AddChildView(std::make_unique<views::View>());
      auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8),
          8));
      layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      row->SetPreferredSize(gfx::Size(kRowWidth, 30));
      if (item.id != workspace_id) {
        row->AddChildView(MakeDot(item.color, 10));
        auto* label = row->AddChildView(std::make_unique<views::Label>(
            item.emoji.empty() ? item.name
                               : (item.emoji + u"  " + item.name)));
        label->SetEnabledColor(SkColorSetA(foreground_, 0x8C));
        label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        label->SetAutoColorReadabilityEnabled(false);
        label->SetSubpixelRenderingEnabled(false);
        layout->SetFlexForView(label, 1);
        continue;
      }
      // Figma Edit_Workspace: the edited row lifts into an elevated card that
      // overflows wider than the dropdown with a real drop shadow, popping
      // open from the row's small width and expanding. The card is a
      // ZephyrusEditCardView (shadow + overflow margin) on its own layer.
      row->SetUseDefaultFillLayout(true);
      auto* card = row->AddChildView(
          std::make_unique<ZephyrusEditCardView>(edit_card_));
      edit_card_view_ = card;
      card->SetPaintToLayer();
      card->layer()->SetFillsBoundsOpaquely(false);
      auto* card_layout =
          card->SetLayoutManager(std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal,
              gfx::Insets::VH(0, 4), 8));
      card_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      // Overflow: the card is wider than the row column so it "comes out".
      row->SetPreferredSize(
          gfx::Size(kRowWidth + 2 * ZephyrusEditCardView::kMargin, 40));

      auto* field = card->AddChildView(
          std::make_unique<ZephyrusInlineNameField>());
      name_field_ = field;
      field->SetText(item.name);
      const SkColor ink = color_utils::GetColorWithMaxContrast(edit_card_);
      field->SetZephyrusColors(ink, edit_card_);
      field->SetBorder(views::CreateSolidSidedBorder(
          gfx::Insets::TLBR(0, 0, 1, 0), SkColorSetA(ink, 0xC0)));
      name_field_->set_controller(this);
      name_field_->GetViewAccessibility().SetName(u"Workspace name");
      card_layout->SetFlexForView(name_field_, 1);

      auto* done = card->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&ZephyrusWorkspaceMenu::OnDoneEditing,
                              base::Unretained(this)),
          u"✓"));
      const SkColor done_fill = color_utils::BlendTowardMaxContrast(edit_card_,
                                                                    0xC8);
      done->SetTextColor(views::Button::STATE_NORMAL,
                         color_utils::GetColorWithMaxContrast(done_fill));
      done->SetTextColor(views::Button::STATE_HOVERED,
                         color_utils::GetColorWithMaxContrast(done_fill));
      done->SetBackground(views::CreateRoundedRectBackground(done_fill, 3.0f));
      done->SetMinSize(gfx::Size(22, 22));
      done->GetViewAccessibility().SetName(u"Confirm rename");
    }

    Relayout();
    // Pop the edit card open: it expands from ~72% width (the hover-highlight
    // footprint) about its left edge, with a quick fade — "comes out in a
    // card" per the design.
    if (edit_card_view_ && edit_card_view_->layer() &&
        gfx::Animation::ShouldRenderRichAnimation()) {
      ui::Layer* layer = edit_card_view_->layer();
      gfx::Transform small;
      small.Scale(0.72, 1.0);
      layer->SetOpacity(0.0f);
      layer->SetTransform(small);
      ui::ScopedLayerAnimationSettings s(layer->GetAnimator());
      s.SetTransitionDuration(base::Milliseconds(200));
      s.SetTweenType(gfx::Tween::EASE_OUT_3);
      layer->SetOpacity(1.0f);
      layer->SetTransform(gfx::Transform());
    }
    if (name_field_) {
      name_field_->RequestFocus();
      name_field_->SelectAll(false);
    }
  }

  // views::TextfieldController: Enter commits the inline rename.
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override {
    if (key_event.type() == ui::EventType::kKeyPressed &&
        key_event.key_code() == ui::VKEY_RETURN) {
      OnDoneEditing(key_event);
      return true;
    }
    return false;
  }

  // ---- Actions -------------------------------------------------------------
  void OnSwitch(int workspace_id, const ui::Event&) {
    manager_->SwitchToWorkspace(workspace_id);
    Finish();
  }

  void OnOpenPrivateTabInWindow(const ui::Event&) {
    Browser* const browser = manager_ ? manager_->browser() : nullptr;
    if (!browser) {
      return;
    }
    auto* controller =
        ZephyrusPrivateWorkspace::GetForProfile(browser->profile());
    if (!controller) {
      return;
    }
    // Close the dropdown first — the new tab activates and rebuilds the strip.
    Finish();
    controller->OpenPrivateTabIn(browser);
  }

  void OnToggleLock(const ui::Event&) {
    Browser* const browser = manager_ ? manager_->browser() : nullptr;
    if (!browser) {
      return;
    }
    Profile* profile = browser->profile();
    ZephyrusPrivateWorkspace::SetLockEnabled(
        profile, !ZephyrusPrivateWorkspace::IsLockEnabled(profile));
    RebuildList();  // Reflect the new state without closing the dropdown.
  }

  void OnTogglePrivate(const ui::Event&) {
    Browser* const browser = manager_ ? manager_->browser() : nullptr;
    if (!browser) {
      return;
    }
    auto* controller =
        ZephyrusPrivateWorkspace::GetForProfile(browser->profile());
    if (!controller) {
      return;
    }
    const bool in_private = ZephyrusPrivateWorkspace::IsPrivate(browser);
    // Close the dropdown before swapping windows: this bubble belongs to the
    // window that is about to be hidden.
    Finish();
    if (in_private) {
      controller->Leave();
    } else {
      controller->Enter(browser);
    }
  }

  void OnAddWorkspace(const ui::Event&) {
    manager_->AddWorkspace();
    Finish();
  }

  void OnDelete(int workspace_id) {
    // Deleting closes every tab in the workspace, so confirm first. Counting
    // them lets the warning name the actual cost instead of a vague caution.
    if (!manager_) {
      return;
    }
    Browser* browser = manager_->browser();
    int tab_count = 0;
    if (browser) {
      TabStripModel* model = browser->tab_strip_model();
      for (int i = 0; i < model->count(); ++i) {
        if (manager_->GetWorkspaceForContents(model->GetWebContentsAt(i)) ==
            workspace_id) {
          ++tab_count;
        }
      }
    }
    const ZephyrusWorkspaceManager::Workspace* workspace =
        manager_->GetWorkspace(workspace_id);
    const std::u16string name = workspace ? workspace->name : u"this workspace";
    const SkColor panel = panel_;
    const SkColor foreground = foreground_;
    base::WeakPtr<ZephyrusWorkspaceManager> manager = manager_->GetWeakPtr();
    // Close the dropdown first: the dialog is modal, and leaving the bubble
    // open behind it traps focus. Finish() (not on_action_, which only
    // refreshes the toolbar pill) is what actually closes the widget, so
    // everything the dialog needs is copied out above, before this point.
    Finish();
    // Posted so the modal is created after the dropdown's widget teardown has
    // finished, rather than during it. The Browser is re-resolved through the
    // weak manager rather than captured raw, so a window closed in between
    // can't be dereferenced.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<ZephyrusWorkspaceManager> manager,
               std::u16string name, int tab_count, SkColor panel,
               SkColor foreground, int id) {
              if (!manager || !manager->browser()) {
                return;
              }
              ShowZephyrusDeleteWorkspaceDialog(
                  manager->browser(), name, tab_count, panel, foreground,
                  base::BindOnce(
                      [](base::WeakPtr<ZephyrusWorkspaceManager> manager,
                         int id) {
                        if (manager) {
                          manager->DeleteWorkspace(id);
                        }
                      },
                      manager, id));
            },
            manager, name, tab_count, panel, foreground, workspace_id));
  }

  void OnPickColor(SkColor color) {
    if (editing_id_) {
      manager_->SetWorkspaceColor(editing_id_, color);
      on_action_.Run();
    }
  }

  void OnDoneEditing(const ui::Event&) {
    if (editing_id_ && name_field_) {
      std::u16string name(name_field_->GetText());
      if (!name.empty()) {
        manager_->RenameWorkspace(editing_id_, name);
      }
      if (emoji_field_) {
        manager_->SetWorkspaceEmoji(editing_id_,
                                    std::u16string(emoji_field_->GetText()));
      }
      on_action_.Run();
    }
    name_field_ = nullptr;
    emoji_field_ = nullptr;
    editing_id_ = 0;
    RebuildList();
  }

  void Finish() {
    on_action_.Run();
    if (views::Widget* widget = GetWidget()) {
      widget->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
    }
  }

  raw_ptr<ZephyrusWorkspaceManager> manager_;
  base::RepeatingClosure on_action_;
  base::RepeatingClosure on_closed_;
  SkColor foreground_ = SK_ColorWHITE;
  SkColor panel_ = SkColorSetRGB(0x28, 0x28, 0x2c);
  SkColor row_hover_ = SkColorSetA(SK_ColorWHITE, 0x1A);
  SkColor edit_card_ = SK_ColorWHITE;
  int editing_id_ = 0;
  raw_ptr<views::Textfield> name_field_ = nullptr;
  raw_ptr<views::Textfield> emoji_field_ = nullptr;
  raw_ptr<views::View> edit_card_view_ = nullptr;
  // Figma full-width-jump entrance (see StartZephyrusEntrance).
  gfx::SlideAnimation expand_animation_{this};
  gfx::Rect final_bounds_;
  int start_width_ = 0;
};

BEGIN_METADATA(ZephyrusWorkspaceMenu)
END_METADATA

// Title-bar workspace switcher: workspace name followed by a trailing dropdown
// chevron (views::LabelButton can't place an image after the label, so this is
// a Button hosting a Label + trailing ImageView).
class ZephyrusWorkspaceButton : public views::Button {
  METADATA_HEADER(ZephyrusWorkspaceButton, views::Button)

 public:
  explicit ZephyrusWorkspaceButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(4, 12), 6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    // Leading mark. Only shown for Private Workspace, where it carries the
    // dedicated lock icon in place of a prepended emoji.
    lock_view_ = AddChildView(std::make_unique<views::ImageView>());
    lock_view_->SetVisible(false);
    label_ = AddChildView(std::make_unique<views::Label>());
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetSubpixelRenderingEnabled(false);
    // Shown when a workspace you can't see is playing audio, so the noise is
    // attributable without opening the sidebar.
    audio_view_ = AddChildView(std::make_unique<views::ImageView>());
    audio_view_->SetVisible(false);
    chevron_ = AddChildView(std::make_unique<views::ImageView>());
  }

  // Shows the dedicated Private Workspace lock icon as a leading mark.
  void SetPrivateMark(bool active, SkColor foreground) {
    lock_view_->SetVisible(active);
    if (active) {
      lock_view_->SetImage(ui::ImageModel::FromVectorIcon(
          kZephyrusPrivateWorkspaceIcon, foreground, 13));
    }
  }

  void SetAudioActive(bool active, SkColor foreground) {
    audio_view_->SetVisible(active);
    if (active) {
      audio_view_->SetImage(ui::ImageModel::FromVectorIcon(
          vector_icons::kVolumeUpIcon, foreground, 12));
      audio_view_->SetTooltipText(u"Another workspace is playing audio");
    }
  }

  void SetContent(const std::u16string& text, SkColor foreground) {
    label_->SetText(text);
    label_->SetEnabledColor(foreground);
    chevron_->SetImage(ui::ImageModel::FromVectorIcon(kZephyrusDropdownIcon,
                                                      foreground, 10));
    const std::u16string accessible =
        text.empty() ? u"Switch workspace" : text;
    GetViewAccessibility().SetName(accessible);
    SetTooltipText(u"Switch workspace");
  }

  // Figma: the chevron points up while the dropdown is open.
  void SetMenuOpen(bool open) {
    if (menu_open_ == open) {
      return;
    }
    menu_open_ = open;
    if (!chevron_->layer()) {
      chevron_->SetPaintToLayer();
      chevron_->layer()->SetFillsBoundsOpaquely(false);
    }
    gfx::Transform flip;
    if (open) {
      flip.Translate(0, chevron_->height());
      flip.Scale(1, -1);
    }
    chevron_->SetTransform(flip);
  }

  // Pill fills for each interaction state; the hover/pressed fills give the
  // button the press feedback it previously lacked entirely.
  void SetPillFills(SkColor normal, SkColor hovered, SkColor pressed) {
    normal_fill_ = normal;
    hovered_fill_ = hovered;
    pressed_fill_ = pressed;
    UpdatePill();
  }

  // views::Button:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    UpdatePill();
  }

 private:
  void UpdatePill() {
    SkColor fill = normal_fill_;
    if (GetState() == views::Button::STATE_PRESSED) {
      fill = pressed_fill_;
    } else if (GetState() == views::Button::STATE_HOVERED) {
      fill = hovered_fill_;
    }
    // Figma Workspaces button: 10px rounded rect (not a full pill).
    SetBackground(views::CreateRoundedRectBackground(fill, 10.0f));
  }

  raw_ptr<views::ImageView> lock_view_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::ImageView> audio_view_ = nullptr;
  raw_ptr<views::ImageView> chevron_ = nullptr;
  SkColor normal_fill_ = SK_ColorTRANSPARENT;
  SkColor hovered_fill_ = SK_ColorTRANSPARENT;
  SkColor pressed_fill_ = SK_ColorTRANSPARENT;
  bool menu_open_ = false;
};

BEGIN_METADATA(ZephyrusWorkspaceButton)
END_METADATA

void ToolbarView::AddZephyrusWorkspaceButton() {
  auto button = std::make_unique<ZephyrusWorkspaceButton>(base::BindRepeating(
      [](ToolbarView* toolbar) { toolbar->ShowZephyrusWorkspaceMenu(); },
      base::Unretained(this)));
  button->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, 6));
  // Place it just to the right of the new-tab (+) button.
  std::optional<size_t> new_tab_index =
      zephyrus_new_tab_button_ ? GetIndexOf(zephyrus_new_tab_button_)
                               : std::nullopt;
  const size_t position = new_tab_index ? *new_tab_index + 1 : 0;
  zephyrus_workspace_button_ =
      AddChildViewAt<views::Button>(std::move(button), position);
  // Follow the manager, not just the menu. The workspace can change without the
  // menu being involved (closing a workspace's last tab, moving a tab away,
  // switching by keyboard), and without this the pill keeps showing the old
  // name while the dropdown shows the real one.
  if (BrowserView* browser_view =
          BrowserView::GetBrowserViewForBrowser(browser_)) {
    if (ZephyrusWorkspaceManager* manager =
            browser_view->zephyrus_workspace_manager()) {
      zephyrus_workspace_changed_subscription_ =
          manager->RegisterChangedCallback(base::BindRepeating(
              &ToolbarView::UpdateZephyrusWorkspaceButton,
              base::Unretained(this)));
    }
  }
  // Audio starting/stopping isn't a workspace change, so poll for it. The walk
  // is O(windows x tabs) PER window holding a pill, so the cadence is kept
  // modest — a hidden-workspace audio badge that appears within 5s is fine,
  // and this halves-again a constant background cost that battery pays for.
  zephyrus_audio_poll_timer_.Start(
      FROM_HERE, base::Seconds(5),
      base::BindRepeating(&ToolbarView::UpdateZephyrusWorkspaceButton,
                          base::Unretained(this)));
  UpdateZephyrusWorkspaceButton();
}

void ToolbarView::UpdateZephyrusWorkspaceButton() {
  if (!zephyrus_workspace_button_) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  ZephyrusWorkspaceManager* manager =
      browser_view ? browser_view->zephyrus_workspace_manager() : nullptr;
  std::u16string label;
  const bool is_private = ZephyrusPrivateWorkspace::IsPrivate(browser_);
  if (is_private) {
    // The private window has its own OTR workspace store, so `manager` here
    // would report that store's default ("Workspace 1") — which would actively
    // mislead about which profile you are browsing in. The lock icon is set
    // separately below; the label is just the name now.
    label = l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVATE_WORKSPACE);
  } else if (manager) {
    const ZephyrusWorkspaceManager::Workspace* current =
        manager->GetWorkspace(manager->current_workspace_id());
    label = manager->current_workspace_name();
    if (current && !current->emoji.empty()) {
      label = current->emoji + u"  " + label;
    }
  }
  // Figma Workspaces button, colored by the SAME dynamic-theme model as the
  // title bar's nav buttons (UpdateZephyrusNavButtonBackgrounds): the page
  // color plus a subtle overlay — light on dark pages, dark on light — so the
  // pill reads as a chip on the chameleon surface, with ink that contrasts
  // with the page.
  const SkColor base =
      zephyrus_titlebar_color_.value_or(SkColorSetRGB(0x16, 0x16, 0x18));
  const bool dark = color_utils::IsDark(base);
  const SkColor overlay = dark ? SK_ColorWHITE : SK_ColorBLACK;
  const SkColor surface =
      color_utils::AlphaBlend(overlay, base, static_cast<SkAlpha>(dark ? 0x22 : 0x18));
  const SkColor surface_hover =
      color_utils::AlphaBlend(overlay, base, static_cast<SkAlpha>(dark ? 0x3A : 0x2C));
  const SkColor surface_press =
      color_utils::AlphaBlend(overlay, base, static_cast<SkAlpha>(dark ? 0x4C : 0x3A));
  const SkColor ink = color_utils::GetColorWithMaxContrast(base);
  auto* pill =
      static_cast<ZephyrusWorkspaceButton*>(zephyrus_workspace_button_.get());
  pill->SetContent(label, ink);
  pill->SetPrivateMark(is_private, ink);
  // Audio state changes don't route through the workspace manager's change
  // notifications, so this is refreshed by a light poll (see the timer in
  // AddZephyrusWorkspaceButton) rather than an event.
  pill->SetAudioActive(manager && manager->HasBackgroundAudio(), ink);
  static_cast<ZephyrusWorkspaceButton*>(zephyrus_workspace_button_.get())
      ->SetPillFills(surface, surface_hover, surface_press);
}

void ToolbarView::ShowZephyrusWorkspaceMenu() {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  ZephyrusWorkspaceManager* manager =
      browser_view ? browser_view->zephyrus_workspace_manager() : nullptr;
  if (!manager || !zephyrus_workspace_button_) {
    return;
  }
  auto menu = std::make_unique<ZephyrusWorkspaceMenu>(
      zephyrus_workspace_button_, manager, zephyrus_titlebar_color_,
      base::BindRepeating(&ToolbarView::UpdateZephyrusWorkspaceButton,
                          base::Unretained(this)),
      base::BindRepeating(
          [](ToolbarView* toolbar) {
            if (toolbar->zephyrus_workspace_button_) {
              static_cast<ZephyrusWorkspaceButton*>(
                  toolbar->zephyrus_workspace_button_.get())
                  ->SetMenuOpen(false);
            }
          },
          base::Unretained(this)));
  ZephyrusWorkspaceMenu* menu_ptr = menu.get();
  views::Widget* menu_widget =
      views::BubbleDialogDelegateView::CreateBubble(std::move(menu));
  menu_widget->Show();
  // Chevron flips up while open; the card plays the Figma width jump, plus a
  // quick fade so the growth reads as an entrance rather than a resize.
  static_cast<ZephyrusWorkspaceButton*>(zephyrus_workspace_button_.get())
      ->SetMenuOpen(true);
  if (ui::Layer* layer = menu_widget->GetLayer();
      layer && gfx::Animation::ShouldRenderRichAnimation()) {
    layer->SetOpacity(0.0f);
    ui::ScopedLayerAnimationSettings fade(layer->GetAnimator());
    fade.SetTransitionDuration(base::Milliseconds(120));
    fade.SetTweenType(gfx::Tween::EASE_OUT);
    layer->SetOpacity(1.0f);
  }
  menu_ptr->StartZephyrusEntrance(zephyrus_workspace_button_->width());
}

void ToolbarView::LayoutCommon() {
  DCHECK(display_mode_ == DisplayMode::kNormal);

  gfx::Insets interior_margin =
      GetLayoutInsets(LayoutInset::TOOLBAR_INTERIOR_MARGIN);

  auto* vts_controller = tabs::VerticalTabStripStateController::From(browser_);
  if (base::FeatureList::IsEnabled(contextual_tasks::kContextualTasks) &&
      (contextual_tasks::kShowEntryPoint.Get() ==
       contextual_tasks::EntryPointOption::kToolbarEphemeralBranded) &&
      (!vts_controller || !vts_controller->ShouldDisplayVerticalTabs())) {
    interior_margin.set_left(0);
  }

  if (app_menu_button_) {
    const bool expanded = app_menu_button_->IsLabelPresentAndVisible();
    if (expanded) {
      // The interior margin in an expanded state should be more than in a
      // collapsed state.
      interior_margin.set_right(interior_margin.right() + 1);
    }
    SetRefreshMargins(app_menu_button_, expanded);
  }

  // The margins of the `avatar_` uses the same constants as the
  // `app_menu_button_`.
  if (avatar_) {
    SetRefreshMargins(avatar_, avatar_->IsLabelPresentAndVisible());
  }

  layout_manager_->SetInteriorMargin(interior_margin);

  // Extend buttons to the window edge if we're either in a maximized or
  // fullscreen window. This makes the buttons easier to hit, see Fitts' law.
  const bool extend_buttons_to_edge =
      browser_->GetWindow() && (browser_->GetWindow()->IsMaximized() ||
                                browser_->GetWindow()->IsFullscreen());
  const int margin = extend_buttons_to_edge ? interior_margin.left() : 0;
  if (features::IsWebUIBackForwardButtonEnabled()) {
    toolbar_webview_->SetBackButtonLeadingMargin(margin);
  } else {
    back_->SetLeadingMargin(margin);
  }

  const int trailing_margin =
      extend_buttons_to_edge ? interior_margin.right() : 0;
  GetAppMenuControl()->SetTrailingMargin(trailing_margin);

  if (toolbar_divider_ && extensions_container_) {
    views::ManualLayoutUtil(layout_manager_)
        .SetViewHidden(toolbar_divider_, !extensions_container_->GetVisible());
  }
  // Cast button visibility is controlled externally.

  UpdateZephyrusWindowControls();
}

// AppMenuIconController::Delegate:
void ToolbarView::UpdateTypeAndSeverity(
    AppMenuIconController::TypeAndSeverity type_and_severity) {
  AppMenuControl* app_menu_control = GetAppMenuControl();
  if (app_menu_control) {
    app_menu_control->SetTypeAndSeverity(type_and_severity);
  }
}

ExtensionsToolbarDesktop* ToolbarView::GetExtensionsToolbarDesktop() {
  return extensions_container_;
}

PinnedToolbarActions* ToolbarView::GetPinnedToolbarActions() {
  return pinned_toolbar_actions_;
}

gfx::Size ToolbarView::GetToolbarButtonSize() const {
  // Since DisplayMode::kLocation is for a slimline toolbar showing only compact
  // location bar used for popups, toolbar buttons (ie downloads) must be
  // smaller to accommodate the smaller size.
  const int size =
      display_mode_ == DisplayMode::kLocation
          ? location_bar_->PreferredSize().height()
          : GetLayoutConstant(LayoutConstant::kToolbarButtonHeight);
  return gfx::Size(size, size);
}

views::BubbleAnchor ToolbarView::GetDefaultExtensionDialogAnchor() {
  if (extensions_container_ && extensions_container_->GetVisible()) {
    return views::BubbleAnchor(extensions_container_->GetExtensionsButton());
  }
  auto* control = GetAppMenuControl();
  return control ? control->GetAnchor() : views::BubbleAnchor();
}

PageActionIconView* ToolbarView::GetPageActionIconView(
    PageActionIconType type) {
  if (!location_bar_view_) {
    // Only new-style page actions with `webui_location_bar_`.
    return nullptr;
  }
  return location_bar_view()->page_action_icon_controller()->GetIconView(type);
}

page_actions::PageActionViewInterface* ToolbarView::GetPageActionViewInterface(
    actions::ActionId action_id) {
  // TODO: crbug.com/501449027 -- implement for WebUI location bar.
  page_actions::PageActionPropertiesProvider provider;
  if (!provider.Contains(action_id)) {
    return nullptr;
  }
  const auto& properties = provider.GetProperties(action_id);
  if (IsPageActionMigrated(properties.type)) {
    return location_bar_view()->page_action_container()->GetPageActionView(
        action_id);
  }
  return GetPageActionIconView(properties.type);
}

AppMenuControl* ToolbarView::GetAppMenuControl() {
  if (features::IsWebUIAppMenuButtonEnabled() && toolbar_webview_) {
    return toolbar_webview_->GetAppMenuControl();
  }
  return app_menu_button_;
}

const AppMenuControl* ToolbarView::GetAppMenuControl() const {
  if (features::IsWebUIAppMenuButtonEnabled() && toolbar_webview_) {
    return toolbar_webview_->GetAppMenuControl();
  }
  return app_menu_button_;
}

gfx::Rect ToolbarView::GetFindBarBoundingBox(int contents_bottom) {
  if (!browser_->SupportsWindowFeature(
          Browser::WindowFeature::kFeatureLocationBar)) {
    return gfx::Rect();
  }

  CHECK(location_bar_view_)
      << "Alternate location bar impls need to handle this.";

  if (!location_bar_view_->IsDrawn()) {
    return gfx::Rect();
  }

  gfx::Rect bounds = location_bar_view_->ConvertRectToWidget(
      location_bar_view_->GetLocalBounds());
  return gfx::Rect(bounds.x(), bounds.bottom(), bounds.width(),
                   contents_bottom - bounds.bottom());
}

void ToolbarView::FocusToolbar() {
  SetPaneFocus(nullptr);
  if (toolbar_webview_) {
    toolbar_webview_->AdjustForToolbarFocus();
  }
}

views::AccessiblePaneView* ToolbarView::GetAsAccessiblePaneView() {
  return this;
}

views::BubbleAnchor ToolbarView::GetBubbleAnchor(
    std::optional<actions::ActionId> action_id) {
  // If a pinned toolbar actions button exists for the action_id, return that.
  if (pinned_toolbar_actions_ && action_id.has_value() &&
      pinned_toolbar_actions_->IsActionPinnedOrPoppedOut(action_id.value())) {
    return pinned_toolbar_actions_->GetBubbleAnchor(action_id.value());
  }

  // Otherwise attempt to use the location bar.
  auto anchor = features::IsWebUILocationBarEnabled()
                    ? views::BubbleAnchor(location_bar_->GetAnchorOrNull())
                    : views::BubbleAnchor(location_bar_view_);
  bool anchor_not_drawn;
  if (views::View* view = anchor.GetIfView()) {
    anchor_not_drawn = !view->IsDrawn();
  } else {
    anchor_not_drawn = (features::IsWebUILocationBarEnabled() ||
                        features::IsWebUIPinnedToolbarActionsEnabled()) &&
                       anchor.IsNull();
  }
  // In app windows the location bar view may exist but not be drawn. Avoid
  // anchoring bubbles to a non-drawn view (e.g. on Ozone/Wayland) and always
  // return a valid view anchor by falling back to the contents view.
  if (anchor_not_drawn && browser_view_) {
    auto* top_container = browser_view_->top_container();
    CHECK(top_container);
    return views::BubbleAnchor(top_container);
  }
  return anchor;
}

views::BubbleAnchor ToolbarView::GetPageActionBubbleAnchor(
    actions::ActionId action_id) {
  page_actions::PageActionViewInterface* view =
      GetPageActionViewInterface(action_id);
  if (view) {
    return view->GetBubbleAnchor();
  }
  return views::BubbleAnchor();
}

void ToolbarView::ZoomChangedForActiveTab(bool can_show_bubble) {
  if (IsPageActionMigrated(PageActionIconType::kZoom)) {
    auto* zoom_view_controller = browser_->GetActiveTabInterface()
                                     ->GetTabFeatures()
                                     ->zoom_view_controller();
    CHECK(zoom_view_controller);
    zoom_view_controller->UpdatePageActionIconAndBubbleVisibility(
        /*prefer_to_show_bubble=*/can_show_bubble, /*from_user_gesture=*/false);
    return;
  }

  // Other impls are expected to only launch after page action migration.
  if (location_bar_view_) {
    location_bar_view_->page_action_icon_controller()->ZoomChangedForActiveTab(
        can_show_bubble);
  }
}

AvatarToolbarButtonInterface* ToolbarView::GetAvatarToolbarButtonInterface() {
  if (features::IsWebUIAvatarButtonEnabled()) {
    return toolbar_webview_
               ? toolbar_webview_->GetAvatarToolbarButtonInterface()
               : nullptr;
  }
  return avatar_;
}

ToolbarButton* ToolbarView::GetBackButton() {
  return back_;
}

ReloadControl* ToolbarView::GetReloadButton() {
  if (features::IsWebUIReloadButtonEnabled()) {
    if (toolbar_webview_) {
      return toolbar_webview_->GetReloadControl();
    } else {
      return nullptr;
    }
  }
  return reload_;
}

IntentChipButton* ToolbarView::GetIntentChipButton() {
  return location_bar_view() ? location_bar_view()->intent_chip() : nullptr;
}

ToolbarButton* ToolbarView::GetDownloadButton() {
  return pinned_toolbar_actions_container_
             ? pinned_toolbar_actions_container_->GetButtonFor(
                   kActionShowDownloads)
             : nullptr;
}

WebUIToolbarWebView* ToolbarView::GetWebUIToolbarViewForTesting() {
  return toolbar_webview_;
}

std::optional<BrowserRootView::DropIndex> ToolbarView::GetDropIndex(
    const ui::DropTargetEvent& event) {
  return BrowserRootView::DropIndex{
      .index = browser_->tab_strip_model()->active_index(),
      .relative_to_index =
          BrowserRootView::DropIndex::RelativeToIndex::kReplaceIndex};
}

BrowserRootView::DropTarget* ToolbarView::GetDropTarget(
    gfx::Point loc_in_local_coords) {
  return HitTestPoint(loc_in_local_coords) ? this : nullptr;
}

views::View* ToolbarView::GetViewForDrop() {
  return this;
}

void ToolbarView::OnChromeLabsPrefChanged() {
  actions::ActionItem* chrome_labs_action =
      pinned_toolbar_actions_container_->GetActionItemFor(
          kActionShowChromeLabs);
  chrome_labs_action->SetVisible(show_chrome_labs_button_.GetValue() &&
                                 ShouldShowChromeLabsUI(browser_->profile()));
  GetViewAccessibility().AnnounceText(l10n_util::GetStringUTF16(
      chrome_labs_action->GetVisible()
          ? IDS_ACCESSIBLE_TEXT_CHROMELABS_BUTTON_ADDED_BY_ENTERPRISE_POLICY
          : IDS_ACCESSIBLE_TEXT_CHROMELABS_BUTTON_REMOVED_BY_ENTERPRISE_POLICY));
}

void ToolbarView::LoadImages() {
  DCHECK_EQ(display_mode_, DisplayMode::kNormal);

  if (extensions_container_) {
    extensions_container_->UpdateAllIcons();
  }
}

void ToolbarView::OnShowForwardButtonChanged() {
  SetForwardButtonVisibility(show_forward_button_.GetValue());
  InvalidateLayout();
}

void ToolbarView::OnShowHomeButtonChanged() {
  if (home_) {
    home_->SetVisible(show_home_button_.GetValue());
  }
}

void ToolbarView::OnTouchUiChanged() {
  if (display_mode_ == DisplayMode::kNormal) {
    // Update the internal margins for touch layout.
    // TODO(dfried): I think we can do better than this by making the touch UI
    // code cleaner.
    const int default_margin =
        GetLayoutConstant(LayoutConstant::kToolbarElementPadding);
    const int location_bar_margin =
        GetLayoutConstant(LayoutConstant::kLocationBarMargin);
    layout_manager_->SetDefault(views::kMarginsKey,
                                gfx::Insets::VH(0, default_margin));
    if (location_bar_view_) {
      location_bar_view_->SetProperty(views::kMarginsKey,
                                      gfx::Insets::VH(0, location_bar_margin));
    }

    LoadImages();
    PreferredSizeChanged();
  }
}

void ToolbarView::SetForwardButtonVisibility(bool visible) {
  if (features::IsWebUIBackForwardButtonEnabled()) {
    toolbar_webview_->SetForwardVisible(visible);
  } else {
    forward_->SetVisible(visible);
  }
}

gfx::Size ToolbarView::GetBackForwardButtonSize(bool minimum_size) const {
  if (back_) {
    return minimum_size ? back_->GetMinimumSize() : back_->GetPreferredSize();
  }
  const int size = GetLayoutConstant(LayoutConstant::kToolbarButtonHeight);
  return gfx::Size(size, size);
}

BEGIN_METADATA(ToolbarView)
ADD_READONLY_PROPERTY_METADATA(bool, AppMenuFocused)
END_METADATA

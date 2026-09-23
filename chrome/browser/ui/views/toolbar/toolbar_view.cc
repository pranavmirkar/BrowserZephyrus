// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/toolbar/toolbar_view.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

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
#include "base/strings/strcat.h"
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
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_avatar_icon_util.h"
#include "chrome/browser/profiles/profile_manager.h"
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
#include "base/command_line.h"
#include "chrome/browser/ui/views/frame/zephyrus_agent_panel.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_switch.h"
#include "chrome/browser/zephyrus/agent/dev_model_client.h"
#include "chrome/browser/ui/views/frame/zephyrus_privacy_popup.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
#include "chrome/browser/ui/views/frame/zephyrus_private_workspace.h"
// ZEPHYRUS PROFILES FRONTEND - DISABLED.
// #include "chrome/browser/ui/views/frame/zephyrus_profile_switcher.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_tab_helper.h"
#include "base/strings/string_number_conversions.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_runner.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/mojom/dialog_button.mojom-shared.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/base/models/image_model.h"
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
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/gfx/animation/tween.h"
#include "ui/compositor/paint_recorder.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/text_utils.h"
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
#include "ui/views/paint_info.h"
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
#include "chrome/browser/ui/views/frame/zephyrus_workspace_image.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_partition.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_icons.h"
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


// DO NOT reach an icon container's spacing through
// ToolbarIconContainerView::GetTargetLayoutManager(). It is an UNCHECKED
// static_cast to views::FlexLayout, and PinnedToolbarActionsContainer is built
// with use_default_target_layout=false, so no FlexLayout is ever installed --
// the cast hands back a bogus pointer and writing through it takes the browser
// down with an access violation (0xC0000005) before the first paint.

// One geometry source for the container and the native ink-drop mask.
//
// Every segment is SYMMETRIC about its own control. That is the whole point:
// the glyph is centred in the control's cell, so a container that reaches
// further to one side than the other puts the glyph visibly off centre.
//
// Splitting at the midpoint between neighbours did exactly that. A run's first
// control grew only rightwards and its last only leftwards, so the glyph drifted
// toward the group's outer cap -- and the old per-run inset, applied to one side
// of those same two controls, pulled it the same way again.
//
// So the run grows by ONE amount on both sides of every control, taken from its
// tightest gap: the closest pair lands exactly kSeam apart, wider-spaced pairs
// keep a little more air, and no glyph moves. A control standing alone in its
// group grows not at all -- there is nothing to connect to.
SkRRect ZephyrusSegmentShape(const std::vector<ZephyrusGroupSegment>& group,
                            size_t index) {
  constexpr int kSeam = 2;
  // A run only closes up to kSeam if it is allowed to grow half its gap. The
  // toolbar sets its buttons about 13dp apart, so a cap of 6 left roughly a
  // 3dp trough -- visible, and the group read as separate chips. The cap is
  // only here to stop a loosely spaced run reaching into the group beside it.
  constexpr int kMaxGrow = 10;
  const auto& item = group[index];
  int grow;
  if (group.size() > 1) {
    grow = kMaxGrow;
    for (size_t i = 1; i < group.size(); ++i) {
      const int gap =
          std::max(0, group[i].bounds.x() - group[i - 1].bounds.right());
      grow = std::min(grow, (gap - kSeam) / 2);
    }
    // NEGATIVE is allowed, and it has to be: cells that already touch (gap 0)
    // need the containers pulled IN to open the seam, not left flush. Flooring
    // this at zero is what painted the window controls as one unbroken slab.
    grow = std::max(grow, -kSeam);
  } else {
    // Nothing to connect to, so nothing constrains the width -- and a toolbar
    // cell is 24 wide inside a 28 tall bar, so a container drawn at the cell's
    // width is a VERTICAL oval. Widen a lone control until its container is at
    // least square and the fully rounded ends read as a circle.
    grow = std::max(0, (item.bounds.height() - item.bounds.width() + 1) / 2);
  }
  const bool first = index == 0;
  const bool last = index + 1 == group.size();
  const float left = item.bounds.x() - grow;
  const float right = item.bounds.right() + grow;
  const float outer = item.bounds.height() / 2.f;
  // Keep the group's outside silhouette while its inner corners respond.
  //
  // The inner radius has to stay SMALL relative to the container's half-height,
  // or a run stops reading as one bar. These containers are kPillHeight tall,
  // so the half-height is 14: at the old inner radius of 8, two corners facing
  // each other across the 2dp seam opened the background to 2 + 8 + 8 = 18dp at
  // the top and bottom edges while the middle stayed 2dp, and every segment
  // read as its own rounded chip. At 4 that opening is 10dp, and the run reads
  // as a divided bar with fully round ends -- which is the connected button
  // group. Scale this with kPillHeight, never independently of it.
  const float inner = item.pressed ? 2.f : 4.f;
  const float end = first && last && item.pressed ? 4.f : outer;
  const float l = first ? end : inner;
  const float r = last ? end : inner;
  const SkVector radii[4] = {{l, l}, {r, r}, {r, r}, {l, l}};
  return SkRRect::MakeRectRadii(
      SkRect::MakeLTRB(left, item.bounds.y(), right, item.bounds.bottom()), radii);
}

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

// Width of one caption button cell.
//
// ONE constant, because there used to be two: SetPreferredSize said one thing
// and ToolbarView::Layout() hardcoded 46, and since Layout() positions these
// absolutely it always won. Changing the preferred size therefore did nothing
// at all, which is how the buttons ended up drawn small while still occupying
// -- and overlapping -- a 46px column each.
// Title-bar pill geometry, shared by the Shield counter and the workspace
// switcher.
//
// These live at namespace scope on purpose: the two pills sit side by side, so
// any difference between them reads as a mistake rather than a variation. They
// were separately tuned before (the workspace pill carried VH(4, 12) against
// the Shield's VH(0, 5)), which is exactly the drift this prevents.
//
// Height is set by the CONTENTS, not by padding -- the pill just wraps whatever
// is inside it -- so the icon and font sizes matter more than the insets.
inline constexpr int kPillPadV = 0;
inline constexpr int kPillPadH = 5;
inline constexpr int kPillFontSize = 10;
// The height BOTH title-bar pills use.
//
// It used to be implicit -- each pill wrapped its own contents, so the Shield's
// height came from its icon and the workspace circle from its font, and the two
// only matched by coincidence. Stating it once and applying it to both is what
// actually keeps them level; it also makes the workspace cell square (a circle
// needs width == height), so the cell's width follows from this too.
// 28, up from 24. At 24 the workspace glyph was 14px across once the inset was
// taken off it, which is small enough that the icon's animation could not be
// read without leaning toward the screen.
//
// It stays well inside the title bar: the caption buttons beside it are 32
// wide, so 28 is not the constraint on the bar's height. 4px larger is also a
// whole number of device pixels at 1.5x and 2x, which a layer-rounded circle
// needs.
inline constexpr int kPillHeight = 28;
// M3's numeric badge, scaled to a toolbar cell: the spec's 16dp chip is two
// thirds the height of a 24dp control, so it comes down to 12 with an 8pt
// numeral. Below that the digits stop being readable at arm's length.
inline constexpr int kBadgeHeight = 12;
inline constexpr int kBadgeFontSize = 8;
inline constexpr int kBadgePadH = 2;

// Every title-bar control sits in a cell this wide. ToolbarView::Layout gives
// each ToolbarButton SetMinSize(kZephyrusCell, kZephyrusCell), and the window
// controls are built at the same width, so one number drives the whole bar.
//
// Measured, after getting it wrong twice: a
// toolbar cell is 32, NOT the 16dp-icon-plus-4dp-insets 24 that
// layout_constants suggests -- the normalising pass above overrides it. A
// 28 here made the window controls the narrowest thing in the bar.
// What a title-bar container actually DRAWS at, which is not kPillHeight.
//
// A workspace cell is kPillHeight square but insets its disc 1px per side, so
// the disc renders 2 smaller than the cell. Matching the cells therefore does
// NOT match what the eye sees; matching this does. Everything in the bar is
// sized from here so the containers, the window controls and the workspace
// discs all render at one height.
inline constexpr int kZephyrusContainer = kPillHeight - 2;

inline constexpr int kZephyrusCell = kZephyrusContainer;

// The gap between two neighbouring cells. Even, because a run's containers are
// grown or shrunk by half of it to land on the 2dp connected seam; an odd gap
// cannot be halved and leaves the seams a pixel apart.
inline constexpr int kZephyrusGap = 2;

// The gap between two toolbar CELLS, which are Chromium's 24 and which we do
// not widen -- see the note in Layout(). The container grows half of this into
// the gap from each side, so 4 lands a 24dp cell in a kZephyrusContainer-wide
// container with kZephyrusGap left over as the seam: 24 + 2*((4 - 2)/2) == 26.
//
// This and kZephyrusContainer move together. A gap that does not satisfy
// 24 + (gap - kZephyrusGap) == kZephyrusContainer leaves the containers wider
// or narrower than they are tall, which is what turned them into ovals before.
//
// This is the cheap way to a square container. Inflating the CELL to 28 or 32
// instead costs the bar that width per control, and the toolbar decides what
// to push into the overflow menu from the width its controls ASK for, not from
// the pixels left over -- so wide cells evict a button while the bar still
// looks half empty.
inline constexpr int kZephyrusCellGap = 4;

// The widest gap two controls may have and still be drawn as one connected
// group. Generous next to the 2dp pitch, so ordinary layout slack still joins,
// but far under the width of a container that has been emptied out.
inline constexpr int kZephyrusMaxJoin = 8;

// Put one view on the title bar's pitch, and ONLY if it is not already there.
// This is called from Layout(); rewriting a margin that has not changed
// invalidates layout, which re-enters Layout.
void SetZephyrusPitchMargin(views::View* view, int left, int right) {
  const gfx::Insets want = gfx::Insets::TLBR(0, left, 0, right);
  const gfx::Insets* have = view->GetProperty(views::kMarginsKey);
  if (!have || *have != want) {
    view->SetProperty(views::kMarginsKey, want);
  }
}

void SetZephyrusPitchMargin(views::View* view) {
  SetZephyrusPitchMargin(view, kZephyrusCellGap, kZephyrusCellGap);
}

// Space a container's OWN buttons, leaving its outer edges flush.
//
// ToolbarIconContainerView asks its layout to ignore DEFAULT margins on the
// main axis, which keeps its first and last child flush with its edges. An
// explicit margin is not a default, so setting one on every button defeated
// that and parked 6dp inside each container edge -- on top of the 6dp between
// the containers. Measured: 6 + 6 + 6 = 18dp between the extensions button and
// the download button, against 6dp anywhere the toolbar owns both sides, which
// split them into separate groups.
void SetZephyrusContainerPitch(views::View* container) {
  std::vector<views::View*> buttons;
  auto walk = [&](auto&& self, views::View* v) -> void {
    if (!v->GetVisible()) {
      return;
    }
    if (views::AsViewClass<views::Button>(v)) {
      buttons.push_back(v);
      return;
    }
    for (views::View* child : v->children()) {
      self(self, child);
    }
  };
  walk(walk, container);
  if (buttons.empty()) {
    return;
  }
  for (size_t i = 0; i < buttons.size(); ++i) {
    SetZephyrusPitchMargin(buttons[i], i == 0 ? 0 : kZephyrusCellGap,
                           i + 1 == buttons.size() ? 0 : kZephyrusCellGap);
  }

  // Then space the container by what its edges ACTUALLY do, not by what they
  // ought to do. A container's edge need not sit on its outermost button:
  // PinnedToolbarActionsContainer still carries the toolbar divider, whose
  // margin is negative here, so its right edge lands 2dp INSIDE its last
  // button -- measured, the gap across it came out 4 where every gap the
  // toolbar owns is 6, and one tight pair drags a whole run's growth down.
  //
  // Adding back whatever the edge gives away makes the button-to-button gap
  // kZephyrusCellGap wherever the run crosses a container boundary. Margins
  // collapse to the LARGER of the two, so this has to overshoot to win.
  const gfx::Rect lead = views::View::ConvertRectToTarget(
      buttons.front(), container, buttons.front()->GetLocalBounds());
  const gfx::Rect trail = views::View::ConvertRectToTarget(
      buttons.back(), container, buttons.back()->GetLocalBounds());
  SetZephyrusPitchMargin(container, kZephyrusCellGap - lead.x(),
                         kZephyrusCellGap - (container->width() - trail.right()));
}
inline constexpr int kZephyrusCaptionCell = kZephyrusCell;

// Air between the close button and the window's right edge. The strip used to
// run flush into the corner; a few pixels of inset lets the cluster read as a
// group sitting in the title bar rather than jammed against the frame.
inline constexpr int kZephyrusCaptionRightPad = 4;

// Nudges the browser controls (extensions, media, app menu) toward the
// separator.
//
// They are placed by the flex layout, while the separator and window controls
// are positioned absolutely in Layout() afterwards. So flex reserves cells and
// margins for those pinned views that Layout() then ignores, and the slack
// piles up as a gap on the left of the separator. Shrinking the toolbar's right
// interior margin takes that slack back and slides the flex cluster right; the
// pinned views do not move, because their bounds are set by hand.
//
// Raise to move the icons further right.
inline constexpr int kZephyrusBrowserControlsRightShift = 3;

// Cell the separator occupies. Its own width is a hairline; the rest is the
// air either side, which is what actually makes it read as a divider rather
// than a stray mark.
// Air each side of the red rule. ASYMMETRIC on purpose, because its two
// neighbours are not built the same way.
//
// To the RIGHT is a caption button, and it carries far more dead space than its
// numbers suggest. The cell is 32px, the glyph box 16px centred inside it, but
// the Breeze glyph only inks the middle 10 of its 18 grid units (see kGlyphBox
// and the geometry table below). So the chevron's visible edge sits about
// 8 + (4/18 * 16) = 11.6px inside the button, not 8px.
//
// To the LEFT is the app menu, whose dots ink nearly their whole box, behind
// only 2px of inter-icon margin and a little internal padding.
//
// Balancing the CELL widths therefore leaves the rule looking closer to the app
// menu, because ~4px of the right-hand gap is invisible glyph margin rather
// than air. These numbers balance the INK instead, which is what the eye reads.
//
// The two knobs: raise kLeftAir to push the rule off the app menu, raise
// kRightAir to push it off the window controls.
// The caption separator and its spacing constants are gone; the gap between
// the two trailing groups is what divides them now.

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

  // Zephyrus: new tab (+) sits with the navigation controls, since the tab
  // strip's own new-tab button is hidden.
  //
  // ADDED HERE, right after reload, because children are laid out in the order
  // they are added and back/forward/reload/new-tab are one button group. It
  // used to be added after Home and Split tabs, which put those two INSIDE the
  // group's span -- and a group whose members are not adjacent cannot be drawn
  // as one run, so the cluster broke in half around them.
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

  if (!features::IsWebUIHomeButtonEnabled()) {
    home_ = AddChildView(std::make_unique<HomeButton>(
        browser_, base::BindRepeating(callback, browser_, IDC_HOME)));
  }

  if (!features::IsWebUISplitTabsButtonEnabled()) {
    split_tabs_ =
        AddChildView(std::make_unique<SplitTabsToolbarButton>(browser_));
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

  // Zephyrus: the agent button, immediately left of the app menu.
  //
  // On this side of the caption separator on purpose. The rule divides browser
  // controls from window controls, and an agent is a browser control -- putting
  // it beyond the rule would group it with minimise and close, and would sit in
  // the 2px of air that separator's spacing was balanced on.
  //
  // Only present when a model is actually configured. An icon that opens a
  // panel which can only say "no model configured" is worse than no icon, and
  // in a normal build there is nothing behind it yet.
  if (browser_->is_type_normal() &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          zephyrus::agent::kAgentModelEndpointSwitch) &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          zephyrus::agent::kAgentModelSwitch)) {
    auto agent_button = std::make_unique<ToolbarButton>(base::BindRepeating(
        [](Browser* browser) {
          BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser);
          if (view && view->zephyrus_agent_panel()) {
            view->zephyrus_agent_panel()->Toggle();
          }
        },
        browser_));
    agent_button->SetVectorIcon(vector_icons::kChatSparkIcon);
    const std::u16string agent_name = u"Agent";
    agent_button->SetTooltipText(agent_name);
    agent_button->GetViewAccessibility().SetName(agent_name);
    zephyrus_agent_button_ = AddChildView(std::move(agent_button));
  }

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
  // ZEPHYRUS PROFILES FRONTEND - DISABLED (see toolbar_view.cc).
    // AddZephyrusProfileButton();
    AddZephyrusAdblockButton();
  }

  // Keep optional actions together on the trailing side of the omnibox.
  // Move before creating its flexible spacers so neither spacer splits a group.
  if (browser_->is_type_normal() && location_bar_view_) {
    for (views::View* action : {static_cast<views::View*>(split_tabs_.get()),
                               static_cast<views::View*>(home_.get())}) {
      if (action) {
        ReorderChildView(action, GetIndexOf(location_bar_view_).value());
      }
    }
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

  // Runs on tab switch and on navigation, which is exactly when the badge has
  // to follow a different counter.
  ObserveZephyrusAdblockCount();
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
    // Normalize icon cells, including actions added after initialization.
    // Leave workspace content and the address field's internal buttons alone.
    auto size_buttons = [&](auto&& self, views::View* view) -> void {
      if (auto* button = views::AsViewClass<ToolbarButton>(view)) {
        // The CELL is left at whatever Chromium sizes it -- 24 for a toolbar
        // button. Forcing it to kZephyrusCell made every control demand 8dp
        // more than it needs, and the toolbar evicts buttons to the overflow
        // menu on demanded width, so Downloads disappeared while the bar still
        // had obvious room. The container reaches kZephyrusCell by growing
        // into kZephyrusCellGap instead, which costs the bar 2dp per control
        // rather than 6.
        button->SetMinSize(gfx::Size(0, kPillHeight));
        // ToolbarButton's constructor does SetHorizontalAlignment(ALIGN_RIGHT).
        // Upstream that is invisible: a stock cell is 24dp with 4dp insets, so
        // the content box is exactly the icon's 16dp and every alignment lands
        // in the same place. Widening the cell to kZephyrusCell for one bar
        // pitch created 8dp of slack, and ALIGN_RIGHT put ALL of it on the
        // icon's left -- MEASURED off the screen at 1.5x: every glyph sat 6
        // device px, exactly 4dp, right of its container's centre.
        //
        // Icon-only controls belong centred. The app menu is left alone: it
        // grows into a labelled chip, where the icon hugging the text is the
        // point.
        if (button != app_menu_button_ &&
            button->GetHorizontalAlignment() != gfx::ALIGN_CENTER) {
          button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
        }
        // One pitch for the whole bar. FlexLayout collapses margins, so the
        // gap between two controls is the LARGER of their two margins, not the
        // sum. A uniform margin makes every gap kZephyrusGap, and a run of
        // evenly pitched cells is the only way the seams inside it come out
        // equal.
        if (!views::AsViewClass<ToolbarIconContainerView>(button->parent())) {
          SetZephyrusPitchMargin(button);
        }
        // The shared container paints the state layer once. Keep the native
        // mask/focus geometry but avoid a second wash over the same segment.
        views::InkDrop::Get(button)->SetHighlightOpacity(0.f);
        views::InkDrop::Get(button)->SetVisibleOpacity(0.f);
        return;
      }
      if (views::AsViewClass<ToolbarIconContainerView>(view)) {
        SetZephyrusContainerPitch(view);
      }
      for (views::View* child : view->children()) {
        self(self, child);
      }
    };
    if (zephyrus_nav_pill_backdrop_) {
      for (views::View* child : children()) {
        if (views::AsViewClass<views::Button>(child) ||
            views::AsViewClass<ToolbarIconContainerView>(child)) {
          size_buttons(size_buttons, child);
        }
      }
    }
    LayoutCommon();
  }

  if (toolbar_controller_ && !zephyrus_compact_) {
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
    int right = width() - kZephyrusCaptionRightPad;
    for (views::Button* button :
         {zephyrus_close_button_.get(), zephyrus_maximize_button_.get(),
          zephyrus_minimize_button_.get()}) {
      if (button && button->GetVisible()) {
        // Full height is the HIT area -- the pointer thrown into the screen
        // corner must still land on Close. The drawn container is clamped to
        // kPillHeight by the group, so what you see matches the other
        // controls while what you can hit reaches the edge.
        //
        // This SetBounds is why setting a preferred size on the button did
        // nothing -- and why kMarginsKey does nothing either. The manual layout
        // overrides both, so the bar's pitch has to be added HERE. Without the
        // gap the three cells sat exactly kZephyrusCaptionCell apart, touching,
        // and their containers only got a seam because the growth rule pulled
        // each one in by a pixel -- which left the window controls 2dp narrower
        // than every other control in the bar.
        button->SetBounds(right - kZephyrusCaptionCell, 0,
                          kZephyrusCaptionCell, height());
        right -= kZephyrusCaptionCell + kZephyrusGap;
      }
    }
    // While the page controls are lent to the sidebar, what is left on the bar
    // is the pin button and the user's own containers, and the flex layout has
    // nothing to lay them out against, so they are placed here: pin first,
    // then the containers, running leftwards from the window controls.
    if (zephyrus_compact_) {
      if (zephyrus_pin_button_ && zephyrus_pin_button_->GetVisible()) {
        zephyrus_pin_button_->SetBounds(right - kZephyrusCaptionCell, 0,
                                        kZephyrusCaptionCell, height());
        right -= kZephyrusCaptionCell;
      }
      // The user's containers stand CLEAR of the window controls rather than
      // beside them at a seam's distance. A pinned action and a close button
      // are not the same kind of control, and a gap wider than kZephyrusMaxJoin
      // is what tells the group painter to draw them as separate runs -- at
      // kZephyrusGap it drew one control's container overlapping the next.
      constexpr int kCompactGroupGap = kZephyrusMaxJoin + 2;
      bool leading = true;
      for (views::View* child : children()) {
        if (!views::AsViewClass<ToolbarIconContainerView>(child) ||
            !child->GetVisible()) {
          continue;
        }
        right -= leading ? kCompactGroupGap : kZephyrusGap;
        leading = false;
        const gfx::Size size = child->GetPreferredSize();
        // PREFERRED height, centred in the bar -- not the bar's full height.
        // Stretched, the container top-aligns the button inside it, and the
        // group painter centres each container on its CONTROL: the button rode
        // high, so its container did too, and the bar's top edge clipped it.
        const int container_height = std::min(height(), size.height());
        child->SetBounds(right - size.width(),
                         (height() - container_height) / 2, size.width(),
                         container_height);
        right -= size.width();
      }
    }
    // The caption separator is GONE. It existed to divide the window controls
    // from the toolbar actions; the two are now separate connected groups, and
    // the gap between the groups says the same thing without a drawn rule.
    // What splits them is the control KIND, not a view sitting between them --
    // see ZephyrusTitlebarGroups.
  }
  // The group painter covers the whole bar; it positions nothing itself, it
  // just draws every group's containers behind the controls.
  if (zephyrus_nav_pill_backdrop_) {
    zephyrus_nav_pill_backdrop_->SetVisible(true);
    zephyrus_nav_pill_backdrop_->SetBoundsRect(GetLocalBounds());
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
    static constexpr int kZephyrusOmniboxMaxWidth = 720;
    constexpr int kZephyrusSpacerOrder = kOrderOffset + 5;
    // PREFERRED is the bar's own preferred width; only space OFFERED beyond
    // that grows it towards the cap.
    //
    // This rule used to answer 720 to the unbounded question, so FlexLayout
    // took 720 as the bar's preferred width. The bar is also first in flex
    // order -- upstream's kOmniboxResizingPrioritization, so the omnibox keeps
    // its minimum until everything else has dropped out -- which meant it took
    // up to 720dp before the toolbar actions got anything. MEASURED in a
    // 1100dp window: the pinned-actions container was allotted 0x0, and
    // Downloads went to the overflow menu with the bar plainly half empty.
    // Maximised, the leftover happened to cover it, which is why this only
    // showed in a restored window, and why fixing cell widths earlier only
    // moved the threshold.
    //
    // The look is unchanged. FlexLayout's first pass caps every child at its
    // preferred width, so the actions now get theirs; its second pass grows
    // children in flex order, and the bar is first, so it still takes the
    // remaining space up to the cap before the spacers that centre it get any.
    const views::FlexRule zephyrus_centered_omnibox_rule =
        base::BindRepeating(
            [](const views::View* view, const views::SizeBounds& bounds) {
              const gfx::Size natural = view->GetPreferredSize(bounds);
              if (!bounds.width().is_bounded()) {
                return gfx::Size(
                    std::min(natural.width(), kZephyrusOmniboxMaxWidth),
                    natural.height());
              }
              const int width = std::max(
                  bounds.width().min_of(kZephyrusOmniboxMaxWidth), 0);
              return gfx::Size(width, natural.height());
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
// A vertical hairline dividing the browser controls from the window controls.
// ZephyrusCaptionSeparator is DELETED. It drew a short accent rule between
// the toolbar actions and the window controls; those are now two separate
// button groups, and the gap between them says the same thing without
// spending the accent on decoration.

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
    // Reserve the same width that Layout() assigns. The full-height hit
    // target is independent of the compact painted container.
    SetPreferredSize(gfx::Size(kZephyrusCaptionCell, 36));
  }

  // The 18-unit Breeze grid is drawn into a box this many DIPs across; the
  // drawn glyph occupies roughly the middle 10 units.
  static constexpr float kGlyphBox = 16.f;

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

  // The group owns the complete container and state layer. A second circular
  // hover fill here would cover the connected corners with a different shape.
  void OnPaintBackground(gfx::Canvas* canvas) override {}

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    if (parent()) {
      parent()->SchedulePaint();
    }
  }

  // Breeze geometry, from breezebutton.cpp, on its own 18x18 grid:
  //   close     X       (5,5)-(13,13) and (13,5)-(5,13)
  //   maximize  chevron UP    (4,11)-(9,6)-(14,11)
  //   minimize  chevron DOWN  (4,7)-(9,12)-(14,7)
  //   restore   filled diamond (4,9)-(9,4)-(14,9)-(9,14)
  //
  // Antialiased on a float grid deliberately: diagonals and chevrons cannot be
  // snapped to whole pixels without distorting their angles, which is what made
  // the old pixel-blocked X ragged at fractional scaling.
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const bool hot =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    // Pair the glyph with the container, including Close's error state.
    const SkColor symbol_color = zephyrus::m3::Role(
        *this, hot && kind_ == Kind::kClose ? kColorZephyrusOnErrorContainer
                                          : kColorZephyrusOnSecondaryContainer);

    gfx::ScopedCanvas scoped(canvas);
    const gfx::Rect contents = GetContentsBounds();
    const float unit = kGlyphBox / 18.f;
    canvas->Translate(gfx::Vector2d(contents.x(), contents.y()));
    canvas->sk_canvas()->scale(unit, unit);
    canvas->sk_canvas()->translate((contents.width() / unit) / 2.f - 9.f,
                                   (contents.height() / unit) / 2.f - 9.f);

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(symbol_color);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.25f);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setStrokeJoin(cc::PaintFlags::kRound_Join);

    switch (kind_) {
      case Kind::kClose:
        canvas->sk_canvas()->drawLine(5, 5, 13, 13, flags);
        canvas->sk_canvas()->drawLine(13, 5, 5, 13, flags);
        break;
      // macOS glyphs for these two. Close keeps its X, which both platforms
      // draw the same way.
      case Kind::kMinimize:
        // A plain bar. macOS's minimise is the one glyph in the set with no
        // direction to it -- the window is not going anywhere you can point at.
        canvas->sk_canvas()->drawLine(5, 9, 13, 9, flags);
        break;
      case Kind::kMaximizeRestore: {
        // Two triangles in opposite corners: pointing OUT to expand, and in
        // toward each other to restore. Filled, unlike the chevrons this
        // replaces -- a stroked triangle this small closes up into a smudge,
        // and macOS fills them for the same reason.
        cc::PaintFlags solid = flags;
        solid.setStyle(cc::PaintFlags::kFill_Style);
        SkPathBuilder path;
        if (maximized_) {
          // Restore: apexes meet near the centre.
          path.moveTo(4, 8);
          path.lineTo(8, 8);
          path.lineTo(8, 4);
          path.close();
          path.moveTo(14, 10);
          path.lineTo(10, 10);
          path.lineTo(10, 14);
          path.close();
        } else {
          // Expand: apexes reach for the corners.
          path.moveTo(4, 4);
          path.lineTo(10, 4);
          path.lineTo(4, 10);
          path.close();
          path.moveTo(14, 14);
          path.lineTo(8, 14);
          path.lineTo(14, 8);
          path.close();
        }
        canvas->sk_canvas()->drawPath(path.detach(), solid);
        break;
      }
    }
  }

 private:
  Kind kind_;
  bool maximized_ = false;
  SkColor foreground_ = SK_ColorWHITE;
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

  // Count of requests blocked on the current page. Zero draws nothing: a shield
  // reading "0" is noise on every page that simply had nothing to block.
  void SetZephyrusBadgeCount(int count) {
    count = std::max(0, count);
    if (badge_count_ == count) {
      return;
    }
    // The count is a BADGE ON the shield -- M3's numeric badge, a chip of its
    // own sitting on the icon's top-right corner.
    //
    // It spent a while sitting BESIDE the glyph instead, as button text inside
    // a pill. That reads well on its own and costs a fortune in width: the
    // control stopped being a 24dp cell and became a ~48dp one, which every
    // layout holding it then had to accommodate -- the sidebar's control strip
    // grew a special case to let one cell outgrow its neighbours, and the run
    // went ragged whenever the counter did. On the icon, the count costs
    // nothing: the shield is a cell like every other cell, at every width.
    badge_count_ = count;
    // NOT SetText. The glyph keeps the standard toolbar icon size and the
    // button keeps its standard bounds; only the painting changes.
    SchedulePaint();
  }

  // ToolbarButton:
  SkColor GetForegroundColor(ButtonState state) const override {
    return foreground_.value_or(ToolbarButton::GetForegroundColor(state));
  }

  // views::View:
  // AFTER the children, because the glyph is one of them: a LabelButton draws
  // its icon into a child image view, so anything painted in OnPaint or
  // OnPaintBackground lands UNDER the shield rather than on it.
  void PaintChildren(const views::PaintInfo& paint_info) override {
    ToolbarButton::PaintChildren(paint_info);
    if (badge_count_ <= 0) {
      return;
    }
    ui::PaintRecorder recorder(paint_info.context(), size());
    gfx::Canvas* canvas = recorder.canvas();

    // Three digits do not fit on a 24dp control at a legible size, and the
    // exact number stops mattering long before then -- "99+" is the number.
    const std::u16string text = badge_count_ > 99
                                    ? u"99+"
                                    : base::NumberToString16(badge_count_);
    const gfx::FontList font({"Segoe UI"}, gfx::Font::NORMAL, kBadgeFontSize,
                             gfx::Font::Weight::MEDIUM);
    const int text_width = gfx::GetStringWidth(text, font);
    const int chip_width =
        std::max(kBadgeHeight, text_width + 2 * kBadgePadH);

    // Anchored to the ICON, not to the cell. A toolbar cell is wider than the
    // glyph it holds and the extra is padding, so a badge in the cell's corner
    // floats away from the thing it is counting. This hangs it off the glyph's
    // top-right corner, then clamps it inside the view -- painting is clipped
    // to a view's own bounds, so a badge pushed past the edge would simply
    // lose its end.
    gfx::Rect icon = GetLocalBounds();
    icon.ClampToCenteredSize(gfx::Size(GetIconSize(), GetIconSize()));
    const int chip_x = std::clamp(icon.right() - chip_width / 2, 0,
                                  std::max(0, width() - chip_width));
    const int chip_y =
        std::clamp(icon.y() - kBadgeHeight / 2, 0,
                   std::max(0, height() - kBadgeHeight));
    const gfx::RectF chip(chip_x, chip_y, chip_width, kBadgeHeight);

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    // PRIMARY, not M3's error role. A notification badge defaults to error
    // because it usually reports something wrong; this reports the blocker
    // doing its job, and colouring that red would be the browser calling its
    // own good news an alarm.
    fill.setColor(zephyrus::m3::Role(*this, kColorZephyrusPrimary));
    canvas->DrawRoundRect(chip, kBadgeHeight / 2.f, fill);

    // The text rect is taller than the chip, sharing its centre: the font's
    // line height is larger than a 12dp chip, and centring the block in a rect
    // that cannot hold it pushed the digits out through the top.
    gfx::RectF text_box = chip;
    text_box.Outset(gfx::OutsetsF::VH(kBadgeHeight, 0));
    canvas->DrawStringRectWithFlags(
        text, font, zephyrus::m3::Role(*this, kColorZephyrusOnPrimary),
        gfx::ToEnclosingRect(text_box), gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  std::optional<SkColor> foreground_;
  int badge_count_ = 0;
};

BEGIN_METADATA(ZephyrusPinButton)
END_METADATA

// Zephyrus: modal confirmation for a destructive workspace delete. The Figma
// file has no dialog component, so the surface is derived from the design
// system already in use — the workspace dropdown's lifted panel color and the
// 10px card radius — rather than inventing a second visual language.
// Holds things -> card radius.
// Material 3 basic dialog, to spec.
//
// 28dp container, 24dp padding, a 24sp headline over 14sp supporting text, and
// TEXT buttons 40dp tall in a right-aligned row. The pieces that look like
// arbitrary numbers are the spec's numbers.
constexpr int kZephyrusDialogRadius = 28;
constexpr int kDialogPadding = 24;
// Headline -> supporting text, then supporting text -> actions. MD3 opens the
// second gap deliberately: the message is read, then the choice is made.
constexpr int kDialogHeadlineGap = 16;
constexpr int kDialogActionsGap = 24;
// Type sizes used to be declared here (24 / 14 / 14). They are gone: the scale
// in zephyrus_m3.h owns those numbers now, and this dialog asks for
// headline-small, body-medium and label-large by name. Two copies of a size is
// how a "scale" stops being one.
constexpr int kDialogButtonHeight = 40;
constexpr int kDialogButtonHPadding = 12;
// CONNECTED BUTTON GROUP: outer edges fully round, the touching edges nearly
// square, separated by a hair.
constexpr int kDialogButtonGap = 4;
// CONCENTRIC with the dialog. A corner nested inside another shares its centre
// only when its radius is the outer radius minus the gap between them, and the
// actions sit exactly kDialogPadding in from the container edge:
//   28 (container) - 24 (padding) = 4
constexpr int kDialogButtonInnerRadius =
    kZephyrusDialogRadius - kDialogPadding;
constexpr int kZephyrusDialogWidth = 360;
// Entrance is generous enough to be read as an arrival; the exit is quicker,
// because waiting on a dialog you have already dismissed is what makes an
// interface feel slow. Both stay inside the sub-300ms UI budget.
constexpr base::TimeDelta kZephyrusDialogEnterDuration =
    base::Milliseconds(200);
constexpr base::TimeDelta kZephyrusDialogExitDuration = base::Milliseconds(130);

// An M3 EXPRESSIVE SHAPE holding the Shield's one number: the scalloped
// "cookie" from M3's shape library, filled in `primary`, with the count on it
// in `onPrimary`. It is the only emphasis in the popup, so the number reads
// first and everything else can stay quiet.
class ZephyrusShieldCookie : public views::View {
  METADATA_HEADER(ZephyrusShieldCookie, views::View)

 public:
  static constexpr int kSize = 64;

  ZephyrusShieldCookie(const std::u16string& text, SkColor fill, SkColor ink)
      : fill_(fill) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    auto* label = AddChildView(std::make_unique<views::Label>(text));
    label->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kHeadlineSmall));
    label->SetEnabledColor(ink);
    label->SetBackgroundColor(fill);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    // The number is read by the text beside it; the shape is decoration.
    label->GetViewAccessibility().SetIsIgnored(true);
  }
  ZephyrusShieldCookie(const ZephyrusShieldCookie&) = delete;
  ZephyrusShieldCookie& operator=(const ZephyrusShieldCookie&) = delete;
  ~ZephyrusShieldCookie() override = default;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kSize, kSize);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    // Nine gentle scallops: a radius that swells and dips 8% around the
    // circle, sampled finely enough that the outline is smooth at any scale.
    constexpr int kLobes = 9;
    constexpr int kSamples = 180;
    constexpr float kDepth = 0.08f;
    const gfx::PointF c = gfx::RectF(GetLocalBounds()).CenterPoint();
    const float outer = std::min(width(), height()) / 2.0f;
    SkPathBuilder builder;
    for (int i = 0; i < kSamples; ++i) {
      const float theta = 2.0f * std::numbers::pi_v<float> * i / kSamples -
                          std::numbers::pi_v<float> / 2;
      const float r =
          outer * (1.0f - kDepth / 2 + (kDepth / 2) * std::cos(kLobes * theta));
      const SkPoint p = SkPoint::Make(c.x() + r * std::cos(theta),
                                      c.y() + r * std::sin(theta));
      if (i == 0) {
        builder.moveTo(p);
      } else {
        builder.lineTo(p);
      }
    }
    builder.close();
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill_);
    canvas->DrawPath(builder.detach(), flags);
    views::View::OnPaint(canvas);
  }

 private:
  SkColor fill_;
};

BEGIN_METADATA(ZephyrusShieldCookie)
END_METADATA

// An M3 FILTER CHIP (FilterChipTokens): 32dp tall, 8dp corners, labelLarge.
//
//   unselected  1dp outlineVariant outline, onSurfaceVariant content
//   selected    secondaryContainer, onSecondaryContainer content
//
// The leading icon is ALWAYS present -- the setting's own icon when off, M3's
// checkmark when on -- so a chip does not change width when it is pressed, and
// the popup around it never resizes under the pointer.
//
// A click flips the state first and then runs the callback, like
// zephyrus::m3::Switch, so the callback reads the new value.
class ZephyrusFilterChip : public views::Button {
  METADATA_HEADER(ZephyrusFilterChip, views::Button)

 public:
  ZephyrusFilterChip(const std::u16string& label,
                     const std::u16string& accessible_name,
                     const gfx::VectorIcon& icon,
                     bool selected)
      : label_(label),
        icon_(icon),
        font_(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge)),
        selected_(selected) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kCheckBox);
    GetViewAccessibility().SetName(accessible_name);
    SetTooltipText(accessible_name);
    UpdateAccessibleCheckedState();
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kRadius);
    SetInstallFocusRingOnFocus(true);
  }
  ZephyrusFilterChip(const ZephyrusFilterChip&) = delete;
  ZephyrusFilterChip& operator=(const ZephyrusFilterChip&) = delete;
  ~ZephyrusFilterChip() override = default;

  bool selected() const { return selected_; }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kLeading + kIcon + kIconGap +
                         gfx::GetStringWidth(label_, font_) + kTrailing,
                     kHeight);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    // Every colour is a role, and a role needs a Widget.
    if (!GetWidget()) {
      return;
    }
    const gfx::RectF bounds(GetLocalBounds());
    const SkColor content = zephyrus::m3::Role(
        *this, selected_ ? kColorZephyrusOnSecondaryContainer
                         : kColorZephyrusOnSurfaceVariant);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (selected_) {
      flags.setColor(
          zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer));
      canvas->DrawRoundRect(bounds, kRadius, flags);
    } else {
      // Inside the bounds, so selecting a chip does not change its size.
      gfx::RectF outline = bounds;
      outline.Inset(0.5f);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(1.0f);
      flags.setColor(zephyrus::m3::Role(*this, kColorZephyrusOutlineVariant));
      canvas->DrawRoundRect(outline, kRadius - 0.5f, flags);
      flags.setStyle(cc::PaintFlags::kFill_Style);
    }

    if (GetEnabled()) {
      const bool pressed = GetState() == STATE_PRESSED;
      const bool hovered = GetState() == STATE_HOVERED;
      if (pressed || hovered || HasFocus()) {
        flags.setColor(zephyrus::m3::StateLayer(
            content, pressed   ? zephyrus::m3::kPressed
                     : hovered ? zephyrus::m3::kHover
                               : zephyrus::m3::kFocus));
        canvas->DrawRoundRect(bounds, kRadius, flags);
      }
    }

    // Laid out in LTR and mirrored by hand: flipping the canvas would mirror
    // the glyphs too.
    const gfx::ImageSkia icon = gfx::CreateVectorIcon(
        selected_ ? kCheckIcon : *icon_, kIcon, content);
    canvas->DrawImageInt(icon, GetMirroredXWithWidthInView(kLeading, kIcon),
                         (height() - kIcon) / 2);
    const int text_x = kLeading + kIcon + kIconGap;
    canvas->DrawStringRectWithFlags(
        label_, font_, content,
        GetMirroredRect(gfx::Rect(text_x, 0,
                                  std::max(0, width() - text_x - kTrailing),
                                  height())),
        gfx::Canvas::TEXT_ALIGN_CENTER);
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
  }

  void NotifyClick(const ui::Event& event) override {
    selected_ = !selected_;
    UpdateAccessibleCheckedState();
    SchedulePaint();
    views::Button::NotifyClick(event);
  }

  void OnFocus() override {
    views::Button::OnFocus();
    SchedulePaint();
  }
  void OnBlur() override {
    views::Button::OnBlur();
    SchedulePaint();
  }
  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    SchedulePaint();
  }

 protected:
  // views::Button:
  void UpdateAccessibleCheckedState() override {
    GetViewAccessibility().SetCheckedState(
        selected_ ? ax::mojom::CheckedState::kTrue
                  : ax::mojom::CheckedState::kFalse);
  }

 private:
  static constexpr int kHeight = 32;
  static constexpr int kRadius = 8;
  static constexpr int kLeading = 8;
  static constexpr int kIcon = 18;
  static constexpr int kIconGap = 8;
  static constexpr int kTrailing = 16;

  std::u16string label_;
  raw_ref<const gfx::VectorIcon> icon_;
  gfx::FontList font_;
  bool selected_;
};

BEGIN_METADATA(ZephyrusFilterChip)
END_METADATA

// M3's CHIP GROUP: chips in a row that wraps, 8dp apart both ways.
class ZephyrusChipGroup : public views::View {
  METADATA_HEADER(ZephyrusChipGroup, views::View)

 public:
  ZephyrusChipGroup() = default;
  ZephyrusChipGroup(const ZephyrusChipGroup&) = delete;
  ZephyrusChipGroup& operator=(const ZephyrusChipGroup&) = delete;
  ~ZephyrusChipGroup() override = default;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Rect extent;
    for (const gfx::Rect& rect :
         Place(available_size.width().is_bounded()
                   ? available_size.width().value()
                   : std::numeric_limits<int>::max())) {
      extent.Union(rect);
    }
    return extent.size();
  }

  void Layout(PassKey) override {
    const std::vector<gfx::Rect> rects = Place(width());
    size_t i = 0;
    for (views::View* child : children()) {
      if (child->GetVisible()) {
        child->SetBoundsRect(rects[i++]);
      }
    }
  }

 private:
  static constexpr int kGap = 8;

  // One rect per VISIBLE child, in order. Reads only preferred sizes, never
  // this view's own, so it is safe to call from CalculatePreferredSize.
  std::vector<gfx::Rect> Place(int max_width) const {
    std::vector<gfx::Rect> rects;
    int x = 0;
    int y = 0;
    int row_height = 0;
    for (const views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      const gfx::Size size = child->GetPreferredSize();
      if (x > 0 && x + size.width() > max_width) {
        x = 0;
        y += row_height + kGap;
        row_height = 0;
      }
      rects.emplace_back(x, y, size.width(), size.height());
      x += size.width() + kGap;
      row_height = std::max(row_height, size.height());
    }
    return rects;
  }
};

BEGIN_METADATA(ZephyrusChipGroup)
END_METADATA

// An M3 FILLED TONAL BUTTON with a trailing icon: 40dp, full-round,
// secondaryContainer, labelLarge in onSecondaryContainer, 16dp either side.
class ZephyrusTonalButton : public views::LabelButton {
  METADATA_HEADER(ZephyrusTonalButton, views::LabelButton)

 public:
  ZephyrusTonalButton(PressedCallback callback,
                      const std::u16string& text,
                      const gfx::VectorIcon& trailing_icon,
                      SkColor container,
                      SkColor content)
      : views::LabelButton(std::move(callback), text) {
    label()->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge));
    SetEnabledTextColors(content);
    SetImageModel(STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(trailing_icon, content, 18));
    // ALIGN_RIGHT is what puts LabelButton's image AFTER its label.
    SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    SetImageLabelSpacing(8);
    SetMinSize(gfx::Size(0, 40));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 16)));
    SetBackground(views::CreateRoundedRectBackground(container, 20));
    SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    SetInstallFocusRingOnFocus(true);
    views::InstallPillHighlightPathGenerator(this);
    views::InkDropHost* const ink = views::InkDrop::Get(this);
    ink->SetMode(views::InkDropHost::InkDropMode::ON);
    ink->SetBaseColor(content);
    ink->SetHighlightOpacity(zephyrus::m3::kHover / 255.0f);
    ink->SetVisibleOpacity(zephyrus::m3::kPressed / 255.0f);
  }
  ZephyrusTonalButton(const ZephyrusTonalButton&) = delete;
  ZephyrusTonalButton& operator=(const ZephyrusTonalButton&) = delete;
  ~ZephyrusTonalButton() override = default;
};

BEGIN_METADATA(ZephyrusTonalButton)
END_METADATA

// A dialog action button (Cancel / destructive confirm).
class ZephyrusDialogButton : public views::LabelButton {
  METADATA_HEADER(ZephyrusDialogButton, views::LabelButton)

 public:
  // Per-corner radii, because the two actions are segments of one connected
  // group. No fill colours: the tonal container is derived from the label.
  ZephyrusDialogButton(const std::u16string& text,
                       SkColor text_color,
                       const gfx::RoundedCornersF& radii,
                       PressedCallback callback)
      : views::LabelButton(std::move(callback), text), radii_(radii) {
    views::FocusRing::Remove(this);
    // One segment of an MD3 connected button group: a tonal container at rest
    // with per-corner radii, the label carrying the meaning. This replaces a
    // filled confirm next to an outlined cancel, which read as "the red one is
    // the default" when the default here is Cancel.
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetEnabledTextColors(text_color);
    SetTextColor(views::Button::STATE_HOVERED, text_color);
    SetTextColor(views::Button::STATE_PRESSED, text_color);
    SetMinSize(gfx::Size(0, kDialogButtonHeight));
    SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(0, kDialogButtonHPadding)));
    label()->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge));
    SetAnimateOnStateChange(false);
    press_morph_.SetSlideDuration(
        zephyrus::m3::Duration(zephyrus::m3::Spring::kFastSpatial));
    press_morph_.SetTweenType(gfx::Tween::LINEAR);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetRoundedCornerRadius(radii_);
    layer()->SetIsFastRoundedCorner(true);
    UpdateFill();
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    UpdateFill();
    UpdatePressFeedback();
  }

  // NO focus ring is painted here, and views::Button's default one is removed
  // in the constructor. There were two of them stacked -- a custom stroke plus
  // the platform ring -- which read as a selected state and cut the connected
  // group's shared silhouette in half.
  //
  // Focus is still visible: Cancel takes it by default and its state layer
  // shows through. That is weaker than a ring and it is a real accessibility
  // trade -- if keyboard focus turns out to be hard to find here, the answer is
  // MD3's 3dp indicator drawn OUTSIDE the group, not a stroke inside a segment.

 private:
  void UpdateFill() {
    // A tonal container AT REST, not just on hover. These are segments of one
    // connected group, and the group's shape is the control -- two bare text
    // labels have no shape to connect.
    //
    // The container is neutral ink for both segments; the label carries the
    // difference in meaning. A red container under the destructive label would
    // make it the loudest thing in a dialog whose default action is Cancel.
    // A tonal container PLUS a state layer, which is how M3 composes this --
    // not three unrelated alphas. The resting container stays put and the
    // state layer adds on top of it, so hover and press are the same
    // increments here as on every other surface.
    //
    // Press was 0x33 (20%), double the spec's 10%. That is most of why these
    // buttons felt heavier than the rest of the UI.
    // Repaint at whatever shape the morph currently holds, so a state change
    // mid-press does not snap the corners back to their resting value.
    ApplyMorph(current_morph_);
  }

  // The container fill for the current state. Split out because the morph
  // repaints the background too and the two must not disagree about colour.
  SkColor FillColor() const {
    constexpr SkAlpha kContainer = 0x14;  // resting tonal fill
    const ButtonState state = GetState();
    SkAlpha alpha = kContainer;
    if (state == STATE_PRESSED) {
      alpha = kContainer + zephyrus::m3::kPressed;
    } else if (state == STATE_HOVERED) {
      alpha = kContainer + zephyrus::m3::kHover;
    }
    return SkColorSetA(zephyrus::Ink(), alpha);
  }

  // SHAPE MORPH on press: the corner squares up while the button is held.
  //
  // This replaces a 0.97 scale transform. The scale was a reasonable invention
  // but it was an invention; the morph is M3's actual button spec, and it is
  // the signature Expressive interaction rather than a flourish.
  //
  // Only the OUTER corners move. The inner ones are the seam of a connected
  // group and belong to the group's geometry, so squaring them would break the
  // pair apart mid-press.
  //
  // At 40dp these are M3 "small" buttons, whose pressed corner is 8.
  void UpdatePressFeedback() {
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      // Reduced motion: take the state, skip the travel.
      ApplyMorph(GetState() == views::Button::STATE_PRESSED ? 1.f : 0.f);
      return;
    }
    if (GetState() == views::Button::STATE_PRESSED) {
      press_morph_.Show();
    } else {
      press_morph_.Hide();
    }
  }

  // views::Button:
  void AnimationProgressed(const gfx::Animation* animation) override {
    if (animation != &press_morph_) {
      views::LabelButton::AnimationProgressed(animation);
      return;
    }
    // LINEAR in, M3's curve applied here -- see zephyrus::m3::Curve. Letting
    // SlideAnimation tween as well would apply two curves to one value.
    ApplyMorph(static_cast<float>(
        zephyrus::m3::Curve(zephyrus::m3::Spring::kFastSpatial)
            .Solve(press_morph_.GetCurrentValue())));
  }

  void ApplyMorph(float t) {
    constexpr float kPressedOuter = 8.f;
    const float outer_tl = gfx::Tween::FloatValueBetween(
        t, radii_.upper_left(), std::min(radii_.upper_left(), kPressedOuter));
    const float outer_bl = gfx::Tween::FloatValueBetween(
        t, radii_.lower_left(), std::min(radii_.lower_left(), kPressedOuter));
    const float outer_tr = gfx::Tween::FloatValueBetween(
        t, radii_.upper_right(), std::min(radii_.upper_right(), kPressedOuter));
    const float outer_br = gfx::Tween::FloatValueBetween(
        t, radii_.lower_right(), std::min(radii_.lower_right(), kPressedOuter));
    const gfx::RoundedCornersF morphed(outer_tl, outer_tr, outer_br, outer_bl);
    current_morph_ = t;
    if (ui::Layer* l = layer()) {
      l->SetRoundedCornerRadius(morphed);
    }
    SetBackground(views::CreateRoundedRectBackground(FillColor(), morphed));
    SchedulePaint();
  }

  const gfx::RoundedCornersF radii_;
  // LINEAR: the M3 curve is applied in AnimationProgressed, not here.
  gfx::SlideAnimation press_morph_{this};
  float current_morph_ = 0.f;
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
    // No outline. There used to be a hairline edge, because the panel sat on
    // whatever the page happened to be -- often near-black -- with nothing to
    // separate it. The dialog now sits over M3's scrim, which darkens the page
    // under it, and M3 separates a dialog from that by tone alone. The final
    // colours arrive in OnThemeChanged(), once there is a Widget to ask.
    SetBorder(views::CreateEmptyBorder(gfx::Insets(kDialogPadding)));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(),
        kDialogHeadlineGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* title = AddChildView(
        std::make_unique<views::Label>(u"Delete “" + workspace_name +
                                       u"”?"));
    title_ = title;
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    title->SetEnabledColor(foreground);
    title->SetAutoColorReadabilityEnabled(false);
    title->SetSubpixelRenderingEnabled(false);
    // headline-small: 24sp at REGULAR weight. The old semibold is the habit
    // MD3 drops -- size carries the hierarchy, not weight.
    title->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kHeadlineSmall));

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
    detail_ = detail;
    detail->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    detail->SetMultiLine(true);
    detail->SetEnabledColor(zephyrus::Muted());
    detail->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kBodyMedium));
    detail->SetAutoColorReadabilityEnabled(false);
    detail->SetSubpixelRenderingEnabled(false);

    // The body is the last thing read before deciding, so it gets a clear gap
    // from the buttons — bigger than the title-to-body gap, so the block reads
    // as "message, then choice" rather than three evenly spaced rows.
    auto* actions = AddChildView(std::make_unique<views::View>());
    actions->SetProperty(
        views::kMarginsKey,
        gfx::Insets::TLBR(kDialogActionsGap - kDialogHeadlineGap, 0, 0, 0));
    auto* actions_layout =
        actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
            kDialogButtonGap));
    actions_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kEnd);

    // TWO TEXT BUTTONS, which is MD3's basic dialog. The destructive one is
    // distinguished by COLOUR (the accent, this language's "danger"), not by
    // being the only filled control in the dialog -- a filled button next to an
    // outlined one reads as the default action, and the default here is Cancel.
    //
    // Cancel still takes focus, so a stray Enter cannot close a workspace's
    // tabs.
    // DESTRUCTIVE IS ALWAYS RED, never the theme's accent. Accent() follows the
    // theme, so under a blue theme "Delete workspace" was blue -- which makes a
    // destructive action look like every other primary action in the browser.
    //
    // Still two reds: #C6102E falls to about 3:1 on a near-black panel, so the
    // lifted dark variant is used there. Which one is picked follows the PANEL,
    // not the theme.
    const SkColor destructive = color_utils::IsDark(panel)
                                    ? zephyrus::kDarkPalette.accent
                                    : zephyrus::kLightPalette.accent;

    // Mirrored radii, so the two squared corners meet in the middle.
    const float outer = kDialogButtonHeight / 2.0f;
    const float inner = kDialogButtonInnerRadius;
    auto* cancel = actions->AddChildView(std::make_unique<ZephyrusDialogButton>(
        u"Cancel", foreground,
        gfx::RoundedCornersF(outer, inner, inner, outer),
        std::move(on_cancel)));
    actions->AddChildView(std::make_unique<ZephyrusDialogButton>(
        u"Delete workspace", destructive,
        gfx::RoundedCornersF(inner, outer, outer, inner),
        std::move(on_confirm)));
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

  // M3 dialog roles: surfaceContainerHigh, onSurface over onSurfaceVariant.
  // Here rather than in the constructor, which runs before the dialog has a
  // Widget and so before any role can be read.
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateRoundedRectBackground(
        zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHigh),
        kZephyrusDialogRadius));
    title_->SetEnabledColor(
        zephyrus::m3::Role(*this, kColorZephyrusOnSurface));
    detail_->SetEnabledColor(
        zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant));
  }

 private:
  raw_ptr<views::View> default_focus_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> detail_ = nullptr;
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


// Paints tonal M3 connected controls below their native glyphs.
class ZephyrusGlassPill : public views::View {
  METADATA_HEADER(ZephyrusGlassPill, views::View)

 public:
  ZephyrusGlassPill() {
    SetCanProcessEventsWithinSubtree(false);
    SetProperty(views::kViewIgnoredByLayoutKey, true);
  }

  // Where the groups come from, asked for at PAINT time.
  //
  // Pulling rather than being told means the containers can never lag the
  // controls after a relayout, and this class is defined below
  // ToolbarView::Layout() so it could not be handed them anyway.
  using GroupSource = base::RepeatingCallback<
      std::vector<std::vector<ZephyrusGroupSegment>>()>;
  void SetGroupSource(GroupSource source) { source_ = std::move(source); }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    if (!source_) {
      return;
    }
    // Connected XS tokens: kZephyrusGap spacing, small inner corners at rest
    // and smaller when pressed, at kZephyrusContainer height. The numbers live
    // in ZephyrusSegmentShape; do not restate them here, they drift.
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setColor(zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer));

    for (const auto& group : source_.Run()) {
      for (size_t i = 0; i < group.size(); ++i) {
        SkRRect rrect = ZephyrusSegmentShape(group, i);
        rrect.offset(-x(), -y());
        SkColor color = zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer);
        SkColor ink = zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer);
        // A disabled control keeps the container. M3 swaps it for onSurface at
        // 12%, but that assumes a far weaker container tone than our seeded
        // one: against kColorZephyrusSecondaryContainer the swap punched a
        // visible hole in the run -- back and forward are disabled on a new
        // tab, so the navigation group split into a grey half and a coloured
        // half. The run's silhouette is the point of a connected group, so the
        // container holds and only the GLYPH drops to 38%, which is where M3
        // puts disabled content anyway.
        if (group[i].hovered || group[i].pressed) {
          if (group[i].close) {
            color = zephyrus::m3::Role(*this, kColorZephyrusErrorContainer);
            ink = zephyrus::m3::Role(*this, kColorZephyrusOnErrorContainer);
          }
          color = zephyrus::m3::WithStateLayer(
              color, ink, group[i].pressed ? zephyrus::m3::kPressed
                                          : zephyrus::m3::kHover);
        }
        fill.setColor(color);
        canvas->sk_canvas()->drawRRect(rrect, fill);
      }
    }
  }

 private:
  GroupSource source_;
};

BEGIN_METADATA(ZephyrusGlassPill)
END_METADATA

}  // namespace

views::View* ToolbarView::zephyrus_new_tab_button() {
  return zephyrus_new_tab_button_;
}

std::vector<std::vector<ZephyrusGroupSegment>>
ToolbarView::ZephyrusTitlebarGroups() const {
  constexpr int kNav = 0;
  constexpr int kCaption = 1;
  constexpr int kActions = 2;
  constexpr int kShield = 3;

  auto usable = [](const views::View* v) {
    return v && v->GetVisible() && !v->bounds().IsEmpty();
  };

  // Group id per control, then a single pass in bar order.
  std::vector<std::pair<views::View*, int>> ordered;
  for (views::View* child : children()) {
    if (child->GetProperty(views::kViewIgnoredByLayoutKey) || !usable(child)) {
      continue;
    }
    if (!views::AsViewClass<views::Button>(child) &&
        !views::AsViewClass<ToolbarIconContainerView>(child)) {
      continue;
    }
    int id;
    if (child == back_ || child == forward_ || child == reload_ ||
        child == zephyrus_new_tab_button_) {
      id = kNav;
    } else if (child == zephyrus_minimize_button_ ||
               child == zephyrus_maximize_button_ ||
               child == zephyrus_close_button_) {
      id = kCaption;
    } else if (child == zephyrus_adblock_button_) {
      id = kShield;
    } else {
      id = kActions;
    }
    // Containers such as pinned actions and extensions contain independent
    // buttons. Each needs its own seam and state, not one stretched capsule.
    auto collect = [&](auto&& self, views::View* view) -> void {
      if (!usable(view)) {
        return;
      }
      if (views::AsViewClass<views::Button>(view)) {
        ordered.emplace_back(view, id);
        return;
      }
      for (views::View* descendant : view->children()) {
        self(self, descendant);
      }
    };
    collect(collect, child);
  }
  std::sort(ordered.begin(), ordered.end(),
            [this](const auto& a, const auto& b) {
              return views::View::ConvertRectToTarget(a.first, this,
                         a.first->GetLocalBounds()).x() <
                     views::View::ConvertRectToTarget(b.first, this,
                         b.first->GetLocalBounds()).x();
            });

  auto segment_for = [this](views::View* v) {
    gfx::Rect r = views::View::ConvertRectToTarget(v, this, v->GetLocalBounds());
    // Maximizing widens the outermost buttons via kInternalPaddingKey so the
    // screen corner still hits them (Fitts's law). That padding is hit area,
    // not something you can see.
    if (const gfx::Insets* pad = v->GetProperty(views::kInternalPaddingKey)) {
      r.Inset(*pad);
    }
    // Every container is the same height, and it is centred on the CONTROL,
    // not on the bar. Centring on the bar was the vertical twin of the old
    // midpoint split: a control whose cell does not sit dead centre in the
    // title bar had its glyph -- which is centred in the cell -- riding high or
    // low inside a container placed somewhere else.
    //
    // Width is left alone. Clamping each control to a square turned narrow
    // cells into vertical ovals and distorted containers holding several
    // actions; ZephyrusSegmentShape widens them instead, symmetrically.
    r.set_y(r.CenterPoint().y() - kZephyrusContainer / 2);
    r.set_height(kZephyrusContainer);
    const views::Button* button = views::AsViewClass<views::Button>(v);
    return ZephyrusGroupSegment{
        r, button && button->GetState() == views::Button::STATE_PRESSED,
        button && button->GetState() == views::Button::STATE_HOVERED,
        v->GetEnabled(), v == zephyrus_close_button_, v};
  };

  // Runs of ADJACENT controls sharing a group id. Both words matter: sharing
  // an id is what makes two controls the same KIND of thing, and sitting next
  // to each other is what lets them be drawn joined.
  //
  // No outer inset is applied to a run's ends. It shaved one side off the
  // first and last control, which is a control's CELL -- the thing the glyph
  // is centred in -- so it moved the glyph. Groups are kept apart by the
  // layout gap between them.
  std::vector<std::vector<ZephyrusGroupSegment>> groups;
  for (size_t i = 0; i < ordered.size();) {
    std::vector<ZephyrusGroupSegment> run;
    run.push_back(segment_for(ordered[i].first));
    size_t j = i + 1;
    while (j < ordered.size() && ordered[j].second == ordered[i].second) {
      ZephyrusGroupSegment next = segment_for(ordered[j].first);
      // A gap far wider than the seam is a HOLE, not a join. Measured: when
      // the pinned actions spill into the overflow menu their container keeps
      // its place in the bar, leaving 36dp of nothing between the extensions
      // button and the overflow button -- and the run drew straight across it
      // as though the two were joined.
      if (next.bounds.x() - run.back().bounds.right() > kZephyrusMaxJoin) {
        break;
      }
      run.push_back(std::move(next));
      ++j;
    }
    groups.push_back(std::move(run));
    i = j;
  }
  return groups;
}

bool ToolbarView::GetZephyrusButtonShape(const views::View* button,
                                         SkRRect* shape) const {
  if (!zephyrus_nav_pill_backdrop_) {
    return false;
  }
  for (const auto& group : ZephyrusTitlebarGroups()) {
    for (size_t i = 0; i < group.size(); ++i) {
      if (group[i].view == button) {
        *shape = ZephyrusSegmentShape(group, i);
        const gfx::Rect bounds = views::View::ConvertRectToTarget(
            button, this, button->GetLocalBounds());
        shape->offset(-bounds.x(), -bounds.y());
        return true;
      }
    }
  }
  return false;
}

void ToolbarView::AddZephyrusWindowControls() {
  // One painter tracks all visible groups, including customized actions.
  auto groups = std::make_unique<ZephyrusGlassPill>();
  groups->SetGroupSource(base::BindRepeating(
      [](ToolbarView* toolbar) { return toolbar->ZephyrusTitlebarGroups(); },
      base::Unretained(this)));
  zephyrus_nav_pill_backdrop_ = AddChildViewAt(std::move(groups), 0);
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
  // Margins only, for anything that ever lays these out generically. Their
  // actual placement is the manual SetBounds in Layout(), which ignores these
  // entirely -- the horizontal pitch lives there.
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
  // InkFor() is the house answer to "what reads on this surface", and it knows
  // about the palette's softened dark fill; a raw IsDark pick does not.
  const SkColor caption_fg = zephyrus::InkFor(effective);
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
  // The upstream buttons that come and go are in this list too, and they are
  // the reason it exists.
  //
  // They only appear when something happens: an extension is installed, a file
  // downloads, a tab starts playing audio. Each arrives carrying Chromium's own
  // toolbar styling, so a row that was uniform a moment ago gains a button with
  // different padding. The extensions one is worse than different, because
  // ExtensionsToolbarButton is a ToolbarChipButton and draws a pill container,
  // which reads as a larger icon sitting among smaller ones.
  //
  // Clearing the highlight is what suppresses that. ToolbarChipButton paints
  // its container and computes its insets in UpdateColorsAndInsets(), keyed off
  // the highlight, so dropping the highlight drops both the container and the
  // extra padding it reserved.
  ToolbarButton* const nav_buttons[] = {
      home_.get(),
      zephyrus_new_tab_button_.get(),
      zephyrus_pin_button_.get(),
      zephyrus_adblock_button_.get(),
      GetDownloadButton(),
      media_button_.get(),
      extensions_container_ ? extensions_container_->GetExtensionsButton()
                            : nullptr,
  };
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

// See the header. Nothing here touches visibility: a control that was hidden
// on the bar stays hidden in the sidebar, which is the whole point of moving
// the real views instead of standing in for them.
void ToolbarView::LendZephyrusChromeTo(views::View* host) {
  if (zephyrus_compact_ || !host) {
    return;
  }
  zephyrus_compact_ = true;

  // Snapshot first: reparenting mutates children() as we walk it.
  std::vector<views::View*> movable;
  for (views::View* child : children()) {
    // What the user added to the bar STAYS on the bar, beside the window
    // controls: extensions and pinned actions are the user's own furniture,
    // they change as they install things, and a panel that reflows every time
    // an extension asks for attention is not a layout.
    const bool stays = child == zephyrus_minimize_button_ ||
                       child == zephyrus_maximize_button_ ||
                       child == zephyrus_close_button_ ||
                       child == zephyrus_pin_button_ ||
                       child == zephyrus_nav_pill_backdrop_ ||
                       views::AsViewClass<ToolbarIconContainerView>(child);
    if (!stays) {
      movable.push_back(child);
    }
  }
  for (views::View* child : movable) {
    zephyrus_lent_children_.emplace_back(child, GetIndexOf(child).value());
  }
  for (views::View* child : movable) {
    host->AddChildView(child);
  }
  PreferredSizeChanged();
  InvalidateLayout();
}

std::vector<views::View*> ToolbarView::ZephyrusLentViews() const {
  std::vector<views::View*> views;
  views.reserve(zephyrus_lent_children_.size());
  for (const auto& [view, index] : zephyrus_lent_children_) {
    views.push_back(view);
  }
  return views;
}

void ToolbarView::ReclaimZephyrusChrome() {
  if (!zephyrus_compact_) {
    return;
  }
  zephyrus_compact_ = false;
  // ASCENDING, and this is not a detail: inserting a view at its old index
  // shifts every later sibling right by one, which is exactly what the later
  // views' own indices already assume. Restoring them back-to-front instead
  // put the bar back in a scrambled order -- the shield after the menu, the
  // omnibox after both.
  for (const auto& [view, index] : zephyrus_lent_children_) {
    AddChildViewAt(view, std::min(index, children().size()));
  }
  zephyrus_lent_children_.clear();
  UpdateZephyrusPinButton();
  PreferredSizeChanged();
  InvalidateLayout();
}void ToolbarView::AddZephyrusPinButton() {
  auto pin = std::make_unique<ZephyrusPinButton>(base::BindRepeating(
      [](ToolbarView* toolbar) {
        if (BrowserView* browser_view =
                BrowserView::GetBrowserViewForBrowser(toolbar->browser_)) {
          browser_view->ToggleZephyrusTitlebarPinned();
        }
      },
      base::Unretained(this)));
  // Placeholder only; UpdateZephyrusPinButton() below sets the state glyph.
  pin->SetVectorIcon(kZephyrusTitlebarPinnedIcon);
  // Sit in the right cluster, immediately to the LEFT of the app menu
  // (three-dots), so the title-bar pin lives with the other window-level
  // controls rather than floating beside the centered omnibox.
  const size_t index = app_menu_button_
                           ? GetIndexOf(app_menu_button_).value()
                           : GetIndexOf(location_bar_view_).value() + 1;
  zephyrus_pin_button_ = AddChildViewAt(std::move(pin), index);
  UpdateZephyrusPinButton();
}

void ToolbarView::ObserveZephyrusAdblockCount() {
  zephyrus_adblock_subscription_ = {};
  if (content::WebContents* contents =
          browser_->tab_strip_model()->GetActiveWebContents()) {
    if (auto* helper =
            zephyrus_adblock::ZephyrusAdblockTabHelper::FromWebContents(
                contents)) {
      zephyrus_adblock_subscription_ = helper->AddChangedCallback(
          base::BindRepeating(&ToolbarView::OnZephyrusAdblockCountChanged,
                              base::Unretained(this)));
    }
  }
  UpdateZephyrusAdblockBadge();
}

void ToolbarView::OnZephyrusAdblockCountChanged() {
  // Coalesced: a page load blocks requests in bursts, and repainting the
  // toolbar per blocked request would spend more time drawing the count than
  // the blocking itself costs. A badge that lags by a frame or two is
  // indistinguishable from one that does not.
  if (!zephyrus_adblock_badge_timer_.IsRunning()) {
    zephyrus_adblock_badge_timer_.Start(
        FROM_HERE, base::Milliseconds(200),
        base::BindOnce(&ToolbarView::UpdateZephyrusAdblockBadge,
                       base::Unretained(this)));
  }
}

void ToolbarView::UpdateZephyrusAdblockBadge() {
  if (!zephyrus_adblock_button_) {
    return;
  }
  int blocked = 0;
  if (content::WebContents* contents =
          browser_->tab_strip_model()->GetActiveWebContents()) {
    if (auto* helper =
            zephyrus_adblock::ZephyrusAdblockTabHelper::FromWebContents(
                contents)) {
      blocked = helper->blocked_this_page();
    }
  }
  auto* shield = static_cast<ZephyrusPinButton*>(zephyrus_adblock_button_.get());
  shield->SetZephyrusBadgeCount(blocked);

  // The count is painted, so it also has to be spoken and shown on hover —
  // otherwise the badge is information only sighted users get.
  const std::u16string name =
      blocked == 0
          ? u"Zephyrus Shield — ad & tracker blocker"
          : base::StrCat({u"Zephyrus Shield — ",
                          base::NumberToString16(blocked),
                          u" blocked on this page"});
  shield->SetTooltipText(name);
  shield->GetViewAccessibility().SetName(name);
}

void ToolbarView::UpdateZephyrusPinButton() {
  if (!zephyrus_pin_button_) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  const bool pinned =
      !browser_view || browser_view->IsZephyrusTitlebarPinned();
  // Zephyrus: the glyph now REPORTS the state instead of just offering the
  // action. One icon for both states meant the button could only be understood
  // by hovering it for the tooltip -- you could not tell from the title bar
  // whether the bar was pinned or set to auto-hide.
  //
  // Filled top strip = pinned. Short stub = hidden/auto-hide.
  zephyrus_pin_button_->SetVectorIcon(pinned ? kZephyrusTitlebarPinnedIcon
                                             : kZephyrusTitlebarHiddenIcon);
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
  if (zephyrus_workspace_strip_) {
    index = GetIndexOf(zephyrus_workspace_strip_).value() + 1;
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
  if (!zephyrus_adblock_button_ ||
      zephyrus::ConsumeReopenSuppression(zephyrus_adblock_button_)) {
    return;
  }
  zephyrus_adblock::ZephyrusAdblockService* service =
      zephyrus_adblock::ZephyrusAdblockServiceFactory::GetForBrowserContext(
          browser_->profile());
  if (!service) {
    return;
  }
  int this_page = 0;
  std::u16string page_host;
  if (content::WebContents* wc =
          browser_->tab_strip_model()->GetActiveWebContents()) {
    // Web pages only: an internal page's "host" (newtab, settings) is not a
    // site, and naming it here would read as one.
    const GURL& url = wc->GetLastCommittedURL();
    if (url.SchemeIsHTTPOrHTTPS()) {
      page_host = base::UTF8ToUTF16(url.host());
    }
    if (auto* helper =
            zephyrus_adblock::ZephyrusAdblockTabHelper::FromWebContents(wc)) {
      this_page = helper->blocked_this_page();
    }
  }

  // M3 ROLES, read from the TOOLBAR: it is in a Widget and has a
  // ColorProvider, which the bubble's own views do not have until the bubble
  // is shown. (The chips read theirs at paint time, once they are in one.)
  const SkColor kContainer =
      zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainer);
  const SkColor kOnSurface = zephyrus::m3::Role(*this, kColorZephyrusOnSurface);
  const SkColor kOnVariant =
      zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant);
  const SkColor kPrimary = zephyrus::m3::Role(*this, kColorZephyrusPrimary);
  const SkColor kOnPrimary = zephyrus::m3::Role(*this, kColorZephyrusOnPrimary);
  const SkColor kPrimaryContainer =
      zephyrus::m3::Role(*this, kColorZephyrusPrimaryContainer);
  const SkColor kOnPrimaryContainer =
      zephyrus::m3::Role(*this, kColorZephyrusOnPrimaryContainer);
  const SkColor kSecondaryContainer =
      zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer);
  const SkColor kOnSecondaryContainer =
      zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer);

  // COMPACT. This used to be 336dp wide with a stats card and four 56dp switch
  // rows, and hid a large part of the page under it. The master switch now
  // lives in the header, the three secondary settings are filter chips, and
  // the two lifetime stats are one line of supporting text.
  constexpr int kWidth = 300;
  constexpr int kInset = 16;
  constexpr int kGap = 16;

  auto content = std::make_unique<views::View>();
  content->SetBackground(
      views::CreateRoundedRectBackground(kContainer, kZephyrusDialogRadius));
  auto* col = content->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kInset), kGap));
  col->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  auto make_label = [](const std::u16string& text, SkColor color,
                       zephyrus::m3::Type type) {
    auto label = std::make_unique<views::Label>(text);
    label->SetEnabledColor(color);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetSubpixelRenderingEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
    label->SetFontList(zephyrus::m3::Font(type));
    return label;
  };

  // ---- Header: icon in a primaryContainer circle, title, master switch ----
  auto* header = content->AddChildView(std::make_unique<views::View>());
  auto* hl = header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
  hl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* badge = header->AddChildView(std::make_unique<views::ImageView>(
      ui::ImageModel::FromVectorIcon(kZephyrusShieldIcon, kOnPrimaryContainer,
                                     20)));
  badge->SetPreferredSize(gfx::Size(40, 40));
  badge->SetBackground(
      views::CreateRoundedRectBackground(kPrimaryContainer, 20));
  auto* titles = header->AddChildView(std::make_unique<views::View>());
  titles->SetLayoutManager(std::make_unique<views::BoxLayout>(
                               views::BoxLayout::Orientation::kVertical))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  titles->AddChildView(
      make_label(u"Shield", kOnSurface, zephyrus::m3::Type::kTitleMedium));
  if (!page_host.empty()) {
    auto* host = titles->AddChildView(
        make_label(page_host, kOnVariant, zephyrus::m3::Type::kBodySmall));
    host->SetElideBehavior(gfx::ELIDE_HEAD);
  }
  hl->SetFlexForView(titles, 1);

  const std::u16string kBlockingName = u"Block ads & trackers";
  auto* master = header->AddChildView(std::make_unique<zephyrus::m3::Switch>());
  // Before the callback exists, so the initial value never reads as a change.
  master->SetIsOn(service->enabled());
  master->GetViewAccessibility().SetName(kBlockingName);
  master->SetTooltipText(kBlockingName);
  master->SetCallback(base::BindRepeating(
      [](zephyrus::m3::Switch* s,
         zephyrus_adblock::ZephyrusAdblockService* service) {
        service->SetEnabled(s->GetIsOn());
      },
      base::Unretained(master), base::Unretained(service)));

  // ---- Hero: the page's count on an expressive shape ----------------------
  auto* hero = content->AddChildView(std::make_unique<views::View>());
  auto* herol = hero->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 16));
  herol->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  hero->AddChildView(std::make_unique<ZephyrusShieldCookie>(
      this_page > 999 ? u"999+" : base::FormatNumber(this_page), kPrimary,
      kOnPrimary));
  auto* hero_text = hero->AddChildView(std::make_unique<views::View>());
  hero_text->SetLayoutManager(std::make_unique<views::BoxLayout>(
                                  views::BoxLayout::Orientation::kVertical,
                                  gfx::Insets(), 2))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  herol->SetFlexForView(hero_text, 1);
  hero_text->AddChildView(make_label(u"blocked on this page", kOnSurface,
                                     zephyrus::m3::Type::kBodyLarge));
  auto* lifetime = hero_text->AddChildView(make_label(
      base::FormatNumber(static_cast<int64_t>(service->total_blocked())) +
          u" blocked in total",
      kOnVariant, zephyrus::m3::Type::kBodySmall));
  lifetime->SetMultiLine(true);
  auto* rules = hero_text->AddChildView(make_label(
      base::FormatNumber(static_cast<int64_t>(service->rule_count())) +
          u" rules active",
      kOnVariant, zephyrus::m3::Type::kBodySmall));
  rules->SetMultiLine(true);
  // The whole line is one statement to a screen reader.
  hero->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  hero->GetViewAccessibility().SetName(
      base::FormatNumber(this_page) + u" blocked on this page");

  // ---- Chips: the three secondary settings --------------------------------
  auto* chips = content->AddChildView(std::make_unique<ZephyrusChipGroup>());
  auto add_chip = [&](const std::u16string& label,
                      const std::u16string& accessible_name,
                      const gfx::VectorIcon& icon, bool selected,
                      base::RepeatingCallback<void(bool)> on_change) {
    auto* chip = chips->AddChildView(std::make_unique<ZephyrusFilterChip>(
        label, accessible_name, icon, selected));
    chip->SetCallback(base::BindRepeating(
        [](ZephyrusFilterChip* c, base::RepeatingCallback<void(bool)> cb) {
          cb.Run(c->selected());
        },
        base::Unretained(chip), std::move(on_change)));
  };
  add_chip(u"Pop-ups", u"Aggressive pop-up blocking", kOpenInNewIcon,
           service->aggressive_popup_blocking(),
           base::BindRepeating(
               [](zephyrus_adblock::ZephyrusAdblockService* s, bool on) {
                 s->SetAggressivePopupBlocking(on);
               },
               base::Unretained(service)));
  add_chip(u"Cookies", u"Block third-party cookies", vector_icons::kCookieIcon,
           service->third_party_cookie_blocking(),
           base::BindRepeating(
               [](zephyrus_adblock::ZephyrusAdblockService* s, bool on) {
                 s->SetThirdPartyCookieBlocking(on);
               },
               base::Unretained(service)));

  // Force encrypted DNS (DoH "secure"). DoH is a single browser-wide setting --
  // it can't be scoped to one workspace -- so this affects every tab. Default
  // is "automatic" (DoH when the network supports it, safe fallback); "secure"
  // always uses DoH and fails closed, which can break captive-portal and
  // DoH-blocking networks. Off the record, this pref reads through to local
  // state, so read/write the real local state directly.
  if (PrefService* local_state = g_browser_process->local_state()) {
    add_chip(u"Secure DNS",
             l10n_util::GetStringUTF16(IDS_ZEPHYRUS_ALWAYS_ENCRYPTED_DNS),
             vector_icons::kLockIcon,
             local_state->GetString(prefs::kDnsOverHttpsMode) ==
                 SecureDnsConfig::kModeSecure,
             base::BindRepeating([](bool on) {
               if (PrefService* ls = g_browser_process->local_state()) {
                 ls->SetString(prefs::kDnsOverHttpsMode,
                               on ? SecureDnsConfig::kModeSecure
                                  : SecureDnsConfig::kModeAutomatic);
               }
             }));
  }

  // Privacy Intelligence (§6.1). Entry point rather than inline content: the
  // Shield panel answers "what is the blocker doing", and the privacy report
  // answers "what happened on this page" -- related, but two different
  // questions, and merging them would make both longer and neither clearer.
  if (zephyrus_privacy::IsCollectionEnabled()) {
    auto* footer = content->AddChildView(std::make_unique<views::View>());
    footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
                                 views::BoxLayout::Orientation::kHorizontal))
        ->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kEnd);
    footer->AddChildView(std::make_unique<ZephyrusTonalButton>(
        base::BindRepeating(
            [](ToolbarView* toolbar) {
              // No explicit close of this bubble, and no posted task:
              // BubbleDialogDelegate closes on deactivation, so the Shield
              // panel dismisses itself once the report takes focus. Calling
              // Close() here would tear down the view that owns this very
              // callback.
              toolbar->ShowZephyrusPrivacyBubble();
            },
            base::Unretained(this)),
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_TITLE),
        kArrowForwardIcon, kSecondaryContainer, kOnSecondaryContainer));
  }

  content->SetPreferredSize(
      gfx::Size(kWidth, content->GetHeightForWidth(kWidth)));

  // TOP_CENTER, not TOP_RIGHT: the nub sits at the middle of the popup's top
  // edge, so the popup itself has to be centred under the shield for the nub to
  // land on it. Anchoring by a corner put the popup off to one side and the nub
  // wherever the corner happened to be.
  auto bubble = std::make_unique<views::BubbleDialogDelegate>(
      zephyrus_adblock_button_, views::BubbleBorder::TOP_CENTER,
      views::BubbleBorder::STANDARD_SHADOW, /*autosize=*/true);
  bubble->SetShowCloseButton(false);
  bubble->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble->set_margins(gfx::Insets());
  zephyrus::ConfigureBubble(bubble.get());
  bubble->SetBackgroundColor(kContainer);
  bubble->SetContentsView(std::move(content));
  views::BubbleDialogDelegate* bubble_ptr = bubble.get();
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  zephyrus::ApplyBubbleFrame(bubble_ptr);
  // The shield popup hangs off the shield button, so it earns a nub.
  zephyrus::ApplyAnchoredNub(bubble_ptr);
  widget->Show();
  AnimateZephyrusBubbleIn(widget);
}

void ToolbarView::ShowZephyrusPrivacyBubble() {
  if (!zephyrus_adblock_button_ || !browser_) {
    return;
  }
  // Anchored to the Shield button: it is the surface this panel belongs to,
  // and the one the user just came from.
  zephyrus_privacy::ShowPrivacyPopup(browser_, zephyrus_adblock_button_);
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
  // Holds content -> card radius. Was 7, mid-range.
  static constexpr float kRadius =
      static_cast<float>(zephyrus::kRadiusCard);

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

  SkColor ink_ = zephyrus::Ink();
  SkColor fill_ = zephyrus::Surface();
};

BEGIN_METADATA(ZephyrusInlineNameField)
END_METADATA


// The workspace index, drawn as dots.
//
// This replaces the coloured swatch that used to identify a workspace. One
// accent means hue is not available for identity, so count and position carry
// it instead -- and the dots sit on the same grid the rest of the interface is
// constructed on, which is the one place in browser chrome where the dot motif
// is doing work rather than decoration.
class ZephyrusWorkspaceDots : public views::View {
  METADATA_HEADER(ZephyrusWorkspaceDots, views::View)

 public:
  ZephyrusWorkspaceDots() { SetCanProcessEventsWithinSubtree(false); }

  void SetDots(int count, SkColor ink) {
    if (count == count_ && ink == ink_) {
      return;
    }
    count_ = count;
    ink_ = ink;
    PreferredSizeChanged();
    SchedulePaint();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override {
    // Whole numbers of device pixels at 100%: a 3px dot on a 5px pitch. Halves
    // here blur at fractional scales, which is the failure this project has hit
    // before with layer-rounded radii.
    return gfx::Size(count_ * kPitch - (kPitch - kDot), kDot);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(ink_);
    const float r = kDot / 2.f;
    for (int i = 0; i < count_; ++i) {
      canvas->DrawCircle(gfx::PointF(i * kPitch + r, height() / 2.f), r, flags);
    }
  }

 private:
  static constexpr int kDot = 3;
  static constexpr int kPitch = 5;
  int count_ = 1;
  SkColor ink_ = SK_ColorBLACK;
};

BEGIN_METADATA(ZephyrusWorkspaceDots)
END_METADATA


// Title-bar workspace switcher: workspace name followed by a trailing dropdown
// chevron (views::LabelButton can't place an image after the label, so this is
// a Button hosting a Label + trailing ImageView).
// Icon picker for a workspace, in the shape Zen uses: a grid you pick from,
// not a text field you paste into.
//
// The editor already had an emoji FIELD, and a field is the wrong control for
// this -- it asks the user to produce an emoji from somewhere (an OS picker, a
// copy-paste) before they can use the feature at all. A grid makes the whole
// interaction one click.
//
// The set is deliberately small and generic. A long list turns picking into
// searching, and these have to read at 13px in a title bar, so anything
// detailed is a smudge regardless of how good it looks in the picker.
// One cell of the icon picker: the glyph, stroked, on a hover wash.
//
// A LabelButton cannot show these -- the glyphs are paths, not text -- so this
// is the smallest button that can paint one.
class ZephyrusIconSwatch : public views::Button {
  METADATA_HEADER(ZephyrusIconSwatch, views::Button)

 public:
  ZephyrusIconSwatch(const zephyrus::WorkspaceIcon& icon,
                     SkColor ink,
                     base::RepeatingClosure on_pick)
      : views::Button(base::BindRepeating(
            [](base::RepeatingClosure cb, const ui::Event&) { cb.Run(); },
            std::move(on_pick))),
        icon_(icon),
        ink_(ink) {
    SetAnimateOnStateChange(false);
    SetPreferredSize(gfx::Size(kSwatch, kSwatch));
    GetViewAccessibility().SetName(std::u16string(icon.label));
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    gfx::RectF body(GetLocalBounds());
    const bool hot =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (hot) {
      // The swatch previews the SHAPE as well as the glyph, so picking an icon
      // also shows what the active indicator will become.
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setColor(SkColorSetA(ink_, 0x1F));
      canvas->DrawPath(zephyrus::ShapePath(icon_->shape, body, 1.f), flags);
    }
    gfx::RectF glyph = body;
    glyph.Inset(kGlyphInset);
    zephyrus::PaintWorkspaceGlyph(canvas, *icon_, glyph, ink_, 1.5f);
  }

 private:
  static constexpr int kSwatch = 30;
  static constexpr float kGlyphInset = 5.f;
  const raw_ref<const zephyrus::WorkspaceIcon> icon_;
  SkColor ink_;
};

BEGIN_METADATA(ZephyrusIconSwatch)
END_METADATA

// An M3 TEXT BUTTON from a plain LabelButton: `primary` text, 40dp, 12dp of
// padding, and a pill state layer. The picker's two actions were body text in
// onSurface and onSurfaceVariant -- they read as captions, not as things to
// press.
static void StyleAsM3TextButton(views::LabelButton* button, SkColor primary) {
  button->SetEnabledTextColors(primary);
  button->SetMinSize(gfx::Size(0, 40));
  button->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 12)));
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  button->SetInstallFocusRingOnFocus(true);
  views::InstallPillHighlightPathGenerator(button);
  views::InkDropHost* const ink = views::InkDrop::Get(button);
  ink->SetMode(views::InkDropHost::InkDropMode::ON);
  ink->SetBaseColor(primary);
  ink->SetHighlightOpacity(zephyrus::m3::kHover / 255.0f);
  ink->SetVisibleOpacity(zephyrus::m3::kPressed / 255.0f);
}

class ZephyrusIconPicker : public views::BubbleDialogDelegateView {
  METADATA_HEADER(ZephyrusIconPicker, views::BubbleDialogDelegateView)

 public:
  using PickCallback = base::RepeatingCallback<void(const std::u16string&)>;

  ZephyrusIconPicker(views::View* anchor,
                     PickCallback on_pick,
                     base::RepeatingClosure on_choose_photo)
      : views::BubbleDialogDelegateView(anchor,
                                        views::BubbleBorder::TOP_CENTER),
        on_pick_(std::move(on_pick)),
        on_choose_photo_(std::move(on_choose_photo)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_margins(gfx::Insets(10));
    zephyrus::ConfigureBubble(this);
    // M3 ROLES, read from the ANCHOR: it is in the browser window and has a
    // ColorProvider, which this bubble does not until it is shown.
    const SkColor on_surface =
        zephyrus::m3::Role(*anchor, kColorZephyrusOnSurface);
    const SkColor primary = zephyrus::m3::Role(*anchor, kColorZephyrusPrimary);
    SetBackgroundColor(
        zephyrus::m3::Role(*anchor, kColorZephyrusSurfaceContainer));

    // DRAWN icons, not emoji.
    //
    // The emoji grid this replaces had a standing problem the comment here used
    // to describe: an emoji is an image, it cannot be recoloured, and a pale one
    // vanished the moment its workspace became active and the disc went white.
    // Two had to be struck from the list for that reason alone.
    //
    // A stroked glyph takes whatever colour it is given, so that whole class of
    // problem is gone rather than worked around -- and the set can be wider,
    // because nothing has to be screened out for being too light.
    constexpr int kPerRow = 7;

    // Rows of BoxLayout rather than a TableLayout: the grid is fixed-size and
    // uniform, so a table buys nothing and TableLayout is not reachable from
    // this translation unit.
    auto* grid = AddChildView(std::make_unique<views::View>());
    grid->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    views::View* row = nullptr;
    int in_row = 0;
    for (const zephyrus::WorkspaceIcon& icon : zephyrus::AllWorkspaceIcons()) {
      if (!row || in_row == kPerRow) {
        row = grid->AddChildView(std::make_unique<views::View>());
        row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
        in_row = 0;
      }
      // The KEY is what gets stored, not the label -- see WorkspaceIcon::key
      // for why it goes in the field that used to hold an emoji.
      const std::u16string key = base::ASCIIToUTF16(std::string(icon.key));
      auto* swatch = row->AddChildView(
          std::make_unique<ZephyrusIconSwatch>(
              icon, on_surface,
              base::BindRepeating(
                  [](ZephyrusIconPicker* self, std::u16string k) {
                    self->Pick(k);
                  },
                  base::Unretained(this), key)));
      swatch->SetTooltipText(icon.label);
      ++in_row;
    }

    // The photo route. It sits BELOW the emoji grid rather than beside it
    // because it is the slower path -- it opens a file dialog, and everything
    // above it is one click. Putting a dialog-opening control in the middle of
    // a grid of instant ones makes the grid feel inconsistent.
    auto* photo = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(
            [](ZephyrusIconPicker* self, const ui::Event&) {
              self->ChoosePhoto();
            },
            base::Unretained(this)),
        u"Choose a photo…"));
    photo->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    photo->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(10, 0, 0, 0));
    StyleAsM3TextButton(photo, primary);

    // Clearing is a first-class choice: a workspace that went back to being
    // "3" should not require deleting and recreating it.
    auto* clear = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(
            [](ZephyrusIconPicker* self, const ui::Event&) {
              self->Pick(std::u16string());
            },
            base::Unretained(this)),
        u"Use the number instead"));
    clear->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    clear->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(4, 0, 0, 0));
    StyleAsM3TextButton(clear, primary);

    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  }

 private:
  void Pick(const std::u16string& glyph) {
    on_pick_.Run(glyph);
    Close();
  }

  // Closes FIRST, then opens the file dialog.
  //
  // Order matters: the file dialog is modal to the window, and leaving a bubble
  // open behind it means the bubble loses activation and dismisses itself
  // anyway -- but on some paths that dismissal destroys `this` while the
  // callback below is still running. Closing deliberately, and running the
  // callback afterwards through a copy, keeps the sequence under our control.
  void ChoosePhoto() {
    base::RepeatingClosure open = on_choose_photo_;
    Close();
    if (open) {
      open.Run();
    }
  }

  void Close() {
    if (GetWidget()) {
      GetWidget()->CloseWithReason(
          views::Widget::ClosedReason::kAcceptButtonClicked);
    }
  }

  PickCallback on_pick_;
  base::RepeatingClosure on_choose_photo_;
};

BEGIN_METADATA(ZephyrusIconPicker)
END_METADATA

// One number in the strip.
class ZephyrusWorkspaceCell : public views::Button,
                              public views::ContextMenuController {
  // No AnimationDelegateViews base: views::Button ALREADY derives from it, and
  // adding it again is an ambiguous base rather than a second delegate. The
  // consequence is that Button's own hover animation and ours arrive at the
  // same callback -- see AnimationProgressed, which has to tell them apart.
  METADATA_HEADER(ZephyrusWorkspaceCell, views::Button)

 public:
  ZephyrusWorkspaceCell(const std::u16string& glyph,
                        bool active,
                        bool is_icon,
                        SkColor ink,
                        PressedCallback callback,
                        base::RepeatingClosure on_context_menu = {},
                        gfx::ImageSkia photo = gfx::ImageSkia(),
                        bool animate_entrance = false)
      : views::Button(std::move(callback)),
        active_(active),
        is_icon_(is_icon),
        ink_(ink),
        photo_(std::move(photo)),
        on_context_menu_(std::move(on_context_menu)) {
    SetAnimateOnStateChange(false);
    if (on_context_menu_) {
      set_context_menu_controller(this);
    }
    // A photo REPLACES the glyph rather than sitting behind it. The label is
    // still created (Layout and the colour code below both assume it exists)
    // but is left empty, which is cheaper than making every one of them
    // null-check a pointer that is non-null in all but one case.
    label_ = AddChildView(
        std::make_unique<views::Label>(photo_.isNull() ? glyph
                                                       : std::u16string()));
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetSubpixelRenderingEnabled(false);
    // An icon fills the disc; a numeral sits in it. Same cell, different
    // optical size -- an emoji at the numeral's point size looks lost.
    label_->SetFontList(gfx::FontList({"Segoe UI"}, gfx::Font::NORMAL,
                                      is_icon ? kPillFontSize + 2
                                              : kPillFontSize,
                                      gfx::Font::Weight::MEDIUM));
    // Alignment belongs in the constructor, not Layout(): it never changes, and
    // setting it during layout meant the FIRST paint used the default
    // (left/baseline) before layout corrected it -- part of why the glyph
    // looked off-centre.
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label_->SetVerticalAlignment(gfx::ALIGN_MIDDLE);

    // A DRAWN icon replaces the label entirely -- it is stroked in
    // OnPaintBackground, so the label would only add an empty box.
    icon_ = zephyrus::FindWorkspaceIcon(glyph);
    if (icon_) {
      label_->SetText(std::u16string());
    }

    // Colour is applied in OnThemeChanged, NOT here. The selected numeral now
    // takes on-secondary-container, which has to be read from the
    // ColorProvider -- and a View has none inside its own constructor, so
    // asking here would return the Role() sentinel and paint magenta.
    SetPreferredSize(gfx::Size(kCell, kCell));

    if (icon_) {
      // LINEAR: the effect curves are keyframes applied inside the painter, so
      // this only has to supply an even 0..1 clock.
      glyph_anim_.SetTweenType(gfx::Tween::LINEAR);
      glyph_anim_.SetSlideDuration(zephyrus::EffectDuration(icon_->effect));
      // Fired by a WORKSPACE SWITCH, not by hover.
      //
      // The strip is rebuilt wholesale whenever anything about the workspace
      // list changes, so "this cell was just constructed" is not on its own a
      // switch -- a rename or an icon change rebuilds it too, and so does
      // opening the window. The strip decides, by comparing the current
      // workspace against the one it drew last time, and only the cell that
      // just became current is told to animate.
      if (animate_entrance) {
        glyph_anim_.Reset(0.0);
        glyph_anim_.Show();
      }
    }
  }

  void Layout(PassKey) override { label_->SetBoundsRect(GetLocalBounds()); }

  // views::AnimationDelegateViews, via views::Button:
  //
  // Button routes its OWN hover animation through here too, so anything that is
  // not ours has to be forwarded or the button's built-in state animation stops
  // working.
  void AnimationProgressed(const gfx::Animation* animation) override {
    if (animation == &glyph_anim_) {
      SchedulePaint();
      return;
    }
    views::Button::AnimationProgressed(animation);
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    if (animation == &glyph_anim_) {
      // ONE cycle, whatever the effect. The continuous ones (breathe, pulse,
      // orbit) used to loop for as long as the cursor stayed; a switch is an
      // event rather than a state, so there is nothing to keep looping for --
      // one breath, one pulse, one full turn, then rest.
      SchedulePaint();
      return;
    }
    views::Button::AnimationEnded(animation);
  }

  // The cell's edge length, for callers that need to ask for a photo at the
  // right size. Exposed rather than duplicated: a photo requested at a size the
  // cell does not use is either blurry or wasteful, and nothing would catch it.
  static constexpr int size() { return kCell; }

  // views::View:
  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    ApplyLabelColor();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool hot =
        GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;

    // A DRAWN icon is its own path: container plus stroked glyph, both painted
    // here. It never reaches the disc code below, which exists to sit behind an
    // emoji or a photo.
    if (icon_) {
      PaintIconCell(canvas, hot);
      return;
    }

    // An idle cell draws no disc -- but a photo cell still has its photo, which
    // is the cell's entire content. Returning before PaintPhoto() here is what
    // made photo workspaces invisible until you hovered them.
    if (!active_ && !hot) {
      PaintPhoto(canvas);
      return;
    }

    gfx::RectF body(GetLocalBounds());
    body.Inset(1.f);
    const float radius = body.height() / 2.f;

    cc::PaintFlags flags;
    flags.setAntiAlias(true);

    // ONE shape for every cell: a white disc, full strength when active and a
    // faint wash on hover.
    //
    // Icon cells briefly drew a RING instead, to keep a white emoji from
    // vanishing into a white disc. It solved that, and looked wrong doing it --
    // a ring beside filled circles reads as an unfinished state rather than a
    // deliberate variant, and inconsistency is more visible than the rare
    // pale icon it was protecting.
    //
    // The pale-icon problem is fixed where it actually belongs: the picker no
    // longer OFFERS an icon that disappears on white. Constraining the input is
    // cheaper than special-casing the output.
    flags.setStyle(cc::PaintFlags::kFill_Style);
    // SECONDARY CONTAINER for the selected cell, M3's role for a selected
    // navigation item -- which is what this strip is.
    //
    // It was an INVERTED INK disc: full-strength neutral with the glyph flipped
    // to the ground colour. That reads as a chip that has been switched off
    // rather than a destination you are currently in, and it is the one place
    // in the browser still using inversion to mean "selected".
    //
    // Hover on an unselected cell is now M3's 8% state layer instead of a
    // hand-picked 18%, so it matches every other hover in the browser.
    flags.setColor(
        active_ ? zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer)
                : zephyrus::m3::StateLayer(
                      zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
                      zephyrus::m3::kHover));
    canvas->DrawRoundRect(body, radius, flags);
    PaintPhoto(canvas);
  }

  // The container's silhouette IS the current-workspace indicator.
  //
  // This replaces a white disc that was either drawn or not. The shape carries
  // the state instead: at rest every cell is a circle, and the current one has
  // morphed into the silhouette its icon was assigned. Hover morphs part of the
  // way, which is what makes the cell feel like it is offering to become the
  // selection rather than just lighting up.
  //
  // No animation loop. The morph amount is a function of state, and Views
  // repaints on state change -- the CSS original got its motion from a
  // transition, which is the one part that does not survive the port. A timer
  // to tween it would be the obvious next step if it reads as abrupt.
  void PaintIconCell(gfx::Canvas* canvas, bool hot) {
    gfx::RectF body(GetLocalBounds());
    body.Inset(1.f);
    if (body.IsEmpty()) {
      return;
    }

    // The container is a STATE, not an animation. Only the glyph moves --
    // the shape morph that used to be animated here was doing too much at
    // 28px, and two things moving at once in a cell that small reads as noise.
    const float morph = active_ ? 1.f : (hot ? kHoverMorph : 0.f);
    const SkPath container = zephyrus::ShapePath(icon_->shape, body, morph);

    // Same two tonal strengths the disc had: solid when current, a faint wash
    // when hovered, nothing at rest.
    if (active_ || hot) {
      cc::PaintFlags fill;
      fill.setAntiAlias(true);
      fill.setStyle(cc::PaintFlags::kFill_Style);
      // Same roles as the disc above, for the same reason.
      fill.setColor(
          active_ ? zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer)
                  : zephyrus::m3::StateLayer(
                        zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
                        zephyrus::m3::kHover));
      canvas->DrawPath(container, fill);
    }

    // ON-secondary-container, the paired ink for the fill above. Pairing is
    // what guarantees contrast: picking the container from a role and the ink
    // from somewhere else is how a themed surface ends up unreadable.
    const SkColor glyph_color =
        active_ ? zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer)
                : ink_;
    gfx::RectF glyph_box = body;
    // PROPORTIONAL, not the fixed 4dp it was. The cell is 28dp on the title
    // bar but about 21 in a 180dp sidebar, where a 4dp inset eats 40% of the
    // cell instead of 28% -- which is how four workspace icons became four
    // grey specks. Scaling keeps the glyph the same share of whatever cell it
    // lands in.
    glyph_box.Inset(body.height() * (kGlyphInset / (kCell - 2.f)));
    // A negative progress is the resting glyph. Only a hovered cell animates.
    const float progress = glyph_anim_.is_animating()
                               ? static_cast<float>(
                                     glyph_anim_.GetCurrentValue())
                               : -1.f;
    zephyrus::PaintWorkspaceGlyph(canvas, *icon_, glyph_box, glyph_color,
                                  kGlyphStroke, progress);
  }

  // Drawn in the BACKGROUND pass, after the disc above it, rather than in
  // OnPaint -- View::OnPaint is final, and there is nothing to paint over: a
  // photo cell has an empty label, so the background pass is the whole cell.
  void PaintPhoto(gfx::Canvas* canvas) {
    if (photo_.isNull()) {
      return;
    }
    // Inset ALWAYS, active or not, so the disc painted behind it shows as a
    // white rim when this workspace is current. Insetting only when active
    // would make the photo change size as you switch workspaces, which reads as
    // a glitch rather than as a state.
    gfx::Rect body = GetLocalBounds();
    body.Inset(kPhotoInset);
    if (body.IsEmpty()) {
      return;
    }

    // Clipped to a circle rather than drawn as a rounded square: the emoji
    // cells beside it are circles, and one square among them is the kind of
    // inconsistency that is more visible than whatever it was meant to solve.
    const gfx::Point center = body.CenterPoint();
    const SkPath clip =
        SkPathBuilder()
            .addCircle(SkPoint::Make(center.x(), center.y()), body.width() / 2.f)
            .detach();
    canvas->Save();
    canvas->ClipPath(clip, /*do_anti_alias=*/true);
    // The stored image is square and `body` is square, so this scales without
    // distorting. Filtering is on because the stored size is deliberately
    // larger than the cell -- see zephyrus_workspace_image.h.
    canvas->DrawImageInt(photo_, 0, 0, photo_.width(), photo_.height(),
                         body.x(), body.y(), body.width(), body.height(),
                         /*filter=*/true);
    canvas->Restore();
  }

 private:
  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(views::View* source,
                                  const gfx::Point& point,
                                  ui::mojom::MenuSourceType source_type) override {
    if (on_context_menu_) {
      on_context_menu_.Run();
    }
  }

  // Square, so the disc is a circle, and the same height as the Shield pill
  // beside it. The disc is this minus the 1px inset on each side.
  static constexpr int kCell = kPillHeight;
  // Leaves a 2px rim of the disc showing around the photo when active.
  static constexpr int kPhotoInset = 2;
  // How much of the cell the glyph gives back to the container. A lobed
  // silhouette (cookie, burst) pulls IN between its lobes, so a glyph sized to
  // the circle would poke through those valleys.
  //
  // 4, down from 5. Combined with the larger cell the glyph goes from 14px to
  // 20px -- about 40% more, which is what makes the shape animations legible
  // at arm's length. Still enough clearance for the lobed silhouettes above.
  static constexpr float kGlyphInset = 4.f;
  // Screen pixels, not scaled with the cell -- see PaintWorkspaceGlyph.
  static constexpr float kGlyphStroke = 1.5f;
  // How far a hovered (but not current) cell's container sits toward its
  // shape. A fixed state, not a tween -- see PaintIconCell.
  static constexpr float kHoverMorph = 0.45f;

  // Drives one cycle of the glyph's effect, 0..1. The curves live in the
  // painter; this only supplies the clock.
  gfx::SlideAnimation glyph_anim_{this};
  // Null unless the stored string names one of our drawn icons; an emoji or a
  // numeral leaves this null and takes the label path.
  raw_ptr<const zephyrus::WorkspaceIcon> icon_ = nullptr;
  // An EMOJI is an image: it cannot be recoloured, so it never takes the
  // paired ink. Only a numeral does.
  void ApplyLabelColor() {
    if (is_icon_) {
      label_->SetEnabledColor(ink_);
      return;
    }
    label_->SetEnabledColor(
        active_ ? zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer)
                : SkColorSetA(ink_, 0x8C));
  }

  bool active_;
  bool is_icon_;
  SkColor ink_;
  gfx::ImageSkia photo_;
  base::RepeatingClosure on_context_menu_;
  raw_ptr<views::Label> label_ = nullptr;
};

BEGIN_METADATA(ZephyrusWorkspaceCell)
END_METADATA

// The workspace switcher, as a tiling window manager does it.
//
// A row of numbers, one per workspace, with the current one marked. Click a
// number, go there. No dropdown, no chevron, no menu -- the whole state and the
// whole control are the same few pixels, which is the entire appeal of the
// pattern: you can see how many workspaces exist and which one you are in
// without opening anything.
//
// This replaces a pill that showed only the CURRENT workspace's name and hid
// the rest behind a menu. That is one more click to answer "where am I in the
// set", and the set is small enough that it never needed hiding.
class ZephyrusWorkspaceStrip : public views::View,
                               public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(ZephyrusWorkspaceStrip, views::View)

 public:
  using SwitchCallback = base::RepeatingCallback<void(int workspace_id)>;

  // What the strip drew last time, so a rebuild can tell a real workspace
  // switch from a rename or a window opening. See SetWorkspaces.
  int last_current_id_ = 0;
  // Narrow host (the sidebar): cells share the width, and the host owns the
  // add button. See SetCompact.
  bool compact_ = false;
  bool drew_once_ = false;
  // What the strip last drew; see SetWorkspaces.
  std::string last_signature_;

  using WorkspaceCallback = base::RepeatingCallback<void(int workspace_id)>;

  // In a 230dp sidebar the strip cannot keep the bar's habits: four
  // workspaces and a trailing + are wider than the panel, and a BoxLayout
  // that cannot fit its children simply cuts the last ones off -- which is how
  // the add button disappeared. Compact mode lets the cells SHARE the width
  // and shrink, and hands the + to the host, which keeps it where it cannot be
  // cut off.
  void SetCompact(bool compact) {
    if (compact_ == compact) {
      return;
    }
    compact_ = compact;
    // The caller refreshes the list right after (ToolbarView::
    // SetZephyrusWorkspaceStripCompact), which is what rebuilds the cells.
  }

  ZephyrusWorkspaceStrip(SwitchCallback on_switch,
                         base::RepeatingClosure on_add,
                         WorkspaceCallback on_pick_icon,
                         WorkspaceCallback on_delete)
      : on_switch_(std::move(on_switch)),
        on_add_(std::move(on_add)),
        on_pick_icon_(std::move(on_pick_icon)),
        on_delete_(std::move(on_delete)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(kPillPadV, kPillPadH), kCellGap));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
  }

  // Rebuilt wholesale on change: the list is a handful of items, and diffing
  // it would be more code than redrawing it.
  // `image_for` resolves a workspace's photo. Passed as a callback rather than
  // reaching for the manager here so the strip stays a pure view -- and because
  // the lookup is what kicks off an async load, which is not something a
  // rebuild-everything method should own.
  using ImageLookup = base::RepeatingCallback<gfx::ImageSkia(int)>;

  void SetWorkspaces(const std::vector<ZephyrusWorkspaceManager::Workspace>& ws,
                     int current_id,
                     SkColor ink,
                     const ImageLookup& image_for) {
    // Did the CURRENT workspace actually change since the last rebuild?
    //
    // This is the whole trigger for the glyph animation, and it has to be
    // decided here because a cell cannot tell. Rebuilds happen for renames,
    // icon changes, added and deleted workspaces, and on window open -- in all
    // of those the active cell is newly constructed but nothing was switched.
    //
    // drew_once_ keeps the first rebuild silent: a window that animates its
    // workspace icon while it is still opening looks like a glitch, not a
    // response to anything the user did.
    // Nothing drawn changed: keep the cells.
    //
    // A rebuild destroys every cell and makes new ones, which relays out the
    // whole window (the strip's preferred size is part of the title bar's) and
    // drops keyboard focus, hover and any open tooltip on the strip. MEASURED:
    // a 5-second poll ran this unconditionally, so every window paid a full
    // relayout every 5 seconds forever, with nothing on screen changing.
    std::vector<gfx::ImageSkia> photos;
    photos.reserve(ws.size());
    std::string signature = base::StrCat(
        {base::NumberToString(current_id), "|",
         base::NumberToString(static_cast<uint32_t>(ink)), "|",
         compact_ ? "c" : "f"});
    for (const auto& w : ws) {
      photos.push_back(image_for ? image_for.Run(w.id) : gfx::ImageSkia());
      base::StrAppend(
          &signature,
          {"|", base::NumberToString(w.id), ":", base::UTF16ToUTF8(w.name), ":",
           base::UTF16ToUTF8(w.emoji), ":", w.image, ":",
           photos.back().isNull() ? "0" : "1"});
    }
    if (drew_once_ && signature == last_signature_) {
      return;
    }
    last_signature_ = std::move(signature);

    const bool switched = drew_once_ && current_id != last_current_id_;
    drew_once_ = true;
    last_current_id_ = current_id;

    RemoveAllChildViews();
    for (size_t i = 0; i < ws.size(); ++i) {
      const auto& w = ws[i];
      const bool active = w.id == current_id;
      // What each cell shows, in order of precedence:
      //   emoji, if the user set one -- that is the point of setting it
      //   name,  if the user chose one
      //   the POSITION, otherwise
      //
      // The position is computed here, from the list being drawn, so it can
      // never disagree with the order on screen. Names that are purely digits
      // are treated as unnamed: earlier builds baked the number INTO the name,
      // and those stored values are what produced a strip reading "3 2 3".
      // Ignoring them renumbers existing workspaces correctly with no
      // migration step.
      // A photo outranks everything: it is the most deliberate choice on
      // offer, and the model already guarantees a workspace has a photo or an
      // emoji but never both.
      gfx::ImageSkia photo = photos[i];
      const bool numeric_name =
          !w.name.empty() &&
          std::ranges::all_of(w.name, [](char16_t c) {
            return c >= u'0' && c <= u'9';
          });
      std::u16string glyph;
      if (!w.emoji.empty()) {
        glyph = w.emoji;
      } else if (!w.name.empty() && !numeric_name && !compact_) {
        // A NAME is the first thing to go in a narrow host. At the sidebar's
        // 180dp minimum a name has nowhere to go but an ellipsis, and four
        // cells reading "..." identify nothing; the position always fits and
        // always distinguishes them. The name is still on the heading above
        // the tab list, and in the cell's tooltip.
        glyph = w.name;
      } else {
        glyph = base::NumberToString16(i + 1);
      }
      // Right-click opens the per-workspace menu. Deleting the LAST workspace
      // is not offered: the browser always has one, and a menu item that
      // silently does nothing is worse than an absent one.
      const bool can_delete = ws.size() > 1;
      auto* cell = AddChildView(std::make_unique<ZephyrusWorkspaceCell>(
          glyph, active, /*is_icon=*/!w.emoji.empty(), ink,
          base::BindRepeating(on_switch_, w.id),
          base::BindRepeating(&ZephyrusWorkspaceStrip::ShowCellMenu,
                              base::Unretained(this), w.id, can_delete),
          std::move(photo),
          /*animate_entrance=*/switched && active));
      cell->SetTooltipText(u"Workspace " + base::NumberToString16(i + 1));
    }

    // Compact cells are placed by Layout() below, which sizes them to the
    // host. Flexing them under the BoxLayout instead is what produced the
    // unreadable strip: BoxLayout takes the deficit out of WIDTH alone, so a
    // 28x28 cell became 18x28 and the round icon inside it was squashed flat.
    if (compact_) {
      return;
    }

    // Trailing +. Inline rather than behind a menu, for the same reason the
    // numbers are: the whole control stays visible and one click deep.
    auto* add = AddChildView(std::make_unique<ZephyrusWorkspaceCell>(
        u"+", /*active=*/false, /*is_icon=*/false, ink,
        base::BindRepeating(on_add_)));
    add->SetTooltipText(u"New workspace");
    PreferredSizeChanged();
  }

  // views::View:
  // Compact: square cells, sized to whatever the host can spare and centred
  // in it. BoxLayout cannot do this -- it only shrinks along the main axis,
  // and a workspace cell narrower than it is tall is no longer the circle the
  // rest of this control assumes it is.
  void Layout(PassKey key) override {
    if (!compact_) {
      LayoutSuperclass<views::View>(this);
      return;
    }
    const int count = static_cast<int>(children().size());
    if (count == 0) {
      return;
    }
    // Tighter than the bar's 3dp: at this size the gap competes with the
    // glyphs for the same few pixels, and the glyphs matter more.
    constexpr int kCompactGap = 2;
    // Below this a drawn icon is a smudge whatever the inset does, so the
    // strip stops shrinking and takes the room it needs. The row is centred,
    // so that overflows symmetrically rather than clipping the last cell.
    constexpr int kMinCell = 18;
    const int room = width() - kCompactGap * (count - 1);
    const int cell =
        std::clamp(room / count, kMinCell, ZephyrusWorkspaceCell::size());
    const int total = cell * count + kCompactGap * (count - 1);
    int x = (width() - total) / 2;
    const int y = (height() - cell) / 2;
    for (views::View* child : children()) {
      child->SetBounds(x, y, cell, cell);
      x += cell + kCompactGap;
    }
  }

 private:
  void ShowCellMenu(int workspace_id, bool can_delete) {
    menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    menu_workspace_id_ = workspace_id;
    menu_model_->AddItem(kCommandPickIcon, u"Choose icon…");
    if (can_delete) {
      menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
      menu_model_->AddItem(kCommandDelete, u"Delete workspace");
    }
    menu_runner_ = std::make_unique<views::MenuRunner>(
        menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
    menu_runner_->RunMenuAt(GetWidget(), nullptr,
                            GetBoundsInScreen(),
                            views::MenuAnchorPosition::kTopLeft,
                            ui::mojom::MenuSourceType::kMouse);
  }

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override {
    if (command_id == kCommandPickIcon) {
      on_pick_icon_.Run(menu_workspace_id_);
    } else if (command_id == kCommandDelete) {
      on_delete_.Run(menu_workspace_id_);
    }
  }

  static constexpr int kCommandPickIcon = 1;
  static constexpr int kCommandDelete = 2;
  // A touch more air than before -- 22px discs sitting 2px apart run
  // together into one shape at a glance.
  static constexpr int kCellGap = 3;
  SwitchCallback on_switch_;
  base::RepeatingClosure on_add_;
  WorkspaceCallback on_pick_icon_;
  WorkspaceCallback on_delete_;
  int menu_workspace_id_ = 0;
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
};

BEGIN_METADATA(ZephyrusWorkspaceStrip)
END_METADATA


// Circular avatar (the signed-in Google photo when available, else the default
// silhouette) for `profile`, at `size` px.
ui::ImageModel ZephyrusProfileAvatar(Profile* profile, int size) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile || !profile_manager) {
    return ui::ImageModel();
  }
  ProfileAttributesEntry* entry =
      profile_manager->GetProfileAttributesStorage()
          .GetProfileAttributesWithPath(
              profile->GetOriginalProfile()->GetPath());
  if (!entry) {
    return ui::ImageModel();
  }
  return ui::ImageModel::FromImage(profiles::GetSizedAvatarIcon(
      entry->GetAvatarIcon(size), size, size, profiles::SHAPE_CIRCLE));
}

// Zephyrus: the profile-switcher pill in the title bar, sitting just left of the
// Workspace pill. Mirrors ZephyrusWorkspaceButton but leads with the profile's
// circular Google avatar. Profile = who you are; Workspace = what you're doing.
// --------------------------------------------------------------------------
// ZEPHYRUS PROFILES FRONTEND - DISABLED
//
// The user-facing profiles feature is withdrawn until Google auth lands.
// The backend (views/frame/zephyrus_profile_switcher.*) is retained in the
// tree and excluded from the build for the same reason.
//
// Disabled rather than deleted on purpose: the Zephyrus changes are
// uncommitted working-tree modifications, so `git checkout` would restore
// UPSTREAM Chromium here, not this code. Deleting it would be permanent.
// Re-enable by removing the #if 0 / #endif pair.
// --------------------------------------------------------------------------
#if 0
class ZephyrusProfileButton : public views::Button {
  METADATA_HEADER(ZephyrusProfileButton, views::Button)

 public:
  explicit ZephyrusProfileButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(3, 6), 5));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    avatar_ = AddChildView(std::make_unique<views::ImageView>());
    chevron_ = AddChildView(std::make_unique<views::ImageView>());
    SetTooltipText(u"Switch profile");
    GetViewAccessibility().SetName(u"Switch profile");
  }

  void SetContent(const ui::ImageModel& avatar, SkColor foreground) {
    if (!avatar.IsEmpty()) {
      avatar_->SetImage(avatar);
    }
    chevron_->SetImage(ui::ImageModel::FromVectorIcon(kZephyrusDropdownIcon,
                                                      foreground, 10));
  }

  // Chevron points up while the dropdown is open (mirrors the Workspace pill).
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

  void SetPillFills(SkColor normal, SkColor hovered, SkColor pressed) {
    normal_fill_ = normal;
    hovered_fill_ = hovered;
    pressed_fill_ = pressed;
    UpdatePill();
  }

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
    SetBackground(views::CreateRoundedRectBackground(fill, 10.0f));
  }

  raw_ptr<views::ImageView> avatar_ = nullptr;
  raw_ptr<views::ImageView> chevron_ = nullptr;
  SkColor normal_fill_ = SK_ColorTRANSPARENT;
  SkColor hovered_fill_ = SK_ColorTRANSPARENT;
  SkColor pressed_fill_ = SK_ColorTRANSPARENT;
  bool menu_open_ = false;
};

BEGIN_METADATA(ZephyrusProfileButton)
END_METADATA
#endif  // ZEPHYRUS PROFILES FRONTEND - DISABLED

// Zephyrus: the profile dropdown. Mirrors ZephyrusWorkspaceMenu's card — same
// bubble style, dynamic-theme colors, hover rows and width-jump entrance — but
// lists Chromium profiles (circular Google avatar + name, current marked) plus
// an "Add profile" row. Selecting a profile does the in-window seamless swap.
// --------------------------------------------------------------------------
// ZEPHYRUS PROFILES FRONTEND - DISABLED
//
// The user-facing profiles feature is withdrawn until Google auth lands.
// The backend (views/frame/zephyrus_profile_switcher.*) is retained in the
// tree and excluded from the build for the same reason.
//
// Disabled rather than deleted on purpose: the Zephyrus changes are
// uncommitted working-tree modifications, so `git checkout` would restore
// UPSTREAM Chromium here, not this code. Deleting it would be permanent.
// Re-enable by removing the #if 0 / #endif pair.
// --------------------------------------------------------------------------
#if 0
class ZephyrusProfileMenu : public views::BubbleDialogDelegateView,
                            public gfx::AnimationDelegate {
  METADATA_HEADER(ZephyrusProfileMenu, views::BubbleDialogDelegateView)

 public:
  ZephyrusProfileMenu(views::View* anchor,
                      Browser* browser,
                      std::optional<SkColor> page_color,
                      base::RepeatingClosure on_closed)
      : views::BubbleDialogDelegateView(anchor, views::BubbleBorder::TOP_LEFT),
        browser_(browser),
        on_closed_(std::move(on_closed)) {
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    set_margins(gfx::Insets(8));
    zephyrus::ConfigureBubble(this);
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));

    // Palette, not the page -- same reasoning as the workspace dropdown above.
    panel_ = zephyrus::Surface();
    foreground_ = zephyrus::Ink();
    row_hover_ = zephyrus::Raise(zephyrus::Surface(), 0x3A);
    SetBackgroundColor(panel_);

    expand_animation_.SetSlideDuration(base::Milliseconds(220));
    expand_animation_.SetTweenType(gfx::Tween::EASE_OUT_3);

    RebuildList();
  }

  ~ZephyrusProfileMenu() override {
    if (on_closed_) {
      on_closed_.Run();
    }
  }

  void OnWidgetInitialized() override {
    views::BubbleDialogDelegateView::OnWidgetInitialized();
    zephyrus::ApplyBubbleFrame(this);
  }

  // The Figma "full width jump", identical to the Workspace dropdown.
  void StartZephyrusEntrance(int anchor_width) {
    views::Widget* widget = GetWidget();
    if (!widget || !gfx::Animation::ShouldRenderRichAnimation()) {
      return;
    }
    final_bounds_ = widget->GetWindowBoundsInScreen();
    start_width_ = std::min(final_bounds_.width(), std::max(anchor_width, 60));
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
  static constexpr int kRowWidth = 230;

  void RebuildList() {
    RemoveAllChildViews();
    ProfileManager* profile_manager = g_browser_process->profile_manager();
    if (!profile_manager) {
      return;
    }
    const base::FilePath current =
        browser_ && browser_->profile()
            ? browser_->profile()->GetOriginalProfile()->GetPath()
            : base::FilePath();
    for (ProfileAttributesEntry* entry :
         profile_manager->GetProfileAttributesStorage()
             .GetAllProfilesAttributesSortedForDisplay()) {
      const base::FilePath path = entry->GetPath();
      const bool active = !current.empty() && path == current;
      auto* row = AddChildView(
          std::make_unique<ZephyrusHoverRevealRow>(row_hover_, active));
      auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8), 8));
      layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      row->SetPreferredSize(gfx::Size(kRowWidth, 36));

      auto* avatar = row->AddChildView(std::make_unique<views::ImageView>());
      avatar->SetImage(ui::ImageModel::FromImage(profiles::GetSizedAvatarIcon(
          entry->GetAvatarIcon(24), 24, 24, profiles::SHAPE_CIRCLE)));

      auto* name = row->AddChildView(std::make_unique<views::LabelButton>(
          base::BindRepeating(&ZephyrusProfileMenu::OnSwitch,
                              base::Unretained(this), path),
          entry->GetName()));
      name->SetTextColor(views::Button::STATE_NORMAL, foreground_);
      name->SetTextColor(views::Button::STATE_HOVERED, foreground_);
      name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      layout->SetFlexForView(name, 1);
      row->FinishInit();
    }

    auto* add_row = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&ZephyrusProfileMenu::OnAddProfile,
                            base::Unretained(this)),
        u"+   Add profile"));
    add_row->SetTextColor(views::Button::STATE_NORMAL,
                          SkColorSetA(foreground_, 0xC0));
    add_row->SetTextColor(views::Button::STATE_HOVERED, foreground_);
    add_row->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    add_row->SetMinSize(gfx::Size(kRowWidth, 34));

    if (GetWidget()) {
      SizeToContents();
    }
  }

  void OnSwitch(const base::FilePath& path) {
    // Copy out of ourselves before Close() tears the bubble (and this bound
    // path) down.
    const base::FilePath target = path;
    Browser* browser = browser_;
    if (views::Widget* widget = GetWidget()) {
      widget->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
    }
    ZephyrusProfileSwitcher::GetInstance()->SwitchTo(target, browser);
  }

  void OnAddProfile() {
    Browser* browser = browser_;
    if (views::Widget* widget = GetWidget()) {
      widget->CloseWithReason(views::Widget::ClosedReason::kUnspecified);
    }
    ZephyrusProfileSwitcher::GetInstance()->AddProfile(browser);
  }

  raw_ptr<Browser> browser_;
  base::RepeatingClosure on_closed_;
  SkColor panel_ = zephyrus::Surface();
  SkColor foreground_ = zephyrus::Ink();
  SkColor row_hover_ = SK_ColorTRANSPARENT;
  gfx::SlideAnimation expand_animation_{this};
  gfx::Rect final_bounds_;
  int start_width_ = 0;
};

BEGIN_METADATA(ZephyrusProfileMenu)
END_METADATA
#endif  // ZEPHYRUS PROFILES FRONTEND - DISABLED

void ToolbarView::SetZephyrusWorkspaceStripCompact(bool compact) {
  if (!zephyrus_workspace_strip_) {
    return;
  }
  static_cast<ZephyrusWorkspaceStrip*>(zephyrus_workspace_strip_.get())
      ->SetCompact(compact);
  // Re-runs SetWorkspaces(), which is what actually lays the cells out.
  UpdateZephyrusWorkspaceButton();
}

void ToolbarView::AddZephyrusWorkspaceButton() {
  auto button = std::make_unique<ZephyrusWorkspaceStrip>(base::BindRepeating(
      [](ToolbarView* toolbar, int workspace_id) {
        if (BrowserView* view =
                BrowserView::GetBrowserViewForBrowser(toolbar->browser_)) {
          if (ZephyrusWorkspaceManager* m = view->zephyrus_workspace_manager()) {
            m->SwitchToWorkspace(workspace_id);
          }
        }
      },
      base::Unretained(this)),
      base::BindRepeating(
          [](ToolbarView* toolbar) {
            if (BrowserView* view =
                    BrowserView::GetBrowserViewForBrowser(toolbar->browser_)) {
              if (ZephyrusWorkspaceManager* m =
                      view->zephyrus_workspace_manager()) {
                m->AddWorkspace();
              }
            }
          },
          base::Unretained(this)),
      base::BindRepeating(&ToolbarView::ShowZephyrusIconPicker,
                          base::Unretained(this)),
      base::BindRepeating(&ToolbarView::ConfirmZephyrusWorkspaceDelete,
                          base::Unretained(this)));
  button->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, 6));
  // Place it just to the right of the new-tab (+) button.
  std::optional<size_t> new_tab_index =
      zephyrus_new_tab_button_ ? GetIndexOf(zephyrus_new_tab_button_)
                               : std::nullopt;
  const size_t position = new_tab_index ? *new_tab_index + 1 : 0;
  zephyrus_workspace_strip_ =
      AddChildViewAt<views::View>(std::move(button), position);
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
  // Kick once on the next loop turn, by which time BrowserView has finished
  // constructing and the workspace manager exists. Without this the first
  // subscription waits for the 5-second poll, so switches in the opening
  // seconds of a window would still lag. A member OneShotTimer cancels on
  // destruction, so this is safe without a weak pointer.
  zephyrus_workspace_subscribe_kick_.Start(
      FROM_HERE, base::TimeDelta(), this,
      &ToolbarView::UpdateZephyrusWorkspaceButton);
  UpdateZephyrusWorkspaceButton();
}

void ToolbarView::ConfirmZephyrusWorkspaceDelete(int workspace_id) {
  BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser_);
  ZephyrusWorkspaceManager* manager =
      view ? view->zephyrus_workspace_manager() : nullptr;
  if (!manager) {
    return;
  }
  const ZephyrusWorkspaceManager::Workspace* ws =
      manager->GetWorkspace(workspace_id);
  if (!ws) {
    return;
  }

  // Deleting a workspace closes its tabs, so it asks first.
  //
  // The confirmation dialog already existed -- it was the old dropdown's, and
  // removing that dropdown left it with no callers. Wiring it here rather than
  // deleting it means the strip's one destructive action is not a single
  // unconfirmed right-click, and nothing has to be rebuilt to get there.
  int tab_count = 0;
  if (TabStripModel* model = browser_->tab_strip_model()) {
    for (int i = 0; i < model->count(); ++i) {
      if (manager->GetWorkspaceForContents(model->GetWebContentsAt(i)) ==
          workspace_id) {
        ++tab_count;
      }
    }
  }
  // An unnamed workspace is called what the strip's own tooltip calls it,
  // "Workspace N" by position. The fallback used to be the phrase "this
  // workspace", which the dialog then quoted as if it were a name:
  // Delete “this workspace”?
  std::u16string label = ws->name;
  if (label.empty()) {
    const auto& all = manager->workspaces();
    for (size_t i = 0; i < all.size(); ++i) {
      if (all[i].id == workspace_id) {
        label = u"Workspace " + base::NumberToString16(i + 1);
        break;
      }
    }
  }
  if (label.empty()) {
    label = u"this workspace";
  }
  ShowZephyrusDeleteWorkspaceDialog(
      browser_, label, tab_count, zephyrus::Surface(), zephyrus::Ink(),
      base::BindOnce(
          // Captures the BROWSER, not this toolbar. ToolbarView has no weak
          // factory, and the dialog is browser-modal -- so the Browser is
          // guaranteed to outlive it, while the toolbar is not obviously so.
          // Looking the manager up again on confirm also means a workspace
          // deleted by some other route in the meantime is simply not found.
          [](Browser* browser, int id) {
            if (BrowserView* v =
                    BrowserView::GetBrowserViewForBrowser(browser)) {
              if (ZephyrusWorkspaceManager* m =
                      v->zephyrus_workspace_manager()) {
                m->DeleteWorkspace(id);
              }
            }
          },
          browser_, workspace_id));
}

void ToolbarView::ShowZephyrusIconPicker(int workspace_id) {
  if (!zephyrus_workspace_strip_) {
    return;
  }
  auto picker = std::make_unique<ZephyrusIconPicker>(
      zephyrus_workspace_strip_,
      base::BindRepeating(
          [](ToolbarView* toolbar, int id, const std::u16string& glyph) {
            if (BrowserView* view =
                    BrowserView::GetBrowserViewForBrowser(toolbar->browser_)) {
              if (ZephyrusWorkspaceManager* m =
                      view->zephyrus_workspace_manager()) {
                m->SetWorkspaceEmoji(id, glyph);
              }
            }
          },
          base::Unretained(this), workspace_id),
      base::BindRepeating(&ToolbarView::ChooseZephyrusWorkspacePhoto,
                          weak_ptr_factory_.GetWeakPtr(), workspace_id));
  views::BubbleDialogDelegateView::CreateBubble(std::move(picker))->Show();
}

void ToolbarView::ChooseZephyrusWorkspacePhoto(int workspace_id) {
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view || !browser_->profile()) {
    return;
  }
  // Weak all the way through. The file dialog is the slowest thing in the
  // browser -- it waits on a human browsing their disk -- so by the time it
  // returns the window may well be gone, and this callback outlives more of the
  // UI than almost any other.
  // Workspace photos live under the window's profile, which is also where the
  // store reads them back from. There is one profile now -- workspaces are
  // partitions, not profiles -- so these cannot diverge.
  zephyrus::PickWorkspaceImage(
      browser_->profile(), workspace_id,
      browser_view->GetWidget() ? browser_view->GetWidget()->GetNativeWindow()
                                : gfx::NativeWindow(),
      base::BindOnce(
          [](base::WeakPtr<ToolbarView> toolbar, int id,
             const std::string& name) {
            // Empty means cancelled or undecodable. Cancelling must leave the
            // existing icon alone, so there is nothing to do -- clearing is a
            // separate, explicit choice in the picker.
            if (!toolbar || name.empty()) {
              return;
            }
            BrowserView* view =
                BrowserView::GetBrowserViewForBrowser(toolbar->browser_);
            ZephyrusWorkspaceManager* manager =
                view ? view->zephyrus_workspace_manager() : nullptr;
            if (manager) {
              manager->SetWorkspaceImage(id, name);
            }
          },
          weak_ptr_factory_.GetWeakPtr(), workspace_id));
}

void ToolbarView::UpdateZephyrusWorkspaceButton() {
  if (!zephyrus_workspace_strip_) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  ZephyrusWorkspaceManager* manager =
      browser_view ? browser_view->zephyrus_workspace_manager() : nullptr;
  if (!manager) {
    return;
  }

  // SUBSCRIBE HERE, not at construction.
  //
  // AddZephyrusWorkspaceButton() tries to register for workspace changes, and
  // ALWAYS FAILS: BrowserView creates the toolbar (browser_view.cc:968) before
  // it creates the workspace manager (:983), so the manager is null at that
  // point and the subscription is silently never made.
  //
  // The strip was therefore never told about a switch. It only refreshed when
  // the 5-second audio poll happened to fire -- which is exactly the "indicator
  // lags behind the switch" symptom, and why instrumenting the switch showed
  // notify=0.000ms: there was nothing subscribed to notify.
  //
  // This runs from that same poll, so the first tick after startup establishes
  // the subscription and every switch after that is immediate.
  if (!zephyrus_workspace_changed_subscription_) {
    zephyrus_workspace_changed_subscription_ =
        manager->RegisterChangedCallback(
            base::BindRepeating(&ToolbarView::UpdateZephyrusWorkspaceButton,
                                base::Unretained(this)));
  }
  // Subscribed, so the poll has nothing left to do. It was named for an audio
  // badge the strip no longer draws; what it still did was rebuild the strip,
  // and so relayout the window, every 5 seconds -- and cover for this late
  // subscription, which the one-shot kick at construction establishes anyway.
  zephyrus_audio_poll_timer_.Stop();
  // Private Workspace has its own store, so its numbering is its own; showing
  // that store's list here would misrepresent which profile you are in. The
  // strip is simply hidden there -- the lock in the title bar already says
  // where you are, and a private window has one workspace by definition.
  const bool is_private = ZephyrusPrivateWorkspace::IsPrivate(browser_);
  zephyrus_workspace_strip_->SetVisible(!is_private);
  if (is_private) {
    return;
  }
  static_cast<ZephyrusWorkspaceStrip*>(zephyrus_workspace_strip_.get())
      ->SetWorkspaces(
          manager->workspaces(), manager->current_workspace_id(),
          zephyrus::Ink(),
          base::BindRepeating(
              [](base::WeakPtr<ZephyrusWorkspaceManager> m,
                 int id) -> gfx::ImageSkia {
                return m ? m->GetWorkspaceImage(id, ZephyrusWorkspaceCell::size())
                         : gfx::ImageSkia();
              },
              manager->GetWeakPtr()));
}


// --------------------------------------------------------------------------
// ZEPHYRUS PROFILES FRONTEND - DISABLED
//
// The user-facing profiles feature is withdrawn until Google auth lands.
// The backend (views/frame/zephyrus_profile_switcher.*) is retained in the
// tree and excluded from the build for the same reason.
//
// Disabled rather than deleted on purpose: the Zephyrus changes are
// uncommitted working-tree modifications, so `git checkout` would restore
// UPSTREAM Chromium here, not this code. Deleting it would be permanent.
// Re-enable by removing the #if 0 / #endif pair.
// --------------------------------------------------------------------------
#if 0
void ToolbarView::AddZephyrusProfileButton() {
  auto button = std::make_unique<ZephyrusProfileButton>(base::BindRepeating(
      [](ToolbarView* toolbar) { toolbar->ShowZephyrusProfileMenu(); },
      base::Unretained(this)));
  button->SetProperty(views::kMarginsKey, gfx::Insets::VH(0, 6));
  // Sit just to the LEFT of the Workspace pill — profile is the higher-level
  // context (who), the workspace is what you're doing within it.
  std::optional<size_t> ws_index =
      zephyrus_workspace_strip_ ? GetIndexOf(zephyrus_workspace_strip_)
                                : std::nullopt;
  const size_t position = ws_index.value_or(0);
  zephyrus_profile_button_ =
      AddChildViewAt<views::Button>(std::move(button), position);
  UpdateZephyrusProfileButton();
}
#endif  // ZEPHYRUS PROFILES FRONTEND - DISABLED

// --------------------------------------------------------------------------
// ZEPHYRUS PROFILES FRONTEND - DISABLED
//
// The user-facing profiles feature is withdrawn until Google auth lands.
// The backend (views/frame/zephyrus_profile_switcher.*) is retained in the
// tree and excluded from the build for the same reason.
//
// Disabled rather than deleted on purpose: the Zephyrus changes are
// uncommitted working-tree modifications, so `git checkout` would restore
// UPSTREAM Chromium here, not this code. Deleting it would be permanent.
// Re-enable by removing the #if 0 / #endif pair.
// --------------------------------------------------------------------------
#if 0
void ToolbarView::UpdateZephyrusProfileButton() {
  if (!zephyrus_profile_button_) {
    return;
  }
  // The Private Workspace is an OTR window; profile switching is a normal-profile
  // concept, so hide the pill there rather than show a confusing identity.
  const bool is_private = ZephyrusPrivateWorkspace::IsPrivate(browser_);
  zephyrus_profile_button_->SetVisible(!is_private);
  if (is_private) {
    return;
  }
  // Same dynamic-theme chip model as the Workspace pill.
  const SkColor base =
      zephyrus_titlebar_color_.value_or(zephyrus::Ground());
  const SkColor surface = zephyrus::Surface();
  const SkColor surface_hover = zephyrus::Raise(zephyrus::Surface(), 0x3A);
  const SkColor surface_press = zephyrus::Raise(zephyrus::Surface(), 0x4C);
  const SkColor ink = zephyrus::InkFor(base);
  auto* pill =
      static_cast<ZephyrusProfileButton*>(zephyrus_profile_button_.get());
  pill->SetContent(ZephyrusProfileAvatar(browser_->profile(), 20), ink);
  pill->SetPillFills(surface, surface_hover, surface_press);
}
#endif  // ZEPHYRUS PROFILES FRONTEND - DISABLED

// --------------------------------------------------------------------------
// ZEPHYRUS PROFILES FRONTEND - DISABLED
//
// The user-facing profiles feature is withdrawn until Google auth lands.
// The backend (views/frame/zephyrus_profile_switcher.*) is retained in the
// tree and excluded from the build for the same reason.
//
// Disabled rather than deleted on purpose: the Zephyrus changes are
// uncommitted working-tree modifications, so `git checkout` would restore
// UPSTREAM Chromium here, not this code. Deleting it would be permanent.
// Re-enable by removing the #if 0 / #endif pair.
// --------------------------------------------------------------------------
#if 0
void ToolbarView::ShowZephyrusProfileMenu() {
  if (!zephyrus_profile_button_ ||
      zephyrus::ConsumeReopenSuppression(zephyrus_profile_button_)) {
    return;
  }
  auto menu = std::make_unique<ZephyrusProfileMenu>(
      zephyrus_profile_button_, browser_, zephyrus_titlebar_color_,
      base::BindRepeating(
          [](ToolbarView* toolbar) {
            if (toolbar->zephyrus_profile_button_) {
              static_cast<ZephyrusProfileButton*>(
                  toolbar->zephyrus_profile_button_.get())
                  ->SetMenuOpen(false);
            }
          },
          base::Unretained(this)));
  ZephyrusProfileMenu* menu_ptr = menu.get();
  views::Widget* menu_widget =
      views::BubbleDialogDelegateView::CreateBubble(std::move(menu));
  menu_widget->Show();
  static_cast<ZephyrusProfileButton*>(zephyrus_profile_button_.get())
      ->SetMenuOpen(true);
  if (ui::Layer* layer = menu_widget->GetLayer();
      layer && gfx::Animation::ShouldRenderRichAnimation()) {
    layer->SetOpacity(0.0f);
    ui::ScopedLayerAnimationSettings fade(layer->GetAnimator());
    fade.SetTransitionDuration(base::Milliseconds(120));
    fade.SetTweenType(gfx::Tween::EASE_OUT);
    layer->SetOpacity(1.0f);
  }
  menu_ptr->StartZephyrusEntrance(zephyrus_profile_button_->width());
}
#endif  // ZEPHYRUS PROFILES FRONTEND - DISABLED

void ToolbarView::LayoutCommon() {
  // While the controls are lent to the sidebar they are not children of this
  // view, and the layout manager CHECKs when asked about a view it does not
  // own (SetViewHidden on the app menu was the crash). Nothing here applies
  // to a bar holding only the window controls anyway.
  if (zephyrus_compact_) {
    UpdateZephyrusWindowControls();
    return;
  }

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

  // Reserve the corner INSET only -- not the strip.
  //
  // The window controls and their separator are ordinary children, so the flex
  // layout already allocates their cells; adding those here too reserved them
  // twice and opened a ~100px hole. What the layout cannot know about is
  // kZephyrusCaptionRightPad, the gap Layout() leaves between the close button
  // and the window edge. Without it the browser controls stay put while the
  // strip shifts, and the space between the two groups drifts by exactly the
  // pad every time it is tuned.
  //
  // Adding just the pad keeps the whole right-hand cluster moving as one.
  if (zephyrus_close_button_ && zephyrus_close_button_->GetVisible()) {
    interior_margin.set_right(interior_margin.right() +
                              kZephyrusCaptionRightPad -
                              kZephyrusBrowserControlsRightShift);
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

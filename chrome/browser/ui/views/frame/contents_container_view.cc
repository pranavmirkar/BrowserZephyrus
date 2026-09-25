// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/contents_container_view.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/i18n/rtl.h"
#include "chrome/browser/actor/ui/actor_overlay_web_view.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/enterprise/data_protection/data_protection_overlay_view.h"
#include "chrome/browser/glic/browser_ui/context_sharing_border_view.h"
#include "chrome/browser/glic/browser_ui/context_sharing_border_view_controller_impl.h"
#include "chrome/browser/glic/public/glic_enabling.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/ai_overlay_dialog/ai_overlay_dialog_controller.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/color/chrome_color_id.h"
#include "chrome/browser/ui/read_anything/read_anything_immersive_overlay_view.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_capture_border_view.h"
#include "chrome/browser/ui/views/frame/contents_container_outline.h"
#include "chrome/browser/ui/views/frame/contents_separator.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view_mini_toolbar.h"
#include "chrome/browser/ui/views/frame/scrim_view.h"
#include "chrome/browser/ui/views/frame/tab_modal_dialog_host.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/indigo/indigo_toolbar.h"
#include "chrome/browser/ui/views/new_tab_footer/footer_web_view.h"
#include "chrome/common/chrome_features.h"
#include "components/search/ntp_features.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/border.h"
#include "ui/views/layout/delegating_layout_manager.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/metadata/view_factory.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_WIN)
#include "ui/views/widget/native_widget_aura.h"
#endif

namespace {
// Zephyrus: the page's curve, a few px above the sidebar panel's 18
// (kPanelCornerRadius in zephyrus_sidebar_view.cc) so they read as the same
// family. 20 rather than 21 on purpose — it stays a whole number of device
// pixels at 125% and 150% display scaling (25 and 30), so the corner mask
// doesn't land on fractional pixels. Was 6 upstream.
constexpr int kSplitViewContentPadding = 4;
constexpr int kNewTabFooterSeparatorHeight = 1;
constexpr int kNewTabFooterHeight = 56;

// Zephyrus: the page as a floating card (the Helium/Zen treatment).
//
// The margin has to exist for the outline to read as a border on the PAGE
// rather than as an outline on the WINDOW: with the contents flush to the
// window edge there is nowhere for a stroke to sit that does not look like
// window chrome.
//
// The page sits in a thin frame of window background: 4 on the three sides
// that face the window frame, none on top where the toolbar is attached. The
// radius is ContentsContainerOutline::kCornerRadius (8), which is also what
// Windows 11 rounds its window corners to, so the page's curve matches the
// frame just outside it. There is no stroke — the frame IS the margin.
// kZephyrusContentMargin lives on ContentsContainerView now, so the layout can
// give the side panel the same margin. Inside these member functions the class
// member is what the bare name resolves to -- class scope is searched before
// the enclosing namespace -- so no local alias is needed, and one here would be
// dead.
constexpr float kZephyrusContentRadius = ContentsContainerOutline::kCornerRadius;
}  // namespace

ContentsContainerView::ContentsContainerView(BrowserView* browser_view)
    : browser_view_(browser_view),
      web_contents_modal_dialog_host_(browser_view_, this) {
  SetLayoutManager(std::make_unique<views::DelegatingLayoutManager>(this));
  SetProperty(views::kElementIdentifierKey, kContentsContainerViewElementId);

  // The default z-order is the order in which children were added to the
  // parent view. So first added devtools and the devtools scrim view (as it
  // exists behind the content view), then the content view and new tab page
  // footer. This should be followed by scrims, borders and lastly mini-toolbar.

  auto devtools_web_view =
      std::make_unique<views::WebView>(browser_view->GetProfile());
  devtools_web_view->SetID(VIEW_ID_DEV_TOOLS_DOCKED);
  devtools_web_view->SetVisible(false);
  devtools_web_view_ = AddChildView(std::move(devtools_web_view));

  devtools_scrim_view_ = AddChildView(std::make_unique<ScrimView>());
  devtools_scrim_view_->layer()->SetName("DevtoolsScrimView");

  toast_anchor_view_ = AddChildView(std::make_unique<views::View>());

  contents_view_ = AddChildView(
      std::make_unique<ContentsWebView>(browser_view->GetProfile()));
  contents_view_->SetID(VIEW_ID_TAB_CONTAINER);
  contents_view_->set_use_default_deadline_when_animating_bounds(
      base::FeatureList::IsEnabled(
          features::kUseDefaultDeadlineWhenAnimatingBounds));

  if (base::FeatureList::IsEnabled(ntp_features::kNtpFooter)) {
    new_tab_footer_view_separator_ =
        AddChildView(ContentsSeparator::CreateContentsSeparator());
    new_tab_footer_view_separator_->SetVisible(false);
    new_tab_footer_view_separator_->SetProperty(
        views::kElementIdentifierKey, kFooterWebViewSeparatorElementId);

    new_tab_footer_view_ =
        AddChildView(std::make_unique<new_tab_footer::NewTabFooterWebView>(
            browser_view->browser()));
    new_tab_footer_view_->SetVisible(false);
  }

  data_protection_overlay_view_ =
      AddChildView(std::make_unique<
                   enterprise_data_protection::DataProtectionOverlayView>());

  if (base::FeatureList::IsEnabled(features::kIndigo)) {
    indigo_overlay_view_ = AddChildView(indigo::CreateIndigoOverlayView());
    indigo_overlay_view_->InsertBeforeInFocusList(contents_view_);
  }

  if (base::FeatureList::IsEnabled(features::kAiOverlayDialog)) {
    auto ai_overlay_dialog_view =
        std::make_unique<views::WebView>(browser_view->GetProfile());
    ai_overlay_dialog_view->SetVisible(false);
    ai_overlay_dialog_view->SetProperty(views::kElementIdentifierKey,
                                        kAiOverlayDialogWebViewElementId);
    ai_overlay_dialog_view->EnableSizingFromWebContents(gfx::Size(1, 1),
                                                        gfx::Size(800, 600));
    ai_overlay_dialog_view_ = AddChildView(std::move(ai_overlay_dialog_view));
  }

  if (features::IsImmersiveReadAnythingEnabled()) {
    auto read_anything_immersive_overlay_view =
        std::make_unique<ReadAnythingImmersiveOverlayView>(contents_view_);
    read_anything_immersive_overlay_view_ =
        AddChildView(std::move(read_anything_immersive_overlay_view));
  }

  contents_scrim_view_ = AddChildView(std::make_unique<ScrimView>());
  contents_scrim_view_->layer()->SetName("ContentsScrimView");

  if (base::FeatureList::IsEnabled(features::kGlicActorUi) &&
      features::kGlicActorUiOverlay.Get()) {
    auto actor_overlay_web_view =
        std::make_unique<ActorOverlayWebView>(browser_view->browser());
    actor_overlay_web_view->SetID(VIEW_ID_ACTOR_OVERLAY);
    actor_overlay_web_view_ = AddChildView(std::move(actor_overlay_web_view));
  }

  glic_selection_overlay_view_ = AddChildView(std::make_unique<views::View>());
  glic_selection_overlay_view_->SetProperty(views::kElementIdentifierKey,
                                            kGlicSelectionOverlayViewElementId);
  glic_selection_overlay_view_->SetVisible(false);
  glic_selection_overlay_view_->SetLayoutManager(
      std::make_unique<views::FillLayout>());
  glic_selection_overlay_view_->SetPaintToLayer();

  if (glic::GlicEnabling::IsProfileEligible(browser_view->GetProfile())) {
    glic_border_ = AddChildView(
        views::Builder<glic::ContextSharingBorderView>(
            glic::ContextSharingBorderView::Factory::Create(
                std::make_unique<
                    glic::ContextSharingBorderViewControllerImpl>(),
                browser_view->browser(), contents_view_))
            .SetVisible(false)
            .SetCanProcessEventsWithinSubtree(false)
            .Build());
  }

  mini_toolbar_ = AddChildView(std::make_unique<MultiContentsViewMiniToolbar>(
      browser_view, contents_view_));

  container_outline_ =
      AddChildView(std::make_unique<ContentsContainerOutline>(mini_toolbar_));

  capture_contents_border_view_ =
      AddChildView(std::make_unique<ContentsCaptureBorderView>(mini_toolbar_));

  view_bounds_observer_.Observe(contents_view_);
}

ContentsContainerView::~ContentsContainerView() {
  indigo_overlay_view_ = nullptr;
  // read_anything_immersive_overlay_view_ holds a raw_ptr to
  // contents_view_. We need to make sure we destroy
  // read_anything_immersive_overlay_view_ first to avoid a dangling pointer.
  if (read_anything_immersive_overlay_view_) {
    auto overlay_view = RemoveChildViewT(read_anything_immersive_overlay_view_);
    read_anything_immersive_overlay_view_ = nullptr;
  }
}

std::vector<views::View*> ContentsContainerView::GetAccessiblePanes() {
  std::vector<views::View*> accessible_panes;
  if (contents_view_->GetVisible()) {
    accessible_panes.push_back(contents_view_);
  }
  if (devtools_web_view_->GetVisible()) {
    accessible_panes.push_back(devtools_web_view_);
  }
  if (devtools_scrim_view_->GetVisible()) {
    accessible_panes.push_back(devtools_scrim_view_);
  }
  return accessible_panes;
}

void ContentsContainerView::UpdateBorderAndOverlay(bool is_in_split,
                                                   bool is_active,
                                                   bool is_highlighted) {
  const bool split_changed = is_in_split != is_in_split_;
  is_in_split_ = is_in_split;

  if (!is_in_split) {
    if (split_changed) {
      SetBorder(nullptr);
      UpdateBorderRoundedCorners();

      mini_toolbar_->SetVisible(false);
      container_outline_->SetVisible(false);
      if (capture_contents_border_view_) {
        capture_contents_border_view_->SetIsInSplit(false);
      }
    }
  } else {
    if (split_changed) {
      SetBorder(views::CreateEmptyBorder(gfx::Insets(
          kSplitViewContentPadding + ContentsContainerOutline::kThickness)));
      UpdateBorderRoundedCorners();
    }

    container_outline_->UpdateState(is_active, is_highlighted);
    // Mini toolbar should only be visible for the inactive contents
    // container view or both depending on configuration.
    mini_toolbar_->UpdateState(is_active, is_highlighted);
    if (capture_contents_border_view_) {
      capture_contents_border_view_->SetIsInSplit(true);
    }
  }

#if BUILDFLAG(IS_CHROMEOS)
  if (split_changed) {
    // Ensures correct window rounded corners after updating contents rounded
    // corners in UpdateBorderRoundedCorners().
    GetWidget()->non_client_view()->frame_view()->UpdateWindowRoundedCorners();
  }
#endif  //  BUILDFLAG(IS_CHROMEOS)
}

void ContentsContainerView::UpdateBorderRoundedCorners() {
  // Zephyrus: ONE radius, split or not.
  //
  // Split view used upstream's 20 while the single-pane card uses 8, so
  // entering split silently changed the shape of the page -- two panes that
  // looked like a different kind of object from the one they replaced. A split
  // is the same card, twice; it should not be a restyle.
  //
  // Everything below is shared -- notably the devtools and footer logic, which
  // is why this function is reused rather than the corners being set directly:
  // a page rounded without regard to a docked devtools pane shows
  // theme-coloured notches between the two, which is what a hand-rolled
  // version got wrong.
  const float corner_radius = kZephyrusContentRadius;
  const gfx::RoundedCornersF all_corners_rounded{corner_radius};

  // Update devtools rounded corners. Note, devtools exists behind the contents
  // view so all devtools corners are rounded.
  devtools_web_view_->holder()->SetCornerRadii(all_corners_rounded);
  devtools_scrim_view_->SetRoundedCorners(all_corners_rounded);

  const bool devtools_in_upper_left =
      devtools_web_view_->GetVisible() &&
      current_devtools_docked_placement_ == DevToolsDockedPlacement::kLeft;
  const bool devtools_in_upper_right =
      devtools_web_view_->GetVisible() &&
      current_devtools_docked_placement_ == DevToolsDockedPlacement::kRight;
  const bool devtools_in_lower_left =
      devtools_web_view_->GetVisible() &&
      (current_devtools_docked_placement_ == DevToolsDockedPlacement::kBottom ||
       current_devtools_docked_placement_ == DevToolsDockedPlacement::kLeft);
  const bool devtools_in_lower_right =
      devtools_web_view_->GetVisible() &&
      (current_devtools_docked_placement_ == DevToolsDockedPlacement::kBottom ||
       current_devtools_docked_placement_ == DevToolsDockedPlacement::kRight);

  const gfx::RoundedCornersF content_upper_rounded_corners =
      gfx::RoundedCornersF{devtools_in_upper_left ? 0 : corner_radius,
                           devtools_in_upper_right ? 0 : corner_radius,
                           0, 0};
  const gfx::RoundedCornersF content_lower_rounded_corners =
      gfx::RoundedCornersF{0, 0,
                           devtools_in_lower_right ? 0 : corner_radius,
                           devtools_in_lower_left ? 0 : corner_radius};
  // Zephyrus: the top corners are always curved. Attached, they curve into the
  // toolbar; with the title bar hidden they curve into the margin that
  // GetZephyrusContentMargin opens up on top, so the page reads as the same
  // card either way rather than changing shape when the toolbar goes away.
  const gfx::RoundedCornersF content_rounded_corners =
      gfx::RoundedCornersF{devtools_in_upper_left ? 0 : corner_radius,
                           devtools_in_upper_right ? 0 : corner_radius,
                           devtools_in_lower_right ? 0 : corner_radius,
                           devtools_in_lower_left ? 0 : corner_radius};

  auto radii = new_tab_footer_view_ && new_tab_footer_view_->GetVisible()
                   ? content_upper_rounded_corners
                   : content_rounded_corners;

  contents_view_->SetBackgroundRadii(radii);
  contents_view_->holder()->SetCornerRadii(radii);
  // Zephyrus: and the ContentsWebView's OWN layer. The two calls above round
  // the background the view paints and the native host that carries the
  // renderer's surface, but the surface is a descendant layer and was not
  // being clipped by either — so the page's square corner painted straight
  // over the curve. That is the artifact that took on the colour of whatever
  // site was loaded: white on Notion, red on a red page. Rounding the parent
  // layer clips every descendant, which is the only one of the three that
  // catches the compositor surface.
  if (ui::Layer* layer = contents_view_->layer()) {
    layer->SetRoundedCornerRadius(radii);
    layer->SetIsFastRoundedCorner(true);
  }
  contents_scrim_view_->SetRoundedCorners(all_corners_rounded);

  if (new_tab_footer_view_) {
    new_tab_footer_view_->holder()->SetCornerRadii(
        content_lower_rounded_corners);
  }

  if (actor_overlay_web_view_) {
    // ActorOverlayWebView should use the same radii as the contents view since
    // it acts as a full transparent layer directly over the main web content.
    actor_overlay_web_view_->holder()->SetCornerRadii(radii);
  }

  if (ai_overlay_dialog_view_) {
    // ai_overlay_dialog_view_ should use the same radii as the contents view
    // since it acts as a layer directly over the main web content.
    ai_overlay_dialog_view_->holder()->SetCornerRadii(radii);
  }

  if (glic_selection_overlay_view_) {
    glic_selection_overlay_view_->layer()->SetRoundedCornerRadius(radii);
  }

  if (glic_border_) {
    glic_border_->SetRoundedCorners(content_rounded_corners);
  }
}

void ContentsContainerView::ClearBorderRoundedCorners() {
  constexpr gfx::RoundedCornersF kNoRoundedCorners = gfx::RoundedCornersF{0};

  devtools_web_view_->holder()->SetCornerRadii(kNoRoundedCorners);
  devtools_scrim_view_->SetRoundedCorners(kNoRoundedCorners);

  contents_view_->SetBackgroundRadii(kNoRoundedCorners);
  if (ui::Layer* layer = contents_view_->layer()) {
    layer->SetRoundedCornerRadius(kNoRoundedCorners);
  }
  contents_view_->holder()->SetCornerRadii(kNoRoundedCorners);

  if (new_tab_footer_view_) {
    new_tab_footer_view_->holder()->SetCornerRadii(kNoRoundedCorners);
  }

  contents_scrim_view_->SetRoundedCorners(kNoRoundedCorners);

  if (actor_overlay_web_view_) {
    actor_overlay_web_view_->holder()->SetCornerRadii(kNoRoundedCorners);
  }

  if (ai_overlay_dialog_view_) {
    ai_overlay_dialog_view_->holder()->SetCornerRadii(kNoRoundedCorners);
  }

  if (glic_selection_overlay_view_) {
    glic_selection_overlay_view_->layer()->SetRoundedCornerRadius(
        kNoRoundedCorners);
  }

  if (glic_border_) {
    glic_border_->SetRoundedCorners(kNoRoundedCorners);
  }
}

void ContentsContainerView::ChildVisibilityChanged(View* child) {
  // Zephyrus: no longer gated on split view. Outside split the page is a
  // rounded card too, so showing or hiding devtools or the footer has to
  // re-derive which of its corners may curve.
  if (child == new_tab_footer_view_ || child == devtools_web_view_) {
    UpdateZephyrusContentCorners();
  }
}

void ContentsContainerView::UpdateZephyrusContentCorners() {
  // Not yet fully constructed. ChildVisibilityChanged fires from inside this
  // view's own constructor — new_tab_footer_view_->SetVisible(false) runs while
  // the footer is already parented but the scrim below it does not exist yet —
  // and both branches below dereference contents_scrim_view_ unconditionally.
  // Upstream never hit this because the caller was gated on is_in_split_, which
  // is false during construction; dropping that gate so the single-pane card
  // updates too made the null reachable, and the browser crashed before the
  // window appeared. Layout() applies the corners once construction finishes.
  if (!contents_view_ || !contents_scrim_view_) {
    return;
  }

  // Rounding the bottom corners against a window edge would cut notches out of
  // the page and show the desktop through them, so a window with no margin —
  // fullscreen, popups — stays square.
  if (!is_in_split_ && GetZephyrusContentMargin().IsEmpty()) {
    ClearBorderRoundedCorners();
    container_outline_->SetVisible(false);
    return;
  }
  UpdateBorderRoundedCorners();
  // No stroke, split or not. The frame is the margin of window background
  // around the page; a light line on top of that reads as a second, brighter
  // edge rather than as definition -- and that was as true of the two panes in
  // a split as of the single card, where it had already been removed. Each
  // pane carries its own margin, so the gutter between them is the same
  // window background doing the same separating job.
  container_outline_->SetVisible(false);
}

void ContentsContainerView::SetZephyrusSuppressedEdge(int edge) {
  if (zephyrus_suppressed_edge_ == edge) {
    return;
  }
  zephyrus_suppressed_edge_ = edge;
  InvalidateLayout();
}

gfx::Insets ContentsContainerView::GetZephyrusContentMargin() const {
  // Fullscreen is a request for the page and nothing else; a margin there would
  // letterbox video. Non-tabbed windows (popups, PWAs, PiP) are sized to their
  // content by the site, so a floating card inside them just wastes the space
  // the site asked for.
  // Split view brings its own insets and its own outline; the card is the
  // single-pane treatment only.
  if (is_in_split_ || !browser_view_ || browser_view_->IsFullscreen() ||
      !browser_view_->GetIsNormalType()) {
    return gfx::Insets();
  }
  // The top is the only edge that changes. With the toolbar attached above,
  // a gap there would not read as a margin around a card — it would read as the
  // page having come unstuck from the browser — so the page stays married to
  // the toolbar and only the three edges facing the window frame get the
  // margin. With the title bar hidden there is no toolbar to be attached to,
  // and the page becomes a card on all four sides.
  // The leading edge behaves like the top: a margin only where the page faces
  // the window frame. With the sidebar attached the page faces the sidebar
  // instead, and it meets it flush — the rounded corner alone separates them,
  // with the sidebar showing through the notch the same way the toolbar does
  // above.
  // The margin on all three framed sides regardless of the sidebar. The card
  // keeps the same frame whether the sidebar is out or not, so it reads as one
  // consistent object rather than changing shape when the sidebar appears — and
  // the 4px gap is what separates it from the sidebar instead of the two sitting
  // flush. Only the TOP still drops, because the toolbar is genuinely attached
  // there.
  const int leading =
      zephyrus_suppressed_edge_ == 1 ? 0 : kZephyrusContentMargin;
  const int trailing =
      zephyrus_suppressed_edge_ == 2 ? 0 : kZephyrusContentMargin;
  return gfx::Insets::TLBR(ZephyrusTopIsAttached() ? 0 : kZephyrusContentMargin,
                           leading, kZephyrusContentMargin, trailing);
}

bool ContentsContainerView::ZephyrusTopIsAttached() const {
  // Whether browser chrome is sitting directly on top of the page. When the
  // title bar is hidden there is nothing up there for the page to curve into,
  // so curving anyway leaves two notches of window background floating against
  // the top of the content with no explanation.
  //
  // The horizontal-tabs strip also counts, title bar or not: it always sits
  // directly above the page and already leaves 4dp under its tabs. Adding the
  // page's own margin on top of that doubled the gap below the tabs (8dp)
  // against the 4dp above them.
  return browser_view_ && (browser_view_->IsZephyrusTitlebarShowing() ||
                           browser_view_->ZephyrusTabStripHeight() > 0);
}

void ContentsContainerView::Layout(PassKey pass_key) {
  LayoutSuperclass<views::View>(this);

  UpdateContentsClip();

  UpdateZephyrusContentCorners();
}

views::View::Views ContentsContainerView::GetChildrenInZOrder() {
#if DCHECK_IS_ON()
  auto ordered_children = views::View::GetChildrenInZOrder();
  // |capture_contents_border_view_| should have the highest z-order.
  DCHECK(ordered_children.back() == capture_contents_border_view_);
  return ordered_children;
#else
  return views::View::GetChildrenInZOrder();
#endif
}

void ContentsContainerView::OnViewBoundsChanged(View* observed_view) {
  if (observed_view == contents_view_) {
    UpdateDevToolsDockedPlacement();
    // Zephyrus: also outside split view — the docked placement that was just
    // recomputed decides which corners may curve.
    UpdateZephyrusContentCorners();
  }
}

void ContentsContainerView::SetContentsResizingStrategy(
    const DevToolsContentsResizingStrategy& strategy) {
  if (strategy_.Equals(strategy)) {
    return;
  }

  strategy_.CopyFrom(strategy);
  InvalidateLayout();
}

void ContentsContainerView::ApplyWatermarkSettings(
    const std::string& watermark_text,
    SkColor fill_color,
    SkColor outline_color,
    int font_size) {
  data_protection_overlay_view_->SetWatermarkText(watermark_text, fill_color,
                                                  outline_color, font_size);
}

void ContentsContainerView::UpdateDevToolsDockedPlacement() {
  DevToolsDockedPlacement placement = DevToolsDockedPlacement::kUnknown;
  gfx::Rect contents_view_bounds = GetContentsViewBounds();
  // Zephyrus: the same margin CalculateProposedLayout applies, or the "devtools
  // are not open" comparison below never matches and every tab looks like it
  // has a docked devtools pane.
  gfx::Rect container_bounds = GetContentsBounds();
  container_bounds.Inset(GetZephyrusContentMargin());

  // If contents_webview has the same bounds as webview_container, it either
  // means that devtools are not open or devtools are open in a separate
  // window (not docked).
  if (contents_view_bounds == container_bounds) {
    placement = DevToolsDockedPlacement::kNone;
  } else if (contents_view_bounds.x() > container_bounds.x() &&
             contents_view_bounds.y() == container_bounds.y() &&
             contents_view_bounds.height() == container_bounds.height()) {
    placement = DevToolsDockedPlacement::kLeft;
  } else if (contents_view_bounds.origin() == container_bounds.origin() &&
             contents_view_bounds.height() == container_bounds.height()) {
    placement = DevToolsDockedPlacement::kRight;
  } else if (contents_view_bounds.origin() == container_bounds.origin() &&
             contents_view_bounds.width() == container_bounds.width()) {
    placement = DevToolsDockedPlacement::kBottom;
  }

  // When browser window is resizing, the contents_container and web_contents
  // bounds can be out of sync, resulting in a state, where it is impossible to
  // infer docked placement based on contents webview bounds. In this case, use
  // the last known docked placement, since resizing a window does not change
  // the devtools dock placement.
  if (placement != DevToolsDockedPlacement::kUnknown) {
    current_devtools_docked_placement_ = placement;
  }
}

void ContentsContainerView::ShowCaptureContentsBorder() {
  if (capture_contents_border_view_) {
    capture_contents_border_view_->SetVisible(true);
  }
}

void ContentsContainerView::HideCaptureContentsBorder() {
  if (capture_contents_border_view_) {
    capture_contents_border_view_->SetVisible(false);
  }
}

void ContentsContainerView::SetCaptureContentsBorderLocation(
    std::optional<gfx::Rect> border_location) {
  if (capture_contents_border_view_) {
    capture_contents_border_view_->SetCaptureContentsBorderLocation(
        border_location);
  }
}

gfx::Rect ContentsContainerView::GetContentsViewBounds() const {
  gfx::Rect contents_view_bounds = contents_view_->bounds();
  if (new_tab_footer_view_ && new_tab_footer_view_->GetVisible()) {
    CHECK(new_tab_footer_view_separator_);
    contents_view_bounds.set_height(contents_view_bounds.height() +
                                    new_tab_footer_view_->height() +
                                    new_tab_footer_view_separator_->height());
  }

  return contents_view_bounds;
}

void ContentsContainerView::SetTargetContentBounds(
    std::optional<gfx::Outsets> target_content_bounds) {
  if (target_content_bounds_ == target_content_bounds) {
    return;
  }

  target_content_bounds_ = target_content_bounds;
  InvalidateLayout(/*avoid_propagate_during_layout=*/true);
}

void ContentsContainerView::SetRoundedCorners(
    const gfx::RoundedCornersF& corner_radii) {
  if (corner_radii == rounded_corner_radii_) {
    return;
  }

  rounded_corner_radii_ = corner_radii;
  UpdateBorderRoundedCorners();
}

void ContentsContainerView::UpdateContentsClip() {
  bool changed = false;
  if (auto* const layer = contents_view_->holder()->GetUILayer()) {
    if (layer->clip_rect() != contents_clip_rect_) {
      layer->SetClipRect(contents_clip_rect_);
      changed = true;
    }
  }
  if (auto* const layer = contents_view_->layer()) {
    if (layer->clip_rect() != contents_clip_rect_) {
      layer->SetClipRect(contents_clip_rect_);
      changed = true;
    }
  }
  if (changed) {
    contents_view_->SchedulePaint();
  }
}

views::ProposedLayout ContentsContainerView::CalculateProposedLayout(
    const views::SizeBounds& size_bounds) const {
  views::ProposedLayout layouts;
  if (!size_bounds.is_fully_bounded()) {
    return layouts;
  }

  int height = size_bounds.height().value();
  int width = size_bounds.width().value();

  if (width == 0 || height == 0) {
    // On Wayland we receive a resize to 0 width first before the actual
    // size bounds. Ignore such requests.
    return layouts;
  }

  // Zephyrus: inset every child by the floating-card margin. Done here rather
  // than with a views::Border because SetBorder() invalidates layout, and the
  // margin depends on fullscreen state that changes during a layout pass — the
  // invalidation would re-enter. GetContentsBounds() is the single source every
  // child position below is derived from, so insetting it once is enough.
  gfx::Rect full_contents_bounds = GetContentsBounds();
  full_contents_bounds.Inset(GetZephyrusContentMargin());
  gfx::Rect devtools_bounds;
  // The area contents excluding devtools is drawn (ie |contents_view_|,
  // |new_tab_footer_view_|, etc).
  gfx::Rect non_devtools_contents_bounds;

  ApplyDevToolsContentsResizingStrategy(strategy_, full_contents_bounds,
                                        &devtools_bounds,
                                        &non_devtools_contents_bounds);
  gfx::Rect contents_view_bounds = non_devtools_contents_bounds;

  // DevTools cares about the specific position, so we have to compensate RTL
  // layout here.
  layouts.child_layouts.emplace_back(
      devtools_web_view_.get(), devtools_web_view_->GetVisible(),
      GetMirroredRect(devtools_bounds),
      views::SizeBounds(full_contents_bounds.size()));
  layouts.child_layouts.emplace_back(
      devtools_scrim_view_.get(), devtools_scrim_view_->GetVisible(),
      GetMirroredRect(devtools_bounds),
      views::SizeBounds(full_contents_bounds.size()));

  if (new_tab_footer_view_) {
    gfx::Rect footer_separator_rect, footer_rect;
    if (new_tab_footer_view_->GetVisible()) {
      // Shrink the rect for the contents view if the ntp footer is visible.
      contents_view_bounds.set_height(non_devtools_contents_bounds.height() -
                                      kNewTabFooterHeight -
                                      kNewTabFooterSeparatorHeight);
      footer_separator_rect =
          gfx::Rect(contents_view_bounds.x(), contents_view_bounds.bottom(),
                    contents_view_bounds.width(), kNewTabFooterSeparatorHeight);
      footer_rect =
          gfx::Rect(contents_view_bounds.x(), contents_view_bounds.bottom(),
                    contents_view_bounds.width(), kNewTabFooterHeight);
    }

    layouts.child_layouts.emplace_back(new_tab_footer_view_separator_.get(),
                                       new_tab_footer_view_->GetVisible(),
                                       footer_separator_rect);
    layouts.child_layouts.emplace_back(new_tab_footer_view_.get(),
                                       new_tab_footer_view_->GetVisible(),
                                       footer_rect);
  }

  const auto& contents_rect = GetMirroredRect(contents_view_bounds);
  layouts.child_layouts.emplace_back(
      contents_view_.get(), contents_view_->GetVisible(), contents_rect);

  layouts.child_layouts.emplace_back(
      toast_anchor_view_.get(), toast_anchor_view_->GetVisible(),
      gfx::BoundingRect(contents_rect.origin(), contents_rect.top_right()));

  if (glic_border_) {
    // |glic_border_| should not be seen over devtools.
    layouts.child_layouts.emplace_back(glic_border_.get(),
                                       glic_border_->GetVisible(),
                                       non_devtools_contents_bounds);
  }

  // The content scrim view should cover the entire contents bounds.
  CHECK(contents_scrim_view_);
  layouts.child_layouts.emplace_back(contents_scrim_view_.get(),
                                     contents_scrim_view_->GetVisible(),
                                     full_contents_bounds);

  CHECK(data_protection_overlay_view_);
  layouts.child_layouts.emplace_back(
      data_protection_overlay_view_.get(),
      data_protection_overlay_view_->GetVisible(), full_contents_bounds);

  // Actor Overlay view bounds are the same as the contents view.
  if (actor_overlay_web_view_) {
    layouts.child_layouts.emplace_back(
        actor_overlay_web_view_.get(), actor_overlay_web_view_->GetVisible(),
        non_devtools_contents_bounds, size_bounds);
  }

  if (ai_overlay_dialog_view_) {
    // TODO(b/490458384): Look into whether the view can be transparent to hit
    // testing (in transparent parts) - otherwise autosize it to the inner web
    // content.
    gfx::Size size = ai_overlay_dialog_view_->GetPreferredSize();
    if (size.IsEmpty()) {
      int dialog_width = 200;
      int dialog_height = 200;
      if (!features::kAiOverlayDialogMockJsonPath.Get().empty()) {
        // 150px (buttons) + 20px (gap) + 100px (persona) = 270px
        dialog_width = 270;
        // 200px (max height of column)
        dialog_height = 200;
      }
      size = gfx::Size(dialog_width, dialog_height);
    }
    int x_margin = 15;
    gfx::Point top_left = non_devtools_contents_bounds.bottom_right() -
                          gfx::Vector2d(size.width() + x_margin, size.height());
    gfx::Rect rect(top_left, size);
    layouts.child_layouts.emplace_back(ai_overlay_dialog_view_.get(),
                                       ai_overlay_dialog_view_->GetVisible(),
                                       rect, views::SizeBounds(rect.size()));
  }

  if (glic_selection_overlay_view_) {
    layouts.child_layouts.emplace_back(
        glic_selection_overlay_view_.get(),
        glic_selection_overlay_view_->GetVisible(),
        non_devtools_contents_bounds, size_bounds);
  }

  // Reading Mode overlay view bounds are the same as the contents view.
  if (features::IsImmersiveReadAnythingEnabled() &&
      read_anything_immersive_overlay_view_) {
    layouts.child_layouts.emplace_back(
        read_anything_immersive_overlay_view_.get(),
        read_anything_immersive_overlay_view_->GetVisible(),
        non_devtools_contents_bounds, size_bounds);
  }

  if (indigo_overlay_view_) {
    layouts.child_layouts.emplace_back(indigo_overlay_view_.get(),
                                       indigo_overlay_view_->GetVisible(),
                                       non_devtools_contents_bounds);
  }

  if (mini_toolbar_) {
    // |mini_toolbar_| should be offset in the bottom right corner, overlapping
    // the outline. Shrink the available space by corner radius to ensure we
    // have space to draw it at the corners.
    views::SizeBounds available_space(width, height);
    available_space.Enlarge(-ContentsContainerOutline::kCornerRadius,
                            -ContentsContainerOutline::kCornerRadius);
    gfx::Size mini_toolbar_size =
        mini_toolbar_->GetPreferredSize(available_space);
    const int offset_x = width - mini_toolbar_size.width();
    const int offset_y = height - mini_toolbar_size.height();
    const gfx::Rect mini_toolbar_rect =
        gfx::Rect(offset_x, offset_y, mini_toolbar_size.width(),
                  mini_toolbar_size.height());
    layouts.child_layouts.emplace_back(
        mini_toolbar_.get(), mini_toolbar_->GetVisible(), mini_toolbar_rect);
  }

  if (container_outline_) {
    // In split view the outline frames the whole container; outside it, it
    // frames the card, so it takes the margin-inset bounds every other child
    // was laid out against.
    layouts.child_layouts.emplace_back(
        container_outline_.get(), container_outline_->GetVisible(),
        is_in_split_ ? gfx::Rect(0, 0, width, height) : full_contents_bounds);
  }

  if (capture_contents_border_view_) {
    gfx::Rect rect;
    if (auto capture_location =
            capture_contents_border_view_->capture_location();
        capture_location) {
      rect = *capture_location;
      rect.Offset(contents_view_bounds.OffsetFromOrigin());
    } else {
      rect = contents_view_bounds;
    }

#if BUILDFLAG(IS_CHROMEOS)
    // Immersive top container might overlap with the blue border in fullscreen
    // mode - see crbug.com/40880524. By insetting the bounds rectangle we
    // ensure that the blue border is always placed below the top container.
    if (ImmersiveModeController::From(browser_view_->browser())->IsRevealed()) {
      const int delta =
          browser_view_->top_container()->bounds().bottom() - rect.y();
      if (delta > 0) {
        rect.Inset(gfx::Insets().set_top(delta));
      }
    }
#endif

    bool visible = capture_contents_border_view_->GetVisible();
#if BUILDFLAG(IS_MAC)
    // Zero sized view should not be shown.
    if (rect.IsEmpty()) {
      visible = false;
    }
#endif  // BUILDFLAG(IS_MAC)

    layouts.child_layouts.emplace_back(capture_contents_border_view_.get(),
                                       visible, rect,
                                       views::SizeBounds(rect.size()));
  }

  auto* const content_layout = layouts.GetLayoutFor(contents_view_);
  if (target_content_bounds_) {
    content_layout->bounds.Outset(*target_content_bounds_);
    contents_clip_rect_ =
        gfx::Rect(gfx::Point(), content_layout->bounds.size());
    gfx::Insets insets = -target_content_bounds_->ToInsets();
    // Rendering layer isn't mirrored, so need to manually mirror insets.
    if (base::i18n::IsRTL()) {
      insets.set_left_right(insets.right(), insets.left());
    }
    contents_clip_rect_.Inset(insets);
  } else {
    contents_clip_rect_ = gfx::Rect();
  }

  layouts.host_size = gfx::Size(width, height);
  return layouts;
}

BEGIN_METADATA(ContentsContainerView)
END_METADATA

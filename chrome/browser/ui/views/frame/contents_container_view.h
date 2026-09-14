// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_CONTENTS_CONTAINER_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_CONTENTS_CONTAINER_VIEW_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/devtools/devtools_contents_resizing_strategy.h"
#include "chrome/browser/ui/views/frame/tab_modal_dialog_host.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/focus/external_focus_tracker.h"
#include "ui/views/layout/delegating_layout_manager.h"
#include "ui/views/view.h"

class BrowserView;
class ContentsCaptureBorderView;
class ContentsContainerOutline;
class ContentsWebView;
class MultiContentsViewMiniToolbar;
class ScrimView;
class ActorOverlayWebView;
class ReadAnythingImmersiveOverlayView;

namespace gfx {
class Rect;
}  // namespace gfx

namespace glic {
class ContextSharingBorderView;
}  // namespace glic

namespace new_tab_footer {
class NewTabFooterWebView;
}  // namespace new_tab_footer

namespace views {
class WebView;
}  // namespace views

namespace enterprise_data_protection {
class DataProtectionOverlayView;
}  // namespace enterprise_data_protection

// ContentsContainerView holds the ContentsWebView and the outlines and
// minitoolbar when in split view.
class ContentsContainerView : public views::View,
                              public views::LayoutDelegate,
                              public views::ViewObserver {
  METADATA_HEADER(ContentsContainerView, views::View)
 public:
  // Zephyrus: the window background left around a floating card.
  //
  // Public because the content card is not the only card. The side panel is
  // laid out by upstream code that has no reason to know about this, so the
  // layout reads the value from here rather than keeping a second copy that
  // could drift.
  static constexpr int kZephyrusContentMargin = 4;

  // The margin of window background left around the web contents so the page
  // reads as a card floating in the window rather than as the window itself.
  // Empty in fullscreen and in non-tabbed windows, where a floating card would
  // be wrong.
  //
  // Public because the customize panel matches its edges to this card, and
  // recomputing the rule outside this class is exactly how that got it wrong
  // three times.
  gfx::Insets GetZephyrusContentMargin() const;

  // Enumerates where the devtools are docked relative to the main web contents.
  enum class DevToolsDockedPlacement {
    kLeft,
    kRight,
    kBottom,
    // Devtools are not docked.
    kNone,
    kUnknown
  };

  explicit ContentsContainerView(BrowserView* browser_view);
  ContentsContainerView(ContentsContainerView&) = delete;
  ContentsContainerView& operator=(const ContentsContainerView&) = delete;
  ~ContentsContainerView() override;

  // Returns accessible panes to be used in BrowserView to create the order of
  // pane traversal.
  std::vector<views::View*> GetAccessiblePanes();

  ContentsWebView* contents_view() { return contents_view_; }
  MultiContentsViewMiniToolbar* mini_toolbar() { return mini_toolbar_; }
  ScrimView* contents_scrim_view() { return contents_scrim_view_; }
  views::WebView* devtools_web_view() { return devtools_web_view_; }
  ScrimView* devtools_scrim_view() { return devtools_scrim_view_; }
  DevToolsDockedPlacement devtools_docked_placement() {
    return current_devtools_docked_placement_;
  }
  ActorOverlayWebView* actor_overlay_web_view() {
    return actor_overlay_web_view_;
  }
  ReadAnythingImmersiveOverlayView* read_anything_immersive_overlay_view() {
    return read_anything_immersive_overlay_view_;
  }
  glic::ContextSharingBorderView* glic_border_view() { return glic_border_; }
  new_tab_footer::NewTabFooterWebView* new_tab_footer_view() {
    return new_tab_footer_view_;
  }
  ContentsCaptureBorderView* capture_contents_border_view() {
    return capture_contents_border_view_;
  }
  enterprise_data_protection::DataProtectionOverlayView*
  data_protection_overlay_view() {
    return data_protection_overlay_view_;
  }
  views::WebView* ai_overlay_dialog_view() { return ai_overlay_dialog_view_; }
  const ContentsContainerOutline* contents_outline_view() const {
    return container_outline_;
  }
  TabModalDialogHost* web_contents_modal_dialog_host() {
    return &web_contents_modal_dialog_host_;
  }

  views::View* indigo_overlay_view() { return indigo_overlay_view_; }

  // Sets the contents resizing strategy.
  void SetContentsResizingStrategy(
      const DevToolsContentsResizingStrategy& strategy);
  DevToolsContentsResizingStrategy& contents_resizing_strategy() {
    return strategy_;
  }

  void ApplyWatermarkSettings(const std::string& watermark_text,
                              SkColor fill_color,
                              SkColor outline_color,
                              int font_size);

  // Zephyrus: in a split, the edge facing the other pane has no window edge to
  // sit against, so it must not carry the card margin -- otherwise the two
  // panes stack their margins either side of the resize area and the gutter
  // comes out several times wider than the frame everywhere else.
  // 0 = none, 1 = leading, 2 = trailing.
  void SetZephyrusSuppressedEdge(int edge);

  void UpdateBorderAndOverlay(bool is_in_split,
                              bool is_active,
                              bool is_highlighted);

  void ShowCaptureContentsBorder();
  void HideCaptureContentsBorder();
  void SetCaptureContentsBorderLocation(
      std::optional<gfx::Rect> border_location);

  // Returns the contents_view bounds including ntp footer.
  gfx::Rect GetContentsViewBounds() const;

  // When set to a non-null value, overrides the target size and position for
  // this view to `target_bounds` (in local coordinates), to prevent reflow
  // during browser animation on some platforms. When this is set, the contents
  // will be resized as if the view were the modified size, then clipped down to
  // the actual size in the layout.
  void SetTargetContentBounds(
      std::optional<gfx::Outsets> target_contents_bounds);

  void SetRoundedCorners(const gfx::RoundedCornersF& corner_radii);

  views::View* GetToastAnchorView() { return toast_anchor_view_; }

 private:
  void UpdateContentsClip();

  // Updates the DevTools docked placement. It infers the docked placement from
  // the bounds of contents_webview relative to the local bounds of the
  // container that holds both contents_webview and devtools_webview.
  void UpdateDevToolsDockedPlacement();

  void UpdateBorderRoundedCorners();
  // Zephyrus: paired with UpdateBorderRoundedCorners() and called from
  // OnBoundsChanged. Lost in the 7913 -> 7922 rebase because this header
  // auto-merged to upstream's shape while the .cc kept ours, so the definition
  // survived without its declaration.
  void ClearBorderRoundedCorners();

  // NOTE for the next rebase: upstream 7922 added
  //   void SetBorderRoundedCornersFrom(const gfx::RoundedCornersF&);
  // which splits the radii out as a parameter. It is deliberately NOT adopted:
  // our UpdateBorderRoundedCorners() computes the Zephyrus floating-card radius
  // itself (split view keeps its own), so upstream's version has no caller here
  // and was declared-but-never-defined after the merge. Re-adopting it means
  // reworking our radius logic to pass the values in, not just restoring the
  // declaration.

  // Zephyrus: applies (or clears) the floating-card corner radii. Split view
  // keeps its own radius; outside it the card uses the Zephyrus one, and a
  // window with no margin gets square corners.
  void UpdateZephyrusContentCorners();



  // Whether browser chrome sits directly above the page right now.
  bool ZephyrusTopIsAttached() const;

  // views::View:
  void ChildVisibilityChanged(View* child) override;
  void Layout(PassKey) override;
  views::View::Views GetChildrenInZOrder() override;

  // views::ViewObserver:
  void OnViewBoundsChanged(View* observed_view) override;

  // LayoutDelegate:
  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override;

  bool is_in_split_ = false;
  int zephyrus_suppressed_edge_ = 0;

  raw_ptr<BrowserView> browser_view_ = nullptr;

  // An invisible view used to anchor tab toasts to the top of the contents
  // view, while being before the contents view in the focus order.
  raw_ptr<views::View> toast_anchor_view_ = nullptr;

  raw_ptr<ContentsWebView> contents_view_ = nullptr;

  TabModalDialogHost web_contents_modal_dialog_host_;

  // The view that contains devtools window for the WebContents.
  raw_ptr<views::WebView> devtools_web_view_ = nullptr;
  // The scrim view that covers the devtools area when a tab-modal dialog is
  // open.
  raw_ptr<ScrimView> devtools_scrim_view_ = nullptr;
  DevToolsDockedPlacement current_devtools_docked_placement_ =
      DevToolsDockedPlacement::kNone;

  // The view that contains the Immersive Reading Mode. This view is an overlay
  // on top of the ContentsWebView.
  raw_ptr<ReadAnythingImmersiveOverlayView>
      read_anything_immersive_overlay_view_ = nullptr;

  // The view that shows a footer at the bottom of the contents
  // container on new tab pages.
  raw_ptr<new_tab_footer::NewTabFooterWebView> new_tab_footer_view_ = nullptr;
  // Separator between the web contents and the Footer.
  raw_ptr<views::View> new_tab_footer_view_separator_ = nullptr;

  // The view that overlays the contents container.
  raw_ptr<enterprise_data_protection::DataProtectionOverlayView>
      data_protection_overlay_view_ = nullptr;

  // The overlay dialog view that is displayed on top of the web contents.
  raw_ptr<views::WebView> ai_overlay_dialog_view_ = nullptr;

  // The scrim view that covers the content area when a tab-modal dialog is
  // open.
  raw_ptr<ScrimView> contents_scrim_view_ = nullptr;

  // The view that contains the Glic Actor Overlay. The Actor Overlay is a UI
  // overlay that is shown on top of the web contents.
  raw_ptr<ActorOverlayWebView> actor_overlay_web_view_ = nullptr;

  // Contains glic selection overlay. The overlay renders a static screenshot
  // of the WebContents and is drawn on top of the WebContents.
  raw_ptr<views::View> glic_selection_overlay_view_ = nullptr;

  // The glic browser view that renders around the web contents area.
  raw_ptr<glic::ContextSharingBorderView> glic_border_ = nullptr;

  raw_ptr<MultiContentsViewMiniToolbar> mini_toolbar_ = nullptr;

  raw_ptr<ContentsContainerOutline> container_outline_ = nullptr;

  raw_ptr<ContentsCaptureBorderView> capture_contents_border_view_ = nullptr;

  // Toolbar for chrome/browser/indigo/, which determines where in the content
  // area it wants to float.
  raw_ptr<views::View> indigo_overlay_view_ = nullptr;

  // See `SetTargetContentSize()`.
  std::optional<gfx::Outsets> target_content_bounds_;

  // This is updated during layout calculation and then applied during layout.
  // It is non-empty when the contents are larger than the visible region during
  // browser animations (see `SetTargetContentWidth()`).
  mutable gfx::Rect contents_clip_rect_;

  // This is rounded corner radii that will be used.
  gfx::RoundedCornersF rounded_corner_radii_;

  DevToolsContentsResizingStrategy strategy_;
  base::ScopedObservation<View, ViewObserver> view_bounds_observer_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_CONTENTS_CONTAINER_VIEW_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_browser_compat.h"
#include "chrome/browser/ui/views/frame/zephyrus_sidebar_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_window_backdrop.h"


#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_icon_motion.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_private_workspace.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "base/task/sequenced_task_runner.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ref.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/frame/zephyrus_audio_button.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_group_model.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_icons.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tab_groups/tab_group_color.h"
#include "components/tab_groups/tab_group_visual_data.h"
#include "components/tabs/public/tab_group.h"
#include "ui/views/layout/flex_layout_view.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_tab_helper.h"
#include "chrome/browser/ui/views/download/bubble/download_toolbar_ui_controller.h"
#include "chrome/browser/ui/views/frame/zephyrus_search_overlay.h"
#include "chrome/browser/ui/views/frame/zephyrus_settings_popup.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_icons.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_setup.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/webui_url_constants.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "content/public/common/url_constants.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/url_formatter/url_formatter.h"
#include "base/strings/escape.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/display/screen.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/color_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/cursor/cursor.h"
#include "ui/views/animation/animation_delegate_views.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/view_class_properties.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/compositor/layer.h"
#include "base/i18n/case_conversion.h"
#include "cc/paint/paint_flags.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/tween.h"
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
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/omnibox/omnibox_view_views.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/masked_targeter_delegate.h"
#include "ui/views/view_shadow.h"
#include "ui/views/view_targeter.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/layout/table_layout.h"
#include "ui/views/mouse_watcher_view_host.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_tracker.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

// Effectively unbounded: ScrollView::ClipHeightTo needs a real maximum.
constexpr int kUnboundedScrollHeight = 100000;

constexpr int kRowHeight = 36;
// The full pill that used to mark the active row is gone with it: every row
// is a filled SEGMENT now (see kSegmentInnerRadius below), so a pill would
// only have applied to one row in a column of rounded rectangles. What says
// "this one" is still the container -- its colour, not its shape.

// The panel is a flush column of window chrome in every layout: square, 10dp
// padding, running from the toolbar to the bottom edge, with no free side for a
// corner to round against.
constexpr int kClassicPadding = 10;
// The covering column's shadow (clipping layout, a hover reveal, not pinned):
// low and square.
constexpr int kCoverElevation = 3;
constexpr int kFaviconSize = 16;
// M3 Expressive SEGMENTED list. A row is a filled segment, not a label
// floating on the panel: 2dp between segments, small inner corners, and the
// group's outer corners on the first and last row of a run. It is the same
// shape language the redesigned WebUI pages use, and it is what gives the
// panel a body -- before this the sidebar was six titles over empty dark.
constexpr int kRowSpacing = 2;
// Compact mode: the gap from the address bar to the first tab card.
constexpr int kCompactListGap = 14;
constexpr int kSegmentInnerRadius = 4;

// Gap between the title bar's controls once they are in the panel. The bar
// spaced them with its own pitch, which a 230dp column has no room for.
// The run's own metrics in the panel: 2dp seams between cells, 4dp corners
// inside the run, and a little height around the glyphs.
constexpr int kZephyrusCompactSeam = 2;
constexpr int kZephyrusCompactInnerRadius = 4;
// The run's height, fixed. It was the tallest control plus 3dp above and below
// -- 38dp, a toolbar cell and then some, which in a 180dp column made the run
// the heaviest thing in the panel. The controls fill the cell now; their 16dp
// glyphs are unchanged, because a toolbar glyph at 14dp lands on fractional
// device pixels at 125% and blurs.
constexpr int kZephyrusCompactRowHeight = 32;

// One run of the title bar's controls, stretched across the panel.
//
// On the bar a control is a 24dp cell in a row as wide as the window; here the
// row is 230dp and there are only a handful of controls, so the cells take an
// equal share of the width and the run ends flush with the omnibox below it.
// The BUTTONS keep their own size and sit centred in their cell -- a
// ToolbarButton right-aligns its icon, so stretching the button itself throws
// every glyph off centre by the slack (this is a bug we have already paid for
// once, in the title bar).
//
// Container geometry is the bar's: fully rounded on the run's outside, 4dp
// corners inside, 2dp seams, and the state layer on the container rather than
// the glyph.
class ZephyrusControlRow : public views::View {
  METADATA_HEADER(ZephyrusControlRow, views::View)

 public:
  ZephyrusControlRow() = default;

  // Off: no tonal cell behind each control, only the hover and press wash, the
  // same as the title bar's buttons with the containers setting off. It was
  // ignored here: in compact mode the cells stayed on whatever the setting said.
  void SetContained(bool contained) {
    if (contained_ != contained) {
      contained_ = contained;
      SchedulePaint();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    if (VisibleChildren().empty()) {
      return gfx::Size();
    }
    return gfx::Size(available_size.width().is_bounded()
                         ? available_size.width().value()
                         : 0,
                     kZephyrusCompactRowHeight);
  }

  void Layout(PassKey key) override {
    // The containers are painted by THIS view, between and around the
    // controls, so a layout that moves a seam has to repaint the whole run.
    // It did not: when a control appeared (the split-view button, say) only
    // the controls' own rects were invalidated, and the strips above them and
    // the old seams kept the previous run's cells -- a second, offset set of
    // containers peeking out along the top edge.
    SchedulePaint();
    const std::vector<views::View*> cells = VisibleChildren();
    if (cells.empty()) {
      return;
    }
    const std::vector<gfx::Rect> rects =
        CellRects(cells, std::max(0, width()));
    for (size_t i = 0; i < cells.size(); ++i) {
      const gfx::Rect& cell = rects[i];
      const gfx::Size size = cells[i]->GetPreferredSize();
      // A control never spills past its own container. Six cells in a 180dp
      // panel are narrower than a toolbar button's preferred width, and a
      // button drawn wider than the cell it is painted in silently takes the
      // clicks that land on its neighbour's container.
      const int cell_w = std::min(size.width(), cell.width());
      const int cell_h = std::min(size.height(), cell.height());
      cells[i]->SetBounds(cell.x() + (cell.width() - cell_w) / 2,
                          cell.y() + (cell.height() - cell_h) / 2, cell_w,
                          cell_h);
    }
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const std::vector<views::View*> cells = VisibleChildren();
    if (cells.empty()) {
      return;
    }
    const int count = static_cast<int>(cells.size());
    const std::vector<gfx::Rect> rects = CellRects(cells, width());
    const SkColor container =
        zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer);
    const SkColor ink =
        zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer);
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    for (int i = 0; i < count; ++i) {
      const gfx::Rect& cell = rects[i];
      const auto* button = views::AsViewClass<views::Button>(cells[i]);
      const bool pressed =
          button && button->GetState() == views::Button::STATE_PRESSED;
      const bool hovered =
          button && button->GetState() == views::Button::STATE_HOVERED;
      SkColor color = container;
      if (!contained_) {
        // No container: only a pointer over a control shows its cell.
        if (!pressed && !hovered) {
          continue;
        }
        color = zephyrus::m3::StateLayer(
            ink, pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover);
      } else if (pressed || hovered) {
        color = zephyrus::m3::WithStateLayer(
            container, ink,
            pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover);
      }
      fill.setColor(color);
      const SkScalar outer = cell.height() / 2.f;
      const SkScalar inner = kZephyrusCompactInnerRadius;
      const SkScalar left = i == 0 ? outer : inner;
      const SkScalar right = i + 1 == count ? outer : inner;
      const SkVector radii[4] = {{left, left}, {right, right},
                                 {right, right}, {left, left}};
      SkRRect rrect;
      rrect.setRectRadii(gfx::RectToSkRect(cell), radii);
      canvas->sk_canvas()->drawRRect(rrect, fill);
    }
  }

  // A control shown or hidden changes every cell's width, not only its own.
  void ChildVisibilityChanged(views::View* child) override {
    views::View::ChildVisibilityChanged(child);
    InvalidateLayout();
    SchedulePaint();
  }

  // The containers track the pointer, so the row repaints on state changes:
  // a child repainting itself only covers its glyph.
  void OnMouseMoved(const ui::MouseEvent& event) override { SchedulePaint(); }
  void OnMouseExited(const ui::MouseEvent& event) override { SchedulePaint(); }

 private:
  bool contained_ = true;

  std::vector<views::View*> VisibleChildren() const {
    std::vector<views::View*> cells;
    for (views::View* child : children()) {
      if (child->GetVisible()) {
        cells.push_back(child);
      }
    }
    return cells;
  }

  // Widths for the whole run, so Layout() and OnPaintBackground() cannot
  // disagree about where a seam is.
  //
  // Cells are equal EXCEPT for one that genuinely needs more: the shield grows
  // into a pill carrying the blocked-request count, and an equal cell clips
  // that number off -- which is what drew a capsule with nothing in it. A
  // counter you cannot read is worse than an uneven run, so the pill keeps its
  // own width and the others divide what is left.
  std::vector<gfx::Rect> CellRects(const std::vector<views::View*>& cells,
                                   int span) const {
    const int count = static_cast<int>(cells.size());
    const int seams = (count - 1) * kZephyrusCompactSeam;
    const int usable = std::max(0, span - seams);
    const int share = usable / count;
    // A wide control may not starve its neighbours: whatever it claims, it
    // leaves every other cell at least kFloor. (With a single control there is
    // nothing to protect, so the cap is the whole run -- halving it there would
    // draw one control in half a strip.)
    constexpr int kFloor = 16;
    const int cap = std::max(share, usable - (count - 1) * kFloor);

    std::vector<int> widths(count, 0);
    int claimed = 0;
    int equal_cells = 0;
    for (int i = 0; i < count; ++i) {
      // Only a control carrying TEXT may outgrow its share. Nothing lent to
      // the panel does today -- the blocked-request count is a badge ON the
      // shield now, so the shield is an ordinary cell -- but the rule is what
      // keeps the run even: a glyph is legible in any cell the run can offer,
      // and letting wide glyph buttons claim their preferred width made the
      // run ragged, three cell widths across five controls.
      const auto* labelled = views::AsViewClass<views::LabelButton>(cells[i]);
      const int preferred = cells[i]->GetPreferredSize().width();
      if (labelled && !labelled->GetText().empty() && preferred > share) {
        widths[i] = std::min(preferred, cap);
        claimed += widths[i];
      } else {
        ++equal_cells;
      }
    }
    if (equal_cells > 0) {
      const int rest = std::max(0, usable - claimed);
      // Distribute the remainder one pixel at a time rather than letting it
      // fall off the end, or the last cell is short of the panel edge.
      const int base = rest / equal_cells;
      int extra = rest % equal_cells;
      for (int i = 0; i < count; ++i) {
        if (widths[i] == 0) {
          widths[i] = base + (extra-- > 0 ? 1 : 0);
        }
      }
    }

    std::vector<gfx::Rect> rects;
    rects.reserve(count);
    int x = 0;
    for (int i = 0; i < count; ++i) {
      rects.emplace_back(x, 0, widths[i], height());
      x += widths[i] + kZephyrusCompactSeam;
    }
    return rects;
  }
};

BEGIN_METADATA(ZephyrusControlRow)
END_METADATA

// A plain icon button for the panel's own actions -- the foot bar's downloads
// shortcut. Sidebar-native, and only where the title bar has nothing to lend:
// its download button lives inside the pinned-actions container, which stays
// on the bar with the rest of the user's controls.
class ZephyrusFootIconButton : public views::ImageButton {
  METADATA_HEADER(ZephyrusFootIconButton, views::ImageButton)

 public:
  ZephyrusFootIconButton(PressedCallback callback,
                         const gfx::VectorIcon& icon,
                         const std::u16string& name)
      : views::ImageButton(std::move(callback)), icon_(icon) {
    SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
    views::InstallCircleHighlightPathGenerator(this);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(28, 28);
  }

  void OnThemeChanged() override {
    views::ImageButton::OnThemeChanged();
    SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(
            *icon_, zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant),
            18));
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() != views::Button::STATE_HOVERED &&
        GetState() != views::Button::STATE_PRESSED) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(zephyrus::m3::StateLayer(
        zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
        GetState() == views::Button::STATE_PRESSED ? zephyrus::m3::kPressed
                                                   : zephyrus::m3::kHover));
    canvas->DrawCircle(GetLocalBounds().CenterPoint(), width() / 2, flags);
  }

 private:
  const raw_ref<const gfx::VectorIcon> icon_;
};

BEGIN_METADATA(ZephyrusFootIconButton)
END_METADATA
constexpr int kSegmentEndRadius = 16;
// How far a split card's rows sit inside it, on every side.
constexpr int kSplitInset = 4;

// Fills `bounds` as one segment. `first`/`last` say which end of the run this
// is; a lone row gets the outer radius on both ends.
void PaintSegment(gfx::Canvas* canvas,
                  const gfx::Rect& bounds,
                  SkColor color,
                  bool first,
                  bool last) {
  const SkScalar top = first ? kSegmentEndRadius : kSegmentInnerRadius;
  const SkScalar bottom = last ? kSegmentEndRadius : kSegmentInnerRadius;
  // Order is top-left, top-right, bottom-right, bottom-left.
  const SkVector radii[4] = {{top, top}, {top, top}, {bottom, bottom},
                             {bottom, bottom}};
  SkRRect rrect;
  rrect.setRectRadii(gfx::RectToSkRect(bounds), radii);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  canvas->sk_canvas()->drawRRect(rrect, flags);
}

// Sidebar row context-menu command ids.
constexpr int kCloseTabCommand = 1;
constexpr int kMoveToWorkspaceSubmenu = 2;
// Zephyrus hides the horizontal tab strip, so this menu is the only place a
// tab can be pinned — there is no strip to right-click.
constexpr int kPinTabCommand = 3;
constexpr int kPinSidebarCommand = 4;
constexpr int kResetSidebarWidthCommand = 5;
constexpr int kMoveToWorkspaceBase = 100;  // + workspace index
// Sets. Below kMoveToWorkspaceBase except the base, which is handled before the
// workspace range is.
constexpr int kNewSetCommand = 6;
constexpr int kRemoveFromSetCommand = 7;
constexpr int kAddToSetSubmenu = 8;
constexpr int kUngroupSetCommand = 9;
constexpr int kRenameSetCommand = 10;
constexpr int kChangeSetIconCommand = 11;
constexpr int kPinSetCommand = 12;
constexpr int kNewTabInSetCommand = 13;
constexpr int kCloseSetCommand = 14;
constexpr int kAddToSetBase = 1000;  // + position in context_menu_set_ids_
// Springy asymmetric slide: the entrance decelerates PAST the resting point
// (overshoot) and settles back — an under-damped spring, the iOS-sheet feel —
// while the exit clears out quickly with no bounce (exits never spring).
// 200 in, 150 out. A panel belongs in the 150-250ms band; the reveal used to
// total 360ms (210 slide + 150 settle), which is over budget for something the
// cursor triggers dozens of times a day. Exit stays faster than entry: slow
// where the user is deciding, fast where the system is responding.
// 210ms in. This panel is revealed by HOVERING the window edge, which a user
// does dozens of times a session -- that frequency argues for reduction, not
// for drawer-length timing. 280ms was modal-scale for something that behaves
// much more like a dropdown, and the cost of being slow is paid every time.
// HOT PATH. The reveal fires on hover, many times a minute, so it keeps its
// short duration rather than taking M3's 350ms fast-spatial. See the note on
// kHotSlideIn -- the spec's numbers are tuned for phone-scale motion and a
// panel that makes you wait is the whole cost here.
constexpr base::TimeDelta kSlideInDuration = zephyrus::m3::kHotSlideIn;
constexpr base::TimeDelta kSlideOutDuration = zephyrus::m3::kHotSlideOut;

// Where the panel sits for a given reveal amount.
//
// The panel's right edge and the page's left edge are THE SAME SEAM, so this
// uses base::ClampFloor exactly as the layout does when it reserves the column
// — same input, same rounding, same integer. Anything else and the two edges
// disagree by a pixel that flickers as the value crosses a boundary.
//
// This is also why the old overshoot had to go: the panel travelled 7px past
// its rest position while the page's edge was clamped to the reserved width
// and could not follow, so the seam visibly tore open and snapped shut. An
// overshoot belongs to a gesture that carried momentum, not to a hover reveal.
//
// Takes the width rather than reading a constant: the panel is user-resizable,
// and if this used a fixed number while the layout reserved the real width,
// the seam would be wrong by exactly the amount the user had resized.
gfx::Transform TransformForReveal(double amount, int width) {
  const int visible = base::ClampFloor(width * amount);
  gfx::Transform transform;
  transform.Translate(-(width - visible), 0);
  return transform;
}

bool IsNewTabPage(content::WebContents* contents) {
  const GURL& url = contents->GetVisibleURL();
  return (url.SchemeIs(content::kChromeUIScheme) &&
          (url.host() == chrome::kChromeUINewTabPageHost ||
           url.host() == chrome::kChromeUINewTabHost ||
           url.host() == chrome::kChromeUINewTabPageThirdPartyHost)) ||
         url.SchemeIs(chrome::kChromeSearchScheme);
}

gfx::ImageSkia GetTabFavicon(content::WebContents* contents) {
  // The new tab page's own favicon is a greyed browser logo that read as an
  // unknown site. It shows the tinted Zephyrus mark instead (GetTabFallbackIcon).
  if (IsNewTabPage(contents)) {
    return gfx::ImageSkia();
  }
  if (auto* driver =
          favicon::ContentFaviconDriver::FromWebContents(contents)) {
    return driver->GetFavicon().AsImageSkia();
  }
  return gfx::ImageSkia();
}

// The tinted mark a row shows when the page has no favicon of its own. The new
// tab page gets the Zephyrus mark (the ribbon from inside the browser's logo);
// anything else the globe.
const gfx::VectorIcon& GetTabFallbackIcon(content::WebContents* contents) {
  return IsNewTabPage(contents) ? kZephyrusMarkIcon
                                : vector_icons::kGlobeIcon;
}

std::u16string GetTabTitle(content::WebContents* contents) {
  std::u16string title = contents->GetTitle();
  if (title.empty()) {
    // Formatted, not the raw spec: that printed "user:password@host" for a URL
    // carrying credentials, in a list that is on screen for anyone to read.
    title = url_formatter::FormatUrl(
        contents->GetVisibleURL(),
        url_formatter::kFormatUrlOmitUsernamePassword |
            url_formatter::kFormatUrlOmitHTTPS,
        base::UnescapeRule::SPACES, nullptr, nullptr, nullptr);
  }
  return title;
}

// Colors a sidebar row adapts to (derived from the active page color).
struct ZephyrusRowColors {
  SkColor foreground;  // Text, favicon fallback, close glyph.
  SkColor idle_bg;     // Resting segment fill -- every row has one now.
  SkColor active_bg;   // Active row background.
  SkColor hover_bg;    // Hovered row background.
  SkColor pressed_bg;  // Pressed row background (M3's 10% state layer).
  SkColor active_pressed_bg;  // Pressed, on the active row's own container.
  SkColor active_fg;   // Ink ON the active row -- it is inverted, so this is
                       // the ground colour, not the ink.
  // Settings > Appearance > containers. Off: idle rows are bare and every
  // fill (hover, press, the active row) is a whole pill of its own, an M3
  // navigation-drawer item, since there is no run left to be a segment of.
  bool contained = true;
};

// A quick-action / favorite row: monochrome icon + label, pill hover fill.
// (Matches the Zephyrus Browser Design mockup's sidebar rows.)
class ZephyrusActionRow : public views::LabelButton {
  METADATA_HEADER(ZephyrusActionRow, views::LabelButton)

 public:
  ZephyrusActionRow(PressedCallback callback,
                    const std::u16string& text,
                    const gfx::VectorIcon& icon,
                    SkColor foreground,
                    const gfx::ImageSkia& favicon = gfx::ImageSkia())
      : views::LabelButton(std::move(callback), text),
        foreground_(foreground),
        icon_(icon) {
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetImageLabelSpacing(10);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(6, 12)));
    SetTextColor(views::Button::STATE_NORMAL, foreground);
    SetTextColor(views::Button::STATE_HOVERED, foreground);
    SetTextColor(views::Button::STATE_PRESSED, foreground);
    // The site's own favicon when the bookmark has one: six identical globes
    // told the favourites apart by label alone.
    SetImageModel(views::Button::STATE_NORMAL,
                  favicon.isNull()
                      ? ui::ImageModel::FromVectorIcon(icon, foreground, 16)
                      : ui::ImageModel::FromImageSkia(favicon));
    label()->SetSubpixelRenderingEnabled(false);
    GetViewAccessibility().SetName(text.empty() ? u"Action" : text);
  }

  // Which end of the favourites run this row is, as for the tab rows.
  void SetSegmentPosition(bool first, bool last) {
    first_in_run_ = first;
    last_in_run_ = last;
    SchedulePaint();
  }

  // views::LabelButton:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::LabelButton::StateChanged(old_state);
    SchedulePaint();
  }

  // Follows the containers setting, as the tab rows do.
  void SetContained(bool contained) {
    if (contained_ != contained) {
      contained_ = contained;
      SchedulePaint();
    }
  }

  // A filled segment in every state, like the tab rows below it: two lists in
  // one column must not use two different shapes. With containers off, only
  // hover and press fill, as a whole pill.
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool pressed = GetState() == views::Button::STATE_PRESSED;
    const bool hovered = GetState() == views::Button::STATE_HOVERED;
    if (!contained_) {
      if (pressed || hovered) {
        PaintSegment(canvas, GetLocalBounds(),
                     zephyrus::m3::StateLayer(
                         foreground_, pressed ? zephyrus::m3::kPressed
                                              : zephyrus::m3::kHover),
                     true, true);
      }
      return;
    }
    const SkColor container =
        zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHigh);
    SkColor fill = container;
    if (pressed) {
      fill = zephyrus::m3::WithStateLayer(container, foreground_,
                                          zephyrus::m3::kPressed);
    } else if (hovered) {
      fill = zephyrus::m3::WithStateLayer(container, foreground_,
                                          zephyrus::m3::kHover);
    }
    PaintSegment(canvas, GetLocalBounds(), fill, first_in_run_, last_in_run_);
  }

  // The tab rows' height: two segmented lists in one column, one metric.
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kRowHeight);
  }

 private:
  const SkColor foreground_;
  const raw_ref<const gfx::VectorIcon> icon_;
  bool contained_ = true;
  bool first_in_run_ = true;
  bool last_in_run_ = true;
};

BEGIN_METADATA(ZephyrusActionRow)
END_METADATA

// The foot of the panel: one filled M3 button, where the version label used
// to sit.
//
// The panel offers exactly one action, and a sidebar whose only fixed element
// was a version string spent its bottom edge on something nobody acts on. The
// version is still on chrome://settings/help.
//
// Tonal rather than a full primary fill: the active tab is the loudest thing
// in this column and has to stay that way.
class ZephyrusNewTabButton : public views::Button {
  METADATA_HEADER(ZephyrusNewTabButton, views::Button)

 public:
  // Icon only: the plus says it, and the card is as wide as the list above it,
  // so the whole width is the target.
  //   Hover: the plus turns a quarter turn on M3's fast-spatial spring.
  //   Press: it shrinks to 82% while held.
  //   Click: it spins another quarter turn with a small pop, ~140ms, and THEN
  //          opens the tab. The action is posted, bound to a weak pointer,
  //          never run from the animation callback.
  // Reduced motion: no animation, and the click acts at once.
  explicit ZephyrusNewTabButton(base::RepeatingClosure on_new_tab)
      : views::Button(base::BindRepeating(&ZephyrusNewTabButton::OnClicked,
                                          base::Unretained(this))),
        on_new_tab_(std::move(on_new_tab)),
        hover_(&animation_delegate_),
        press_(&animation_delegate_),
        click_(&animation_delegate_) {
    hover_.SetTweenType(gfx::Tween::LINEAR);
    hover_.SetSlideDuration(base::Milliseconds(260));
    press_.SetTweenType(gfx::Tween::LINEAR);
    press_.SetSlideDuration(base::Milliseconds(100));
    click_.SetTweenType(gfx::Tween::LINEAR);
    click_.SetSlideDuration(base::Milliseconds(140));
    GetViewAccessibility().SetName(u"New tab");
    SetTooltipText(u"New tab");
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kCornerRadius);
  }
  ZephyrusNewTabButton(const ZephyrusNewTabButton&) = delete;
  ZephyrusNewTabButton& operator=(const ZephyrusNewTabButton&) = delete;
  ~ZephyrusNewTabButton() override = default;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(bare_ ? kHeight : 0, kHeight);
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    SchedulePaint();
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    if (acting_) {
      return;
    }
    const views::Button::ButtonState state = GetState();
    Retarget(hover_, state == STATE_HOVERED || state == STATE_PRESSED);
    Retarget(press_, state == STATE_PRESSED);
  }

  // The plus inside a set: no card, only the plus (a state layer circle under
  // the pointer), as the set's design has it.
  void SetBare(bool bare) {
    if (bare_ != bare) {
      bare_ = bare;
      PreferredSizeChanged();
      SchedulePaint();
    }
  }

  // Settings > Appearance > containers. Off, it is a text button: bare at
  // rest, a state layer under the pointer.
  void SetContained(bool contained) {
    if (contained_ != contained) {
      contained_ = contained;
      SchedulePaint();
    }
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (bare_) {
      if (GetState() == views::Button::STATE_HOVERED ||
          GetState() == views::Button::STATE_PRESSED) {
        cc::PaintFlags flags;
        flags.setAntiAlias(true);
        flags.setColor(zephyrus::m3::StateLayer(
            zephyrus::m3::Role(*this, kColorZephyrusPrimary),
            GetState() == views::Button::STATE_PRESSED
                ? zephyrus::m3::kPressed
                : zephyrus::m3::kHover));
        canvas->DrawCircle(gfx::PointF(GetLocalBounds().CenterPoint()),
                           GetLocalBounds().height() / 2.0f, flags);
      }
      return;
    }
    // A NEUTRAL container with brand-coloured ink, not a brand-filled slab.
    // Filled with primary-container it was the loudest thing in the panel --
    // louder than the active tab, which is the one row that has to win.
    const SkColor container =
        zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHigh);
    const SkColor ink = zephyrus::m3::Role(*this, kColorZephyrusPrimary);
    const bool pressed = GetState() == views::Button::STATE_PRESSED;
    const bool hovered = GetState() == views::Button::STATE_HOVERED;
    SkColor fill = container;
    if (!contained_) {
      if (!pressed && !hovered) {
        return;
      }
      fill = zephyrus::m3::StateLayer(
          ink, pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover);
    } else if (pressed) {
      fill = zephyrus::m3::WithStateLayer(container, ink,
                                          zephyrus::m3::kPressed);
    } else if (hovered) {
      fill = zephyrus::m3::WithStateLayer(container, ink, zephyrus::m3::kHover);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(fill);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kCornerRadius,
                          flags);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF bounds(GetLocalBounds());
    const float hover = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastSpatial)
                            .Solve(hover_.GetCurrentValue());
    const float press = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastEffects)
                            .Solve(press_.GetCurrentValue());
    const double click_t = click_.GetCurrentValue();
    const float click = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastEffects)
                            .Solve(click_t);
    const float degrees = 90.0f * hover + 90.0f * click;
    // The pop: up to 125% at the middle of the click, back to 100% at its end.
    const float pop = 0.25f * static_cast<float>(std::sin(M_PI * click_t));
    const float scale = (1.0f - 0.18f * press) * (1.0f + pop);
    canvas->Save();
    cc::PaintCanvas* const paint = canvas->sk_canvas();
    paint->translate(bounds.width() / 2.0f, bounds.height() / 2.0f);
    paint->rotate(degrees);
    paint->scale(scale, scale);
    paint->translate(-kIconSize / 2.0f, -kIconSize / 2.0f);
    gfx::PaintVectorIcon(canvas, vector_icons::kAdd2Icon, kIconSize,
                         zephyrus::m3::Role(*this, kColorZephyrusPrimary));
    canvas->Restore();
  }

 private:
  // Shorter than a tab row would suggest, a quiet footer to the list: 32, not
  // 40. The corners drop from 16 to 12 with it, or a 32dp card would be a
  // capsule.
  static constexpr int kHeight = 32;
  static constexpr int kCornerRadius = 12;
  static constexpr int kIconSize = 20;

  // views::Button is itself a gfx::AnimationDelegate, so ours cannot be the
  // button: it is this.
  class AnimationForwarder final : public gfx::AnimationDelegate {
   public:
    explicit AnimationForwarder(ZephyrusNewTabButton* owner) : owner_(owner) {}
    void AnimationProgressed(const gfx::Animation*) override {
      owner_->SchedulePaint();
    }
    void AnimationEnded(const gfx::Animation* animation) override {
      owner_->OnAnimationEnded(animation);
    }

   private:
    raw_ptr<ZephyrusNewTabButton> owner_;
  };

  void OnAnimationEnded(const gfx::Animation* animation) {
    SchedulePaint();
    if (animation != &click_) {
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ZephyrusNewTabButton::RunAction,
                                  weak_factory_.GetWeakPtr()));
  }

  void Retarget(gfx::SlideAnimation& animation, bool show) {
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      animation.Reset(show ? 1.0 : 0.0);
      SchedulePaint();
    } else if (show) {
      animation.Show();
    } else {
      animation.Hide();
    }
  }

  void OnClicked() {
    if (acting_) {
      return;
    }
    acting_ = true;
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      RunAction();
      return;
    }
    click_.Show();
  }

  void RunAction() {
    // Re-armed first: the list may not be rebuilt (and this button replaced)
    // by what the action does.
    acting_ = false;
    click_.Reset(0.0);
    on_new_tab_.Run();
  }

  base::RepeatingClosure on_new_tab_;
  AnimationForwarder animation_delegate_{this};
  gfx::SlideAnimation hover_;
  gfx::SlideAnimation press_;
  gfx::SlideAnimation click_;
  bool contained_ = true;
  bool acting_ = false;
  bool bare_ = false;
  base::WeakPtrFactory<ZephyrusNewTabButton> weak_factory_{this};
};

BEGIN_METADATA(ZephyrusNewTabButton)
END_METADATA

// Small quiet section caption ("Favorites", "Tabs", ...).
// Section headings are 11px UPPERCASE with wide tracking, not chips.
//
// Measured on the reference, 11px is the second-most-common type size on the
// whole site (53 elements, behind only 16px body) and it is always this: a
// small tracked label marking something as DATA rather than prose. It is the
// cheapest single move in the language and it carries further than the dot
// font does.
//
// The outlined pill chips this replaces were correct for the previous
// language. Here they would be twelve extra rounded rectangles stacked down a
// narrow rail, competing with the rows they are supposed to be labelling.
// An M3 list subheader.
//
// Was an ALL-CAPS label-small in 55% ink -- the Nothing OS section marker,
// which the overhaul retired. M3 labels a list group with title-small in the
// PRIMARY colour, in sentence case: the colour is what separates a heading
// from a row, so the type does not have to shout it.
//
// The count on the trailing edge answers "how many tabs do I have open" from
// the panel itself, which previously meant counting rows.
//
// A view rather than a bare label because it reads its own roles in
// OnThemeChanged. The Favorites heading is built once in the constructor and
// never rebuilt, so a colour captured at construction stayed stale through
// every later theme change.
class ZephyrusSectionHeader : public views::View {
  METADATA_HEADER(ZephyrusSectionHeader, views::View)

 public:
  explicit ZephyrusSectionHeader(const std::u16string& text) {
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(18, 12, 6, 12), 8));
    label_ = AddChildView(std::make_unique<views::Label>(text));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label_->SetElideBehavior(gfx::ELIDE_TAIL);
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetSubpixelRenderingEnabled(false);
    label_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kTitleSmall));

    count_ = AddChildView(std::make_unique<views::Label>());
    count_->SetAutoColorReadabilityEnabled(false);
    count_->SetSubpixelRenderingEnabled(false);
    count_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelMedium));
    count_->SetVisible(false);
    static_cast<views::BoxLayout*>(GetLayoutManager())
        ->SetFlexForView(label_, 1);
  }

  void SetCount(size_t count) {
    count_->SetText(base::NumberToString16(count));
    count_->SetVisible(true);
  }

  // Adopts a control onto the heading's trailing edge -- the new-tab button,
  // beside the count of what it adds to.
  void SetTrailingView(views::View* view) {
    AddChildView(view);
  }

  // views::View:
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    label_->SetEnabledColor(
        zephyrus::m3::Role(*this, kColorZephyrusPrimary));
    count_->SetEnabledColor(
        zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant));
  }

 private:
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::Label> count_ = nullptr;
};

BEGIN_METADATA(ZephyrusSectionHeader)
END_METADATA


// A set's name as its chip and menu entries show it.
std::u16string GetSetTitle(TabStripModel* model,
                           const tab_groups::TabGroupId& id) {
  const tab_groups::TabGroupVisualData* data =
      model->group_model()->GetTabGroup(id)->visual_data();
  return data && !data->title().empty() ? data->title() : u"Set";
}

// ---- Sets ------------------------------------------------------------------
//
// A SET is a Chromium tab group, shown the way the design shows it: a header
// (icon, name, pin, chevron) that opens and closes a body of the set's tab
// cards. The title and colour live in the group's own visual data (which the
// session saves); the icon, whether it is pinned to the top, and whether it is
// closed live in the profile prefs, keyed by the group's token, because a tab
// group has nowhere to keep them.

constexpr int kSetHeaderHeight = 32;
constexpr char kSetsPref[] = "zephyrus.sets";
constexpr char kSetsOrderPref[] = "zephyrus.sets_order";
constexpr char kDefaultSetIcon[] = "files";
constexpr size_t kMaxSetNameLength = 32;

struct SetMeta {
  std::string icon;
  bool pinned = false;
  bool collapsed = false;
};

SetMeta ReadSetMeta(PrefService* prefs, const tab_groups::TabGroupId& id) {
  SetMeta meta;
  if (const base::DictValue* dict =
          prefs->GetDict(kSetsPref).FindDict(id.ToString())) {
    if (const std::string* icon = dict->FindString("icon")) {
      meta.icon = *icon;
    }
    meta.pinned = dict->FindBool("pinned").value_or(false);
    meta.collapsed = dict->FindBool("collapsed").value_or(false);
  }
  return meta;
}

void WriteSetMeta(PrefService* prefs,
                  const tab_groups::TabGroupId& id,
                  const SetMeta& meta) {
  ScopedDictPrefUpdate update(prefs, kSetsPref);
  base::DictValue* dict = update->EnsureDict(id.ToString());
  dict->Set("icon", meta.icon);
  dict->Set("pinned", meta.pinned);
  dict->Set("collapsed", meta.collapsed);
}

// Appended once, when the set is made, so a set keeps its place.
void AppendToSetOrder(PrefService* prefs, const tab_groups::TabGroupId& id) {
  const std::string token = id.ToString();
  ScopedListPrefUpdate update(prefs, kSetsOrderPref);
  for (const base::Value& value : update.Get()) {
    if (value.is_string() && value.GetString() == token) {
      return;
    }
  }
  update->Append(token);
}

void ForgetSet(PrefService* prefs, const tab_groups::TabGroupId& id) {
  const std::string token = id.ToString();
  {
    ScopedDictPrefUpdate update(prefs, kSetsPref);
    update->Remove(token);
  }
  ScopedListPrefUpdate update(prefs, kSetsOrderPref);
  update->EraseIf([&](const base::Value& value) {
    return value.is_string() && value.GetString() == token;
  });
}

// `sets` in the order the user keeps them; sets with no stored place go last.
std::vector<tab_groups::TabGroupId> OrderedSets(
    PrefService* prefs,
    std::vector<tab_groups::TabGroupId> sets) {
  const base::ListValue& order = prefs->GetList(kSetsOrderPref);
  auto rank = [&](const tab_groups::TabGroupId& id) {
    const std::string token = id.ToString();
    for (size_t i = 0; i < order.size(); ++i) {
      if (order[i].is_string() && order[i].GetString() == token) {
        return i;
      }
    }
    return order.size();
  };
  std::stable_sort(sets.begin(), sets.end(),
                   [&](const tab_groups::TabGroupId& a,
                       const tab_groups::TabGroupId& b) {
                     return rank(a) < rank(b);
                   });
  return sets;
}

// Motion shared by the small icons of a set: see zephyrus::IconMotion.
using SetIconMotion = zephyrus::IconMotion;

// A set's icon, drawn from the workspace icon set (stroked, so it takes any
// ink). Still: it does not animate on hover, press or click.
class ZephyrusSetGlyph : public views::View {
  METADATA_HEADER(ZephyrusSetGlyph, views::View)

 public:
  explicit ZephyrusSetGlyph(const std::string& key) {
    icon_ = zephyrus::FindWorkspaceIcon(key);
    if (!icon_) {
      icon_ = zephyrus::FindWorkspaceIcon(std::string_view(kDefaultSetIcon));
    }
    SetCanProcessEventsWithinSubtree(false);
  }
  ZephyrusSetGlyph(const ZephyrusSetGlyph&) = delete;
  ZephyrusSetGlyph& operator=(const ZephyrusSetGlyph&) = delete;
  ~ZephyrusSetGlyph() override = default;

  void SetColor(SkColor color) {
    if (color_ != color) {
      color_ = color;
      SchedulePaint();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(20, 20);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (icon_) {
      zephyrus::PaintWorkspaceGlyph(canvas, *icon_,
                                    gfx::RectF(GetLocalBounds()), color_, 1.8f);
    }
  }

 private:
  raw_ptr<const zephyrus::WorkspaceIcon> icon_ = nullptr;
  SkColor color_ = SK_ColorGRAY;
};

BEGIN_METADATA(ZephyrusSetGlyph)
END_METADATA

// The pin beside a set's header: shown while the header is hovered, and always
// once the set is pinned. Hover tips it and enlarges it, pressed it dips, and a
// click swings it before the set moves.
class ZephyrusSetPinButton : public views::Button {
  METADATA_HEADER(ZephyrusSetPinButton, views::Button)

 public:
  ZephyrusSetPinButton(base::RepeatingClosure toggle,
                       base::RepeatingClosure on_hover_changed)
      : views::Button(base::BindRepeating(&ZephyrusSetPinButton::OnPressed,
                                          base::Unretained(this))),
        toggle_(std::move(toggle)),
        on_hover_changed_(std::move(on_hover_changed)),
        motion_(base::BindRepeating(&views::View::SchedulePaint,
                                    base::Unretained(this))) {
    SetFocusBehavior(FocusBehavior::NEVER);
    SetShown(false, false);
  }
  ZephyrusSetPinButton(const ZephyrusSetPinButton&) = delete;
  ZephyrusSetPinButton& operator=(const ZephyrusSetPinButton&) = delete;
  ~ZephyrusSetPinButton() override = default;

  void SetShown(bool shown, bool pinned) {
    shown_ = shown;
    pinned_ = pinned;
    // Invisible means not clickable: a click on the empty spot must not pin.
    SetEnabled(shown || pinned);
    const std::u16string name = pinned ? u"Unpin from top" : u"Pin to top";
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
    SchedulePaint();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kSize, kSize);
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    const views::Button::ButtonState state = GetState();
    motion_.SetHovered(state == STATE_HOVERED || state == STATE_PRESSED);
    motion_.SetPressed(state == STATE_PRESSED);
    if (on_hover_changed_) {
      on_hover_changed_.Run();
    }
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if ((!shown_ && !pinned_) || GetState() != views::Button::STATE_HOVERED) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(zephyrus::m3::StateLayer(
        zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
        zephyrus::m3::kHover));
    canvas->DrawCircle(gfx::PointF(GetLocalBounds().CenterPoint()),
                       GetLocalBounds().width() / 2.0f, flags);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (!shown_ && !pinned_) {
      return;
    }
    const float hover = motion_.Hover();
    const float press = motion_.Press();
    const float pop = motion_.Pop();
    canvas->Save();
    cc::PaintCanvas* const paint = canvas->sk_canvas();
    paint->translate(kSize / 2.0f, kSize / 2.0f);
    paint->rotate(-16.0f * hover + 26.0f * pop);
    const float scale = 1.0f + 0.14f * hover - 0.14f * press + 0.22f * pop;
    paint->scale(scale, scale);
    paint->translate(-kIconSize / 2.0f, -kIconSize / 2.0f);
    gfx::PaintVectorIcon(
        canvas, kKeepIcon, kIconSize,
        zephyrus::m3::Role(*this, pinned_ ? kColorZephyrusPrimary
                                          : kColorZephyrusOnSurfaceVariant));
    canvas->Restore();
  }

 private:
  static constexpr int kSize = 24;
  static constexpr int kIconSize = 16;

  void OnPressed(const ui::Event& event) {
    motion_.Click();
    // After the swing, not before: pinning moves the set and rebuilds the list.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusSetPinButton::RunToggle,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(170));
  }
  void RunToggle() { toggle_.Run(); }

  base::RepeatingClosure toggle_;
  base::RepeatingClosure on_hover_changed_;
  bool shown_ = false;
  bool pinned_ = false;
  SetIconMotion motion_;
  base::WeakPtrFactory<ZephyrusSetPinButton> weak_factory_{this};
};

BEGIN_METADATA(ZephyrusSetPinButton)
END_METADATA

// The plus in a set's header: a new tab in that set. It turns on hover, dips
// while pressed, and spins and pops on click (the tab opens just after, since
// opening it rebuilds the list).
class ZephyrusSetPlusButton : public views::Button {
  METADATA_HEADER(ZephyrusSetPlusButton, views::Button)

 public:
  explicit ZephyrusSetPlusButton(base::RepeatingClosure new_tab)
      : views::Button(base::BindRepeating(&ZephyrusSetPlusButton::OnPressed,
                                          base::Unretained(this))),
        new_tab_(std::move(new_tab)),
        motion_(base::BindRepeating(&views::View::SchedulePaint,
                                    base::Unretained(this))) {
    SetFocusBehavior(FocusBehavior::NEVER);
    GetViewAccessibility().SetName(u"New tab in set");
    SetTooltipText(u"New tab in set");
  }
  ZephyrusSetPlusButton(const ZephyrusSetPlusButton&) = delete;
  ZephyrusSetPlusButton& operator=(const ZephyrusSetPlusButton&) = delete;
  ~ZephyrusSetPlusButton() override = default;

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kSize, kSize);
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    const views::Button::ButtonState state = GetState();
    motion_.SetHovered(state == STATE_HOVERED || state == STATE_PRESSED);
    motion_.SetPressed(state == STATE_PRESSED);
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (GetState() != views::Button::STATE_HOVERED &&
        GetState() != views::Button::STATE_PRESSED) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(zephyrus::m3::StateLayer(
        zephyrus::m3::Role(*this, kColorZephyrusPrimary),
        GetState() == views::Button::STATE_PRESSED ? zephyrus::m3::kPressed
                                                   : zephyrus::m3::kHover));
    canvas->DrawCircle(gfx::PointF(GetLocalBounds().CenterPoint()),
                       GetLocalBounds().width() / 2.0f, flags);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const float hover = motion_.Hover();
    const float press = motion_.Press();
    const float pop = motion_.Pop();
    canvas->Save();
    cc::PaintCanvas* const paint = canvas->sk_canvas();
    paint->translate(kSize / 2.0f, kSize / 2.0f);
    paint->rotate(90.0f * hover + 90.0f * pop);
    const float scale = 1.0f - 0.14f * press + 0.2f * pop;
    paint->scale(scale, scale);
    paint->translate(-kIconSize / 2.0f, -kIconSize / 2.0f);
    gfx::PaintVectorIcon(canvas, vector_icons::kAdd2Icon, kIconSize,
                         zephyrus::m3::Role(*this, kColorZephyrusPrimary));
    canvas->Restore();
  }

 private:
  static constexpr int kSize = 24;
  static constexpr int kIconSize = 16;

  void OnPressed(const ui::Event& event) {
    motion_.Click();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusSetPlusButton::RunNewTab,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(150));
  }
  void RunNewTab() { new_tab_.Run(); }

  base::RepeatingClosure new_tab_;
  SetIconMotion motion_;
  base::WeakPtrFactory<ZephyrusSetPlusButton> weak_factory_{this};
};

BEGIN_METADATA(ZephyrusSetPlusButton)
END_METADATA

// The dropdown control: the chevron in a round container of its own, at the
// end of the header's capsule. Concentric with it (Rule 2): the capsule is 32dp
// tall, so its end has a radius of 16, and the container sits 4dp inside that
// end -- a 24dp circle, radius 16 - 4 = 12. Clicking it opens or closes the
// set. The container shows hover as a state layer; the chevron itself has no
// hover motion, dips while pressed and pops on a click.
class ZephyrusSetToggleButton : public views::Button {
  METADATA_HEADER(ZephyrusSetToggleButton, views::Button)

 public:
  static constexpr int kSize = 24;

  explicit ZephyrusSetToggleButton(base::RepeatingClosure toggle)
      : views::Button(base::BindRepeating(&ZephyrusSetToggleButton::OnPressed,
                                          base::Unretained(this))),
        toggle_(std::move(toggle)),
        motion_(base::BindRepeating(&views::View::SchedulePaint,
                                    base::Unretained(this))) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
    views::InstallCircleHighlightPathGenerator(this);
    SetExpanded(true);
  }
  ZephyrusSetToggleButton(const ZephyrusSetToggleButton&) = delete;
  ZephyrusSetToggleButton& operator=(const ZephyrusSetToggleButton&) = delete;
  ~ZephyrusSetToggleButton() override = default;

  void SetAngle(float degrees) {
    if (angle_ != degrees) {
      angle_ = degrees;
      SchedulePaint();
    }
  }
  void SetColor(SkColor color) {
    if (color_ != color) {
      color_ = color;
      SchedulePaint();
    }
  }
  void SetExpanded(bool expanded) {
    const std::u16string name = expanded ? u"Close set" : u"Open set";
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kSize, kSize);
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    motion_.SetPressed(GetState() == STATE_PRESSED);
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    // The content ink as a wash, so it reads on the capsule whatever its tone.
    uint8_t alpha = 0x1F;
    if (GetState() == views::Button::STATE_PRESSED) {
      alpha = 0x3D;
    } else if (GetState() == views::Button::STATE_HOVERED) {
      alpha = 0x33;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(color_, alpha));
    canvas->DrawCircle(gfx::PointF(GetLocalBounds().CenterPoint()),
                       GetLocalBounds().width() / 2.0f, flags);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const float press = motion_.Press();
    const float pop = motion_.Pop();
    canvas->Save();
    cc::PaintCanvas* const paint = canvas->sk_canvas();
    paint->translate(kSize / 2.0f, kSize / 2.0f);
    paint->rotate(angle_);
    const float scale = 1.0f - 0.16f * press + 0.18f * pop;
    paint->scale(scale, scale);
    paint->translate(-kIconSize / 2.0f, -kIconSize / 2.0f);
    gfx::PaintVectorIcon(canvas, kZephyrusExpandMoreIcon, kIconSize, color_);
    canvas->Restore();
  }

 private:
  static constexpr int kIconSize = 16;

  void OnPressed(const ui::Event& event) {
    motion_.Click();
    toggle_.Run();
  }

  base::RepeatingClosure toggle_;
  float angle_ = 0.0f;
  SkColor color_ = SK_ColorGRAY;
  SetIconMotion motion_;
};

BEGIN_METADATA(ZephyrusSetToggleButton)
END_METADATA

// The name's editor while a set is being renamed. Commits when it loses focus.
class ZephyrusSetRenameField : public views::Textfield {
  METADATA_HEADER(ZephyrusSetRenameField, views::Textfield)

 public:
  explicit ZephyrusSetRenameField(base::RepeatingClosure on_blur)
      : on_blur_(std::move(on_blur)) {}
  ZephyrusSetRenameField(const ZephyrusSetRenameField&) = delete;
  ZephyrusSetRenameField& operator=(const ZephyrusSetRenameField&) = delete;
  ~ZephyrusSetRenameField() override = default;

  void OnBlur() override {
    views::Textfield::OnBlur();
    if (on_blur_) {
      on_blur_.Run();
    }
  }

 private:
  base::RepeatingClosure on_blur_;
};

BEGIN_METADATA(ZephyrusSetRenameField)
END_METADATA

// A set's header, as designed: a capsule that hugs its contents -- the set's
// icon, its name, the pin, a new tab, a chevron -- centred above the cards.
// Click to open or close; F2 or the menu to rename.
class ZephyrusSetHeader : public views::Button,
                          public views::TextfieldController {
  METADATA_HEADER(ZephyrusSetHeader, views::Button)

 public:
  struct Callbacks {
    base::RepeatingCallback<void(const std::u16string&)> rename;
    base::RepeatingCallback<void(bool)> rename_active;
    base::RepeatingClosure toggle_pin;
    base::RepeatingClosure new_tab;
    // The header is dragged: where in it it was taken, and the cursor.
    base::RepeatingCallback<void(const gfx::Point&, const gfx::Point&)> drag;
    // The drag ended; true if it was cancelled.
    base::RepeatingCallback<void(bool)> drag_end;
    // The icon was double-clicked: open the icon picker.
    base::RepeatingClosure change_icon;
  };

  ZephyrusSetHeader(const tab_groups::TabGroupId& id,
                    const std::u16string& title,
                    const std::string& icon_key,
                    bool pinned,
                    bool has_active,
                    base::RepeatingClosure toggle,
                    Callbacks callbacks)
      : views::Button(views::Button::PressedCallback()),
        id_(id),
        has_active_(has_active),
        toggle_(std::move(toggle)),
        callbacks_(std::move(callbacks)) {
    // Stays hovered while the pointer is over the pin or the plus inside it.
    SetNotifyEnterExitOnChild(true);
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, 12, 0, 4), 6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    layout->set_minimum_cross_axis_size(kSetHeaderHeight);
    glyph_ = AddChildView(std::make_unique<ZephyrusSetGlyph>(icon_key));
    label_ = AddChildView(std::make_unique<views::Label>(title));
    // Smaller than the old 14px, and the one flexible child: a long name gives
    // way (and elides) instead of pushing the buttons out of the capsule.
    label_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelMedium));
    label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label_->SetElideBehavior(gfx::ELIDE_TAIL);
    label_->SetAutoColorReadabilityEnabled(false);
    label_->SetSubpixelRenderingEnabled(false);
    label_->SetCanProcessEventsWithinSubtree(false);
    editor_ = AddChildView(std::make_unique<ZephyrusSetRenameField>(
        base::BindRepeating(&ZephyrusSetHeader::EndRename,
                            base::Unretained(this), true)));
    editor_->set_controller(this);
    editor_->SetVisible(false);
    editor_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelMedium));
    editor_->SetDefaultWidthInChars(7);
    editor_->GetViewAccessibility().SetName(u"Set name");
    pin_ = AddChildView(std::make_unique<ZephyrusSetPinButton>(
        callbacks_.toggle_pin, base::RepeatingClosure()));
    pin_->SetShown(true, pinned);
    plus_ = AddChildView(
        std::make_unique<ZephyrusSetPlusButton>(callbacks_.new_tab));
    // The dropdown control, in its own container at the capsule's end.
    chevron_ = AddChildView(std::make_unique<ZephyrusSetToggleButton>(toggle_));
    layout->SetFlexForView(label_, 1);
    layout->SetFlexForView(editor_, 1);
    GetViewAccessibility().SetName(title);
    // The whole name, for when it is cut short.
    SetTooltipText(title);
    views::FocusRing::Install(this);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                  kSetHeaderHeight / 2);
  }
  ZephyrusSetHeader(const ZephyrusSetHeader&) = delete;
  ZephyrusSetHeader& operator=(const ZephyrusSetHeader&) = delete;
  ~ZephyrusSetHeader() override = default;

  const tab_groups::TabGroupId& set_id() const { return id_; }

  // 1 open, 0 closed. Turns the chevron.
  void SetExpansion(double value) {
    expansion_ = value;
    chevron_->SetAngle(static_cast<float>(-90.0 * (1.0 - value)));
    chevron_->SetExpanded(value > 0.5);
    ApplyColors();
    SchedulePaint();
  }

  void BeginRename() {
    if (renaming_) {
      return;
    }
    renaming_ = true;
    editor_->SetText(label_->GetText());
    label_->SetVisible(false);
    editor_->SetVisible(true);
    callbacks_.rename_active.Run(true);
    editor_->RequestFocus();
    editor_->SelectAll(true);
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    ApplyColors();
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_F2) {
      BeginRename();
      return true;
    }
    return views::Button::OnKeyPressed(event);
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    // No hover or press layer: the capsule is not the control, the container
    // at its end is.
    const bool lit = (has_active_ && expansion_ < 0.5) || drop_highlight_;
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(zephyrus::m3::Role(
        *this, lit ? kColorZephyrusSecondaryContainer
                   : kColorZephyrusSurfaceContainerHighest));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kSetHeaderHeight / 2.0f,
                          flags);
    if (drop_highlight_) {
      // The ring that says "drop here".
      cc::PaintFlags ring;
      ring.setAntiAlias(true);
      ring.setStyle(cc::PaintFlags::kStroke_Style);
      ring.setStrokeWidth(2);
      ring.setColor(zephyrus::m3::Role(*this, kColorZephyrusPrimary));
      gfx::RectF bounds(GetLocalBounds());
      bounds.Inset(1.0f);
      canvas->DrawRoundRect(bounds, kSetHeaderHeight / 2.0f - 1.0f, ring);
    }
  }

  // A tab being dragged is over this set: it lights up.
  void SetDropHighlight(bool on) {
    if (drop_highlight_ != on) {
      drop_highlight_ = on;
      SchedulePaint();
    }
  }
  std::u16string GetTitle() const { return std::u16string(label_->GetText()); }

  // Dragging the header (by the capsule, not by the buttons in it) moves the
  // set in the list.
  bool OnMousePressed(const ui::MouseEvent& event) override {
    press_point_ = event.location();
    // Double-click the name to rename it in place, the icon to change it.
    if (!renaming_ && event.IsOnlyLeftMouseButton() &&
        event.GetClickCount() == 2) {
      auto hit = [&](const views::View* view) {
        gfx::Rect bounds = view->bounds();
        bounds.Inset(-3);
        return view->GetVisible() && bounds.Contains(event.location());
      };
      if (callbacks_.change_icon && hit(glyph_)) {
        callbacks_.change_icon.Run();
        return true;
      }
      if (hit(label_)) {
        BeginRename();
        return true;
      }
    }
    return views::Button::OnMousePressed(event);
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    constexpr int kDragThreshold = 6;
    if (renaming_ || !callbacks_.drag ||
        (!dragging_ && (event.location() - press_point_).Length() <
                           kDragThreshold)) {
      return views::Button::OnMouseDragged(event);
    }
    if (!dragging_) {
      dragging_ = true;
      // The drag travels over the page; capture keeps the events coming here.
      if (GetWidget()) {
        GetWidget()->SetCapture(this);
      }
    }
    callbacks_.drag.Run(press_point_,
                        display::Screen::Get()->GetCursorScreenPoint());
    return true;
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (!dragging_) {
      views::Button::OnMouseReleased(event);
      return;
    }
    dragging_ = false;
    if (GetWidget()) {
      GetWidget()->ReleaseCapture();
    }
    if (callbacks_.drag_end) {
      callbacks_.drag_end.Run(false);
    }
  }
  void OnMouseCaptureLost() override {
    if (!dragging_) {
      views::Button::OnMouseCaptureLost();
      return;
    }
    dragging_ = false;
    if (callbacks_.drag_end) {
      callbacks_.drag_end.Run(true);
    }
  }

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override {
    if (key_event.type() != ui::EventType::kKeyPressed) {
      return false;
    }
    if (key_event.key_code() == ui::VKEY_RETURN) {
      EndRename(true);
      return true;
    }
    if (key_event.key_code() == ui::VKEY_ESCAPE) {
      EndRename(false);
      return true;
    }
    return false;
  }

 private:
  void ApplyColors() {
    if (!GetWidget()) {
      return;
    }
    const bool lit = has_active_ && expansion_ < 0.5;
    const SkColor ink = zephyrus::m3::Role(
        *this, lit ? kColorZephyrusOnSecondaryContainer
                   : kColorZephyrusOnSurface);
    const SkColor variant = zephyrus::m3::Role(
        *this, lit ? kColorZephyrusOnSecondaryContainer
                   : kColorZephyrusOnSurfaceVariant);
    glyph_->SetColor(variant);
    label_->SetEnabledColor(ink);
    editor_->SetColor(ink);
    chevron_->SetColor(variant);
  }

  void EndRename(bool commit) {
    if (!renaming_) {
      return;
    }
    renaming_ = false;
    std::u16string text;
    base::TrimWhitespace(editor_->GetText(), base::TRIM_ALL, &text);
    if (text.size() > kMaxSetNameLength) {
      text.resize(kMaxSetNameLength);
    }
    editor_->SetVisible(false);
    label_->SetVisible(true);
    // The editor held focus; without this the header takes it and shows a
    // keyboard ring around itself.
    if (GetFocusManager()) {
      GetFocusManager()->ClearFocus();
    }
    callbacks_.rename_active.Run(false);
    if (commit && !text.empty() && text != label_->GetText()) {
      label_->SetText(text);
      GetViewAccessibility().SetName(text);
      SetTooltipText(text);
      callbacks_.rename.Run(text);
    }
  }

  const tab_groups::TabGroupId id_;
  bool has_active_;
  double expansion_ = 1.0;
  bool renaming_ = false;
  bool dragging_ = false;
  bool drop_highlight_ = false;
  gfx::Point press_point_;
  base::RepeatingClosure toggle_;
  Callbacks callbacks_;
  raw_ptr<ZephyrusSetGlyph> glyph_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<ZephyrusSetRenameField> editor_ = nullptr;
  raw_ptr<ZephyrusSetPinButton> pin_ = nullptr;
  raw_ptr<ZephyrusSetPlusButton> plus_ = nullptr;
  raw_ptr<ZephyrusSetToggleButton> chevron_ = nullptr;
};

BEGIN_METADATA(ZephyrusSetHeader)
END_METADATA

// The tab cards under a header. Its height follows how far the set is open, and
// it clips what is not showing yet.
}  // namespace

// Folds a closed tab's row away: its height and its opacity run to nothing, and
// the rows around it follow through layout. Owned by the sidebar until it ends.
class ZephyrusRowCloser : public gfx::AnimationDelegate {
 public:
  ZephyrusRowCloser(views::View* row, base::OnceClosure done)
      : tracker_(row),
        height_(row->height()),
        done_(std::move(done)),
        anim_(this) {
    // A layer that clips, so the title does not spill over the rows it passes
    // while the row is shorter than its contents.
    row->SetCanProcessEventsWithinSubtree(false);
    row->SetPaintToLayer();
    row->layer()->SetFillsBoundsOpaquely(false);
    row->layer()->SetMasksToBounds(true);
    anim_.SetTweenType(gfx::Tween::EASE_OUT);
    anim_.SetSlideDuration(base::Milliseconds(200));
    anim_.Show();
  }
  ZephyrusRowCloser(const ZephyrusRowCloser&) = delete;
  ZephyrusRowCloser& operator=(const ZephyrusRowCloser&) = delete;
  ~ZephyrusRowCloser() override = default;

  bool finished() const { return finished_; }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    views::View* row = tracker_.view();
    if (!row) {
      return;
    }
    const double t = animation->GetCurrentValue();
    row->SetPreferredSize(
        gfx::Size(0, base::ClampRound(height_ * (1.0 - t))));
    if (row->layer()) {
      // Gone before the height is, so the text is not seen squashed.
      row->layer()->SetOpacity(static_cast<float>(std::max(0.0, 1.0 - 1.6 * t)));
    }
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    finished_ = true;
    if (views::View* row = tracker_.view()) {
      row->SetVisible(false);
    }
    if (done_) {
      std::move(done_).Run();
    }
  }

 private:
  views::ViewTracker tracker_;
  const int height_;
  base::OnceClosure done_;
  bool finished_ = false;
  gfx::SlideAnimation anim_;
};

namespace {

// The gap that opens where a dragged tab would land: a tonal outline of a row
// that grows in, and closes again when the tab moves on.
class ZephyrusDropSlot : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(ZephyrusDropSlot, views::View)

 public:
  ZephyrusDropSlot() : anim_(this) {
    SetCanProcessEventsWithinSubtree(false);
    anim_.SetTweenType(gfx::Tween::EASE_OUT);
    anim_.SetSlideDuration(base::Milliseconds(160));
    if (gfx::Animation::ShouldRenderRichAnimation()) {
      anim_.Reset(0.0);
      anim_.Show();
    } else {
      anim_.Reset(1.0);
    }
  }
  ZephyrusDropSlot(const ZephyrusDropSlot&) = delete;
  ZephyrusDropSlot& operator=(const ZephyrusDropSlot&) = delete;
  ~ZephyrusDropSlot() override = default;

  // Closes, then takes itself out of the list.
  void Dismiss() {
    dismissed_ = true;
    if (gfx::Animation::ShouldRenderRichAnimation()) {
      anim_.Hide();
    } else {
      anim_.Reset(0.0);
      RemoveSoon();
    }
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(
        0, base::ClampRound(kRowHeight * anim_.GetCurrentValue()));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (height() < 4) {
      return;
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetA(
        zephyrus::m3::Role(*this, kColorZephyrusPrimary), 0x38));
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), 12.f, flags);
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    PreferredSizeChanged();
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    if (dismissed_) {
      RemoveSoon();
    }
  }

 private:
  // Not from inside the animation callback: removing deletes this.
  void RemoveSoon() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](std::unique_ptr<views::ViewTracker> tracker) {
              views::View* view = tracker->view();
              if (view && view->parent()) {
                view->parent()->RemoveChildViewT(view);
              }
            },
            std::make_unique<views::ViewTracker>(this)));
  }

  gfx::SlideAnimation anim_;
  bool dismissed_ = false;
};

BEGIN_METADATA(ZephyrusDropSlot)
END_METADATA

class ZephyrusSetBody : public views::BoxLayoutView {
  METADATA_HEADER(ZephyrusSetBody, views::BoxLayoutView)

 public:
  ZephyrusSetBody() {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    SetBetweenChildSpacing(kRowSpacing);
    // The gap under the header, inside the body so it opens and closes with it.
    SetInsideBorderInsets(gfx::Insets::TLBR(6, 0, 0, 0));
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetMasksToBounds(true);
  }
  ZephyrusSetBody(const ZephyrusSetBody&) = delete;
  ZephyrusSetBody& operator=(const ZephyrusSetBody&) = delete;
  ~ZephyrusSetBody() override = default;

  void SetProgress(double progress) {
    if (progress_ == progress) {
      return;
    }
    progress_ = progress;
    SetVisible(progress_ > 0.001);
    PreferredSizeChanged();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size size = views::BoxLayoutView::CalculatePreferredSize(available_size);
    size.set_height(base::ClampRound(size.height() * progress_));
    return size;
  }

 private:
  double progress_ = 1.0;
};

BEGIN_METADATA(ZephyrusSetBody)
END_METADATA

// One set in the list, laid out as designed: the header centred and only as
// wide as its contents, the cards full width beneath it. It owns the animation
// that opens and closes the body, and the one that plays when the set is first
// made.
class ZephyrusSetSection : public views::View, public gfx::AnimationDelegate {
  METADATA_HEADER(ZephyrusSetSection, views::View)

 public:
  struct Callbacks {
    base::RepeatingCallback<void(bool)> collapsed_changed;
    base::RepeatingClosure toggle_pin;
    base::RepeatingCallback<void(const std::u16string&)> rename;
    base::RepeatingCallback<void(bool)> rename_active;
    base::RepeatingClosure new_tab;
    base::RepeatingCallback<void(const gfx::Point&, const gfx::Point&)> drag;
    base::RepeatingCallback<void(bool)> drag_end;
    base::RepeatingClosure change_icon;
  };

  ZephyrusSetSection(const tab_groups::TabGroupId& id,
                     const std::u16string& title,
                     const std::string& icon_key,
                     bool pinned,
                     bool collapsed,
                     bool has_active,
                     int count,
                     Callbacks callbacks)
      : id_(id),
        expanded_(!collapsed),
        collapsed_changed_(callbacks.collapsed_changed),
        anim_(this) {
    header_ = AddChildView(std::make_unique<ZephyrusSetHeader>(
        id, title, icon_key, pinned, has_active,
        base::BindRepeating(&ZephyrusSetSection::Toggle,
                            base::Unretained(this)),
        ZephyrusSetHeader::Callbacks{callbacks.rename, callbacks.rename_active,
                                     callbacks.toggle_pin, callbacks.new_tab,
                                     callbacks.drag, callbacks.drag_end,
                                     callbacks.change_icon}));
    body_ = AddChildView(std::make_unique<ZephyrusSetBody>());
    anim_.SetTweenType(gfx::Tween::EASE_OUT);
    anim_.SetSlideDuration(base::Milliseconds(220));
    anim_.Reset(expanded_ ? 1.0 : 0.0);
    Apply(anim_.GetCurrentValue());
  }
  ZephyrusSetSection(const ZephyrusSetSection&) = delete;
  ZephyrusSetSection& operator=(const ZephyrusSetSection&) = delete;
  ~ZephyrusSetSection() override = default;

  const tab_groups::TabGroupId& set_id() const { return id_; }
  ZephyrusSetHeader* header() { return header_; }
  ZephyrusSetBody* body() { return body_; }
  void SetDropHighlight(bool on) { header_->SetDropHighlight(on); }

  // The set was just made: open from nothing.
  void PlayCreation() {
    if (!gfx::Animation::ShouldRenderRichAnimation() || !expanded_) {
      return;
    }
    anim_.Reset(0.0);
    Apply(0.0);
    anim_.Show();
  }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0,
                     kSetHeaderHeight + body_->GetPreferredSize().height());
  }

  void Layout(PassKey) override {
    const int width = this->width();
    // Half the cards' width at least, as designed; wider for a long name.
    const int header_width =
        std::clamp(header_->GetPreferredSize().width(), width / 2, width);
    header_->SetBounds((width - header_width) / 2, 0, header_width,
                       kSetHeaderHeight);
    body_->SetBounds(0, kSetHeaderHeight, width,
                     body_->GetPreferredSize().height());
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    Apply(animation->GetCurrentValue());
  }

 private:
  void Toggle() {
    expanded_ = !expanded_;
    if (gfx::Animation::ShouldRenderRichAnimation()) {
      if (expanded_) {
        anim_.Show();
      } else {
        anim_.Hide();
      }
    } else {
      anim_.Reset(expanded_ ? 1.0 : 0.0);
      Apply(anim_.GetCurrentValue());
    }
    collapsed_changed_.Run(!expanded_);
  }

  void Apply(double value) {
    body_->SetProgress(value);
    header_->SetExpansion(value);
  }

  const tab_groups::TabGroupId id_;
  bool expanded_;
  base::RepeatingCallback<void(bool)> collapsed_changed_;
  gfx::SlideAnimation anim_;
  raw_ptr<ZephyrusSetHeader> header_ = nullptr;
  raw_ptr<ZephyrusSetBody> body_ = nullptr;
};

BEGIN_METADATA(ZephyrusSetSection)
END_METADATA

// The rounded rule between the sets and the loose tabs under them.
class ZephyrusRoundedDivider : public views::View {
  METADATA_HEADER(ZephyrusRoundedDivider, views::View)

 public:
  ZephyrusRoundedDivider() { SetCanProcessEventsWithinSubtree(false); }
  ZephyrusRoundedDivider(const ZephyrusRoundedDivider&) = delete;
  ZephyrusRoundedDivider& operator=(const ZephyrusRoundedDivider&) = delete;
  ~ZephyrusRoundedDivider() override = default;

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kSpaceAbove + kThickness + kSpaceBelow);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(
        zephyrus::m3::Role(*this, kColorZephyrusOutlineVariant));
    // Half the list's width, centred.
    const int line_width = width() / 2;
    canvas->DrawRoundRect(
        gfx::RectF((width() - line_width) / 2, kSpaceAbove, line_width,
                   kThickness),
        kThickness / 2.0f, flags);
  }

 private:
  static constexpr int kThickness = 3;
  // The set above carries an 8dp bottom margin and the list spaces its children
  // 2dp apart, so the gaps are made equal here: 8 + 2 above, 8 + 2 below.
  static constexpr int kSpaceAbove = 0;
  static constexpr int kSpaceBelow = 8;
};

BEGIN_METADATA(ZephyrusRoundedDivider)
END_METADATA

ZephyrusSetSection* FindSetSection(views::View* container,
                                   const tab_groups::TabGroupId& id) {
  for (views::View* child : container->children()) {
    auto* section = views::AsViewClass<ZephyrusSetSection>(child);
    if (section && section->set_id() == id) {
      return section;
    }
  }
  return nullptr;
}

// The tab's close button.
//
// Fills a red circle on hover, matching the window's own close control -- the
// destructive action is the one place the accent appears in either strip, and
// using it in both keeps "red means this closes something" a rule rather than
// a coincidence.
class ZephyrusTabCloseButton : public views::ImageButton {
  METADATA_HEADER(ZephyrusTabCloseButton, views::ImageButton)

 public:
  explicit ZephyrusTabCloseButton(PressedCallback callback)
      : views::ImageButton(std::move(callback)) {}

  // The row hands down the ink appropriate to its CURRENT state, since the
  // active row inverts. Hover then overrides it: the glyph sits on the accent,
  // so it takes the accent's own ink rather than the row's.
  // An M3 ICON BUTTON: the glyph keeps the row's ink in every state, and
  // hover/press are state layers of that ink. It was the retired language's
  // one red, read from the process-wide legacy palette -- which neither
  // follows a window's own workspace theme nor is an M3 role.
  void SetRowInk(SkColor on_row) {
    ink_ = on_row;
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(views::kCloseIcon, on_row,
                                                 kGlyph));
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == STATE_HOVERED;
    const bool pressed = GetState() == STATE_PRESSED;
    if (!hovered && !pressed) {
      return;
    }
    const SkColor fill = zephyrus::m3::StateLayer(
        ink_, pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(fill);
    const gfx::Rect b = GetContentsBounds();
    canvas->DrawCircle(gfx::PointF(b.CenterPoint()),
                       std::min(b.width(), b.height()) / 2.f, flags);
  }

 private:
  static constexpr int kGlyph = 16;
  SkColor ink_ = SK_ColorTRANSPARENT;
};

BEGIN_METADATA(ZephyrusTabCloseButton)
END_METADATA

// A new tab's row arrives rather than appearing: it fades in while sliding in
// from the leading edge, ~220ms. Compositor-driven (opacity and transform).
// Skipped for reduced motion.
void AnimateRowIn(views::View* row) {
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    return;
  }
  row->SetPaintToLayer();
  ui::Layer* layer = row->layer();
  layer->SetFillsBoundsOpaquely(false);
  gfx::Transform from;
  from.Translate(-16.f, 0.f);
  layer->SetOpacity(0.f);
  layer->SetTransform(from);
  ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
  settings.SetTransitionDuration(base::Milliseconds(220));
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  layer->SetOpacity(1.f);
  layer->SetTransform(gfx::Transform());
}

// The rows after a newly added one (and the New tab button) were laid out
// `shift` lower than before; start each that much higher and let it settle, so
// the list opens a gap rather than jumping. Same duration and curve as
// AnimateRowIn.
void SlideRowsBelow(views::View* container, views::View* added, int shift) {
  if (!gfx::Animation::ShouldRenderRichAnimation()) {
    return;
  }
  bool after = false;
  for (views::View* child : container->children()) {
    if (child == added) {
      after = true;
      continue;
    }
    if (!after || !child->GetVisible()) {
      continue;
    }
    child->SetPaintToLayer();
    ui::Layer* layer = child->layer();
    layer->SetFillsBoundsOpaquely(false);
    gfx::Transform from;
    from.Translate(0.f, -static_cast<float>(shift));
    layer->SetTransform(from);
    ui::ScopedLayerAnimationSettings settings(layer->GetAnimator());
    settings.SetTransitionDuration(base::Milliseconds(220));
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    layer->SetTransform(gfx::Transform());
  }
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
    full_title_ = accessible_title;
    GetViewAccessibility().SetName(accessible_title);
    // The tooltip is set in Layout(), and only when the title is actually cut
    // off. Every row used to carry one, so pointing at a row whose title was
    // fully readable still popped a box repeating it over the row below.
    InvalidateLayout();

    // Stay "hovered" while the cursor is over the child close button; otherwise
    // showing the close button under the cursor makes the row flip-flop between
    // hovered/normal, blinking the button.
    SetNotifyEnterExitOnChild(true);

    // Wider horizontal padding so content sits comfortably inside the pill.
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, 12), 10));

    favicon_view_ = AddChildView(std::make_unique<views::ImageView>());
    favicon_view_->SetImageSize(gfx::Size(kFaviconSize, kFaviconSize));
    SetFaviconImage(favicon);

    title_label_ = AddChildView(std::make_unique<views::Label>(title));
    // The ACTIVE row says so in its type as well as its container: M3 sets a
    // selected navigation item in the emphasized weight. The fill alone did
    // the whole job before, which left the difference between "this tab" and
    // "the tab under the cursor" resting on a tone step -- the thing that
    // collapses first on the dark scheme.
    // body-small, not body-medium. At 14px the titles filled the row edge to
    // edge and the list read as a wall of text; the segments need air inside
    // them more than the titles need size.
    // As the settings rail's rows: the title in the label weight, the site
    // under it in the regular weight.
    title_label_->SetFontList(zephyrus::m3::Font(
        zephyrus::m3::Type::kLabelMedium, /*emphasized=*/true));
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
    // The same control as the horizontal strip's: bars that move while it
    // plays, the struck speaker when muted.
    audio_button_ = AddChildView(std::make_unique<ZephyrusAudioButton>(
        base::BindRepeating(
            [](base::RepeatingClosure cb, const ui::Event&) { cb.Run(); },
            std::move(mute_callback)),
        /*diameter=*/18));
    audio_button_->SetInk(is_active_ ? colors_.active_fg : colors_.foreground);
    audio_button_->SetAudio(audible, muted);
    // Shown while playing, and while muted so the user can undo it.
    audio_button_->SetVisible(audio_button_->wanted());

    close_callback_ = close_callback;
    close_button_ = AddChildView(std::make_unique<ZephyrusTabCloseButton>(
        base::BindRepeating(
            [](base::RepeatingClosure cb, const ui::Event&) { cb.Run(); },
            std::move(close_callback))));
    // Out of the tab order: focus moves row to row, and Delete on a focused
    // row closes it (see OnKeyPressed).
    close_button_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    close_button_->SetRowInk(is_active_ ? colors_.active_fg
                                       : colors_.foreground);
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
    UpdateCloseVisibility();
    UpdateBackground();
  }

  // Keyboard parity. The close control appeared only under a hovering POINTER,
  // so a keyboard user could reach a tab but never close it from the list.
  // A focused row now shows it, and Delete closes the row's tab; the close
  // button itself stays out of the tab order so focus moves row to row.
  void OnFocus() override {
    views::Button::OnFocus();
    UpdateCloseVisibility();
  }
  void OnBlur() override {
    views::Button::OnBlur();
    UpdateCloseVisibility();
  }
  bool OnKeyPressed(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_DELETE && close_callback_) {
      close_callback_.Run();
      return true;
    }
    return views::Button::OnKeyPressed(event);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(0, kRowHeight);
  }

  // Which end of its run this row sits at, so the group rounds only on the
  // outside. Set by the sidebar after the list is built -- a row cannot know
  // it, because headers and split cards break the runs.
  void SetSegmentPosition(bool first, bool last) {
    if (first == first_in_run_ && last == last_in_run_) {
      return;
    }
    first_in_run_ = first;
    last_in_run_ = last;
    SchedulePaint();
  }

  void Layout(PassKey key) override {
    LayoutSuperclass<views::Button>(this);
    // Elision is only known once the label has been given its width, which
    // has just happened.
    SetTooltipText(title_label_ && title_label_->IsDisplayTextTruncated()
                       ? full_title_
                       : std::u16string());
  }

  int model_index() const { return model_index_; }
  // Where inside the row the user grabbed it, so the proxy can sit under the
  // cursor at the same spot rather than jumping to its own corner.
  const gfx::Point& press_point() const { return press_point_; }

  void SetDragCallbacks(
      base::RepeatingCallback<void(views::View*, int)> on_drag,
      base::RepeatingClosure on_drag_end,
      base::RepeatingClosure on_drag_cancel) {
    on_drag_ = std::move(on_drag);
    on_drag_end_ = std::move(on_drag_end);
    on_drag_cancel_ = std::move(on_drag_cancel);
  }

  // views::Button:
  bool OnMousePressed(const ui::MouseEvent& event) override {
    press_point_ = event.location();
    press_y_ = event.y();
    dragging_ = false;
    return views::Button::OnMousePressed(event);
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    // A threshold, so a click with a shaky hand is still a click. Without it
    // every activation would risk being read as a one-pixel reorder.
    constexpr int kDragThreshold = 5;
    if (!dragging_ && std::abs(event.y() - press_y_) < kDragThreshold) {
      return views::Button::OnMouseDragged(event);
    }
    if (!dragging_) {
      dragging_ = true;
      // Its own layer, so it can be drawn ABOVE its siblings while it travels.
      // Without one it is just another child painted in tree order, and it
      // would slide underneath the rows it passes.
      SetPaintToLayer();
      layer()->SetFillsBoundsOpaquely(false);
      lift_started_ = base::TimeTicks::Now();
      // Hold the mouse explicitly for the rest of the gesture.
      //
      // This drag deliberately leaves the panel and travels over the web
      // contents, and the moment it does the renderer's view takes the mouse.
      // Drag events stop, the release never arrives, and the gesture simply
      // never ends: the drop indicator stays up and no split is ever created.
      // Capture is what keeps the events coming back here instead.
      if (GetWidget()) {
        GetWidget()->SetCapture(this);
      }
    }

    gfx::Point point = event.location();
    views::View::ConvertPointToTarget(this, parent(), &point);

    // Reorder FIRST -- that runs layout and moves this view's slot -- then
    // measure the offset against the new slot. The other order chases a
    // position that is already stale and the row lags a frame behind the
    // cursor on every reorder.
    if (on_drag_) {
      on_drag_.Run(this, point.y());
    }
    UpdateLift(point.y() - press_y_);
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (!dragging_) {
      views::Button::OnMouseReleased(event);
      return;
    }
    dragging_ = false;
    // Deliberately NOT forwarded to Button: releasing after a drag must not
    // also activate the tab. Dropping a tab into place and having the browser
    // navigate to it is the kind of surprise that makes a feature feel broken.
    SetState(views::Button::STATE_NORMAL);
    split_affordance_ = false;
    affordance_timer_.Stop();
    affordance_changed_ = base::TimeTicks();
    affordance_from_ = 0.f;
    if (layer()) {
      layer()->SetOpacity(1.f);
    }
    SettleIntoPlace();
    if (GetWidget()) {
      GetWidget()->ReleaseCapture();
    }
    if (on_drag_end_) {
      on_drag_end_.Run();
    }
  }

  // Capture can still be taken -- an alt-tab, a system dialog, a window
  // deactivation. Treated as a CANCEL rather than a drop: losing the mouse is
  // not the user letting go, and committing a split because Windows moved the
  // focus somewhere would be the browser acting on something nobody asked for.
  void OnMouseCaptureLost() override {
    if (!dragging_) {
      views::Button::OnMouseCaptureLost();
      return;
    }
    dragging_ = false;
    split_affordance_ = false;
    affordance_timer_.Stop();
    affordance_changed_ = base::TimeTicks();
    affordance_from_ = 0.f;
    if (layer()) {
      layer()->SetOpacity(1.f);
    }
    SetState(views::Button::STATE_NORMAL);
    SettleIntoPlace();
    if (on_drag_cancel_) {
      on_drag_cancel_.Run();
    }
  }

  // Lifts the row toward the cursor. `desired_top` is where its top edge wants
  // to be, in the parent's coordinates.
  // Signals that releasing here would split rather than reorder. The row grows
  // further and goes translucent -- it has left the list it belongs to, and it
  // should stop looking like a list item.
  void SetSplitAffordance(bool on) {
    if (on == split_affordance_) {
      return;
    }
    // Capture where the transition is starting from, so crossing back and
    // forth across the boundary retargets from the current value instead of
    // restarting from the other end -- the same reason transitions beat
    // keyframes for anything a user can trigger repeatedly.
    affordance_from_ = CurrentAffordance();
    split_affordance_ = on;
    affordance_changed_ = base::TimeTicks::Now();

    // Driven by a timer, not by mouse movement. The transition used to be
    // computed only when the cursor moved, so stopping dead on the boundary --
    // which is exactly what someone does while deciding whether to drop --
    // froze it halfway. A frozen transition looks like a bug, and this is the
    // moment the user is looking hardest.
    affordance_timer_.Start(
        FROM_HERE, base::Milliseconds(16), this,
        &ZephyrusTabRow::OnAffordanceTick);
  }

  void UpdateLift(int desired_top) {
    if (!layer()) {
      return;
    }
    // The row itself no longer travels -- the proxy does. What is left here is
    // a dimmed placeholder sitting in the slot the drop will land in, which is
    // more useful than an empty gap: it says "this is the tab you are moving,
    // and this is where it goes".
    gfx::Transform transform;

    // The scale-up, by contrast, is a state change and gets eased -- 140ms,
    // which is the press-feedback range. Interpolated by hand rather than
    // handed to the animator, because the animator would own the whole
    // transform and drag the translation into the easing with it.
    const double elapsed = (base::TimeTicks::Now() - lift_started_).InSecondsF();
    const double t = std::clamp(elapsed / 0.14, 0.0, 1.0);
    // EXACT curve, not TweenFor: this value is computed here rather than
    // handed to the compositor, so the real bezier is available.
    const float lift = static_cast<float>(
        zephyrus::m3::Curve(zephyrus::m3::Spring::kFastSpatial).Solve(t));

    // Eased between the two states rather than snapped. This is a state change
    // -- "releasing here does something else" -- and it was arriving in a
    // single frame, which reads as a glitch beside a row that is otherwise
    // moving smoothly.
    const float affordance = CurrentAffordance();
    // Shrinks very slightly as it is picked up, which reads as the content
    // having been taken out of it.
    const float scale = 1.f - 0.02f * lift;
    layer()->SetOpacity(0.35f - 0.1f * affordance);
    last_desired_top_ = desired_top;

    const float cx = width() / 2.f;
    const float cy = height() / 2.f;
    transform.Translate(cx, cy);
    transform.Scale(scale, scale);
    transform.Translate(-cx, -cy);
    layer()->SetTransform(transform);
  }

  // Drops it into the slot the gap has been holding open.
  void SettleIntoPlace() {
    if (!layer()) {
      return;
    }
    // ease-out and short: the decision was the drag, and the release is the
    // system confirming it. Slow here would feel like the row was reluctant.
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    settings.SetTransitionDuration(base::Milliseconds(160));
    // The row SETTLES back into place -- spatial, so it takes the spatial
    // tween. Duration stays short for the reason above it.
    settings.SetTweenType(
        zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
    layer()->SetTransform(gfx::Transform());
    // Back to solid: the tab has returned to the list.
    layer()->SetOpacity(1.f);
  }

  // Which tinted mark stands in for a missing favicon (GetTabFallbackIcon).
  void SetFallbackIcon(const gfx::VectorIcon& icon) {
    if (&*fallback_icon_ != &icon) {
      fallback_icon_ = raw_ref(icon);
      ApplyFallbackIconTint();
    }
  }

  // Updates the row's favicon/title/active state in place (without recreating
  // the view), preserving hover state.
  void UpdateContent(const gfx::ImageSkia& favicon,
                     const std::u16string& title,
                     bool is_active) {
    SetFaviconImage(favicon);
    title_label_->SetText(title);
    const std::u16string accessible_title = title.empty() ? u"Tab" : title;
    full_title_ = accessible_title;
    GetViewAccessibility().SetName(accessible_title);
    // The tooltip is set in Layout(), and only when the title is actually cut
    // off. Every row used to carry one, so pointing at a row whose title was
    // fully readable still popped a box repeating it over the row below.
    InvalidateLayout();
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
    audio_button_->SetAudio(audible, muted);
    audio_button_->SetVisible(audio_button_->wanted());
  }

 private:
  void SetFlexForView(views::View* view, int flex) {
    static_cast<views::BoxLayout*>(GetLayoutManager())
        ->SetFlexForView(view, flex);
  }

  void UpdateCloseVisibility() {
    if (!close_button_) {
      return;
    }
    close_button_->SetVisible(GetState() == views::Button::STATE_HOVERED ||
                              GetState() == views::Button::STATE_PRESSED ||
                              HasFocus());
  }

  void SetFaviconImage(const gfx::ImageSkia& favicon) {
    showing_fallback_icon_ = favicon.isNull();
    if (!showing_fallback_icon_) {
      // A real favicon is an IMAGE and is never recoloured -- sites keep their
      // own colour in the tab list.
      favicon_view_->SetImage(ui::ImageModel::FromImageSkia(favicon));
      return;
    }
    // The fallback globe is the only tinted mark here, so it is the only one
    // that can vanish into an inverted row. Drawn through the same helper the
    // label and close glyph use.
    ApplyFallbackIconTint();
  }

  void ApplyFallbackIconTint() {
    if (!showing_fallback_icon_ || !favicon_view_) {
      return;
    }
    favicon_view_->SetImage(ui::ImageModel::FromVectorIcon(
        *fallback_icon_,
        is_active_ ? colors_.active_fg : colors_.foreground, kFaviconSize));
  }

  // A flat row.
  //
  // The sticker treatment this replaces -- hard offset shadow plus a 2px
  // outline -- belonged to the previous language. Under Nothing there is
  // almost no elevation to express: separation is a hairline and a flat
  // surface step, and a shadow anywhere in the tab list would be the loudest
  // thing on screen.
  //
  // The ACTIVE row is an M3 navigation-drawer item: a full pill on
  // secondary-container, with onSecondaryContainer ink. That container is the
  // whole "this one" signal -- it replaced an inversion, and then an accent bar
  // that had been standing in for the inversion. Everything drawn ON the row
  // still has to take its ink from colors_.active_fg, because the row's ground
  // is no longer the panel's.
  void OnPaintBackground(gfx::Canvas* canvas) override {
    // Hover and press are DIFFERENT states in M3 -- 8% and 10% layers -- and
    // painting one fill for both meant pressing a row acknowledged nothing.
    // The active row gets its press layer over its own container, because its
    // ground is the container and not the panel.
    const views::Button::ButtonState state = GetState();
    const bool pressed = state == views::Button::STATE_PRESSED;
    const bool hovered = state == views::Button::STATE_HOVERED;
    SkColor fill;
    if (is_active_) {
      fill = pressed ? colors_.active_pressed_bg : colors_.active_bg;
    } else if (pressed) {
      fill = colors_.pressed_bg;
    } else if (hovered) {
      fill = colors_.hover_bg;
    } else {
      fill = colors_.idle_bg;
    }
    if (!colors_.contained) {
      // No run to be a segment of: a bare row at rest, and any fill a whole
      // pill. idle_bg is transparent here.
      if (SkColorGetA(fill) != SK_AlphaTRANSPARENT) {
        PaintSegment(canvas, GetLocalBounds(), fill, true, true);
      }
      return;
    }
    PaintSegment(canvas, GetLocalBounds(), fill, first_in_run_, last_in_run_);
  }

  void UpdateBackground() {
    // Painting happens in OnPaintBackground; this only invalidates. Anything
    // drawn ON the row has to flip with it or it disappears into an inverted
    // fill -- the title and the close glyph below.
    //
    // That includes the fallback globe shown for sites with no favicon: it is
    // the only TINTED mark in the row, so it was invisible on the active row
    // until it was refreshed here too. Real favicons are images and are never
    // recoloured -- whether they should be is a separate, still-open question.
    const SkColor on_row = is_active_ ? colors_.active_fg : colors_.foreground;
    if (title_label_) {
      title_label_->SetEnabledColor(on_row);
    }
    if (close_button_) {
      close_button_->SetRowInk(on_row);
    }
    if (audio_button_) {
      audio_button_->SetInk(on_row);
    }
    ApplyFallbackIconTint();
    SchedulePaint();
  }


  bool is_active_;
  int model_index_;
  raw_ref<const gfx::VectorIcon> fallback_icon_{vector_icons::kGlobeIcon};
  std::u16string full_title_;
  bool first_in_run_ = true;
  bool last_in_run_ = true;
  int press_y_ = 0;
  gfx::Point press_point_;
  bool dragging_ = false;
  bool showing_fallback_icon_ = false;
  base::TimeTicks lift_started_;
  bool split_affordance_ = false;
  base::TimeTicks affordance_changed_;
  float affordance_from_ = 0.f;
  int last_desired_top_ = 0;
  base::RepeatingTimer affordance_timer_;

  // 0 = in the list, 1 = over the page. Eased, and read from the clock rather
  // than accumulated, so it is correct on any frame it happens to be asked on.
  float CurrentAffordance() const {
    if (affordance_changed_.is_null()) {
      return split_affordance_ ? 1.f : 0.f;
    }
    const double t = std::clamp(
        (base::TimeTicks::Now() - affordance_changed_).InSecondsF() / 0.16, 0.0,
        1.0);
    // Exact curve for the same reason as the lift above.
    const float eased = static_cast<float>(
        zephyrus::m3::Curve(zephyrus::m3::Spring::kFastSpatial).Solve(t));
    const float to = split_affordance_ ? 1.f : 0.f;
    return affordance_from_ + (to - affordance_from_) * eased;
  }

  void OnAffordanceTick() {
    UpdateLift(last_desired_top_);
    if ((base::TimeTicks::Now() - affordance_changed_).InSecondsF() >= 0.16) {
      affordance_timer_.Stop();
    }
  }
  base::RepeatingCallback<void(views::View*, int)> on_drag_;
  base::RepeatingClosure on_drag_end_;
  base::RepeatingClosure on_drag_cancel_;
  ZephyrusRowColors colors_;
  raw_ptr<views::ImageView> favicon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<ZephyrusAudioButton> audio_button_ = nullptr;
  raw_ptr<ZephyrusTabCloseButton> close_button_ = nullptr;
  base::RepeatingClosure close_callback_;
};

BEGIN_METADATA(ZephyrusTabRow)
END_METADATA

// Every tab row under `root`, however deep: rows live in the list itself, in a
// split card, or in a set's body.
void CollectTabRows(views::View* root, std::vector<ZephyrusTabRow*>* out) {
  for (views::View* child : root->children()) {
    if (auto* row = views::AsViewClass<ZephyrusTabRow>(child)) {
      out->push_back(row);
    } else {
      CollectTabRows(child, out);
    }
  }
}

// Two tabs in a split, drawn as one object.
//
// The point of the card is that a split is ONE thing the user arranged, not
// two tabs that happen to be adjacent. Drawing them as separate rows with a
// shared tint would say "related"; enclosing them in a single surface with a
// break control between says "joined, and here is how you undo it" -- which is
// the only thing about a split a user needs from a list.
class ZephyrusSplitCard : public views::View {
  METADATA_HEADER(ZephyrusSplitCard, views::View)

 public:
  ZephyrusSplitCard(SkColor foreground, base::RepeatingClosure break_callback)
      : foreground_(foreground) {
    // The SAME inset on every side, so the rows' ends nest in the card's
    // corners (Rule 2) -- it was 5 vertically and 4 across.
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(kSplitInset),
        0));
    // A raised surface rather than an outline: the rows inside already carry
    // their own hover and selection fills, and a border around them would be a
    // third rectangle competing with those two. Filled in OnThemeChanged.
    break_callback_ = std::move(break_callback);
  }

  // Rule 2: the rows inside end in kSegmentEndRadius, kSplitInset from this
  // card's edge, so the card is their radius PLUS that inset. It was 12 --
  // SMALLER than the 16 of the rows it holds, so their corners bulged past the
  // card's. And an M3 role rather than an alpha of the ink.
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateRoundedRectBackground(
        zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest),
        kSegmentEndRadius + kSplitInset));
  }

  // Called after both rows have been added, so the control sits between them.
  void AddBreakControl(SkColor foreground) {
    auto* strip = AddChildViewAt(std::make_unique<views::View>(), 1);
    seam_strip_ = strip;
    auto* layout = strip->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(3, 0), 0));
    layout->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kCenter);

    auto* button = strip->AddChildView(std::make_unique<views::ImageButton>(
        base::BindRepeating([](base::RepeatingClosure cb,
                               const ui::Event&) { cb.Run(); },
                            break_callback_)));
    button->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(views::kCloseIcon,
                                       SkColorSetA(foreground, 0xAA), 12));
    button->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    button->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    button->GetViewAccessibility().SetName(u"Split view: separate these tabs");
    button->SetTooltipText(u"Separate these tabs");
    views::InstallCircleHighlightPathGenerator(button);
  }

  // A hairline across the card, with the break control sitting on it.
  //
  // The control used to float in a gap between the two rows, which said
  // nothing -- a gap is just absence. A line running between them says the
  // tabs are joined, and a control sitting ON that line says this is where it
  // comes apart. The metaphor explains the feature; no tooltip has to.
  //
  // Painted by the card rather than by a child view: a nested View would need
  // its own metadata, and this is one line.
  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    if (!seam_strip_) {
      return;
    }
    const float y = seam_strip_->bounds().CenterPoint().y();
    cc::PaintFlags flags;
    flags.setColor(SkColorSetA(foreground_, 0x33));
    flags.setStrokeWidth(1.f);
    // Stops short of the control, so the line does not run underneath it.
    constexpr float kGap = 13.f;
    canvas->DrawLine(gfx::PointF(8.f, y), gfx::PointF(width() / 2.f - kGap, y),
                     flags);
    canvas->DrawLine(gfx::PointF(width() / 2.f + kGap, y),
                     gfx::PointF(width() - 8.f, y), flags);
  }

 private:
  raw_ptr<views::View> seam_strip_ = nullptr;
  SkColor foreground_;
  base::RepeatingClosure break_callback_;
};

BEGIN_METADATA(ZephyrusSplitCard)
END_METADATA

}  // namespace

// The omnibox while it is being edited in compact mode.
//
// In compact mode the address bar lives in the sidebar, and a 180-300dp column
// is no place to read or type an address: the text elides after a dozen
// characters and the suggestions drop down at the same cramped width. So
// focusing it lifts the SAME LocationBarView out of the panel into this
// floating capsule, which grows from the pill's own spot out over the page.
// The suggestions follow it -- the popup is positioned from the location bar's
// bounds -- and leaving the omnibox shrinks it back into the panel.
//
// A child of BrowserView, like the drag proxy: the sidebar clips its children
// to its own bounds, and would not route a click outside them either.
class ZephyrusOmniboxOverlay : public views::View,
                               public views::MaskedTargeterDelegate,
                               // Frame-aligned: ticks on the compositor's frames,
                               // not a ~40fps timer (see the omnibox's own
                               // grow animation in toolbar_view.cc).
                               public views::AnimationDelegateViews {
  METADATA_HEADER(ZephyrusOmniboxOverlay, views::View)

 public:
  // Room around the capsule for its shadow. Painted, never hit: see
  // GetHitTestMask.
  static constexpr int kShadowMargin = 12;
  // The address bar's own corner radius (LocationBarView's
  // kZephyrusOmniboxRadius), not half its height. The open overlay was a full
  // capsule around a bar that is a rounded rectangle, so focusing it changed
  // its shape; now it only grows. 8 is whole device pixels at 1.5x and 2x.
  static constexpr int kCornerRadius = zephyrus::kRadiusSmall;

  ZephyrusOmniboxOverlay(base::RepeatingClosure on_escape,
                         base::RepeatingClosure on_collapsed)
      : views::AnimationDelegateViews(this),
        on_escape_(std::move(on_escape)),
        on_collapsed_(std::move(on_collapsed)) {
    // Its own layer, to stack above the sidebar's and the page's. This outer
    // view paints the shadow and the fill; it cannot clip, or the shadow would
    // go with it. (views::ViewShadow cannot draw one here at all: ui::Shadow
    // caps its elevation at (shorter side - 2 * radius) / 4, which is zero for
    // a capsule.)
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
    // The capsule's CLIP is this inner layer. The address bar inside it is
    // laid out once per direction, at the width it is heading for, and the
    // growing capsule uncovers it. Animating the address bar's own width
    // instead re-ran its layout, rebuilt its background and moved the
    // suggestions popup's native window on every frame -- the expensive part
    // of the whole motion.
    clip_ = AddChildView(std::make_unique<views::View>());
    clip_->SetPaintToLayer();
    clip_->layer()->SetFillsBoundsOpaquely(false);
    clip_->layer()->SetMasksToBounds(true);
    animation_.SetTweenType(
        zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
    // Escape ends the edit -- but only an Escape the omnibox itself declined.
    // It sees the key first, and uses it to revert typed text and close the
    // suggestions; accelerators get it once there is nothing left to undo.
    AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
  }
  ZephyrusOmniboxOverlay(const ZephyrusOmniboxOverlay&) = delete;
  ZephyrusOmniboxOverlay& operator=(const ZephyrusOmniboxOverlay&) = delete;

  // The address bar is hosted inside the clip, not in this view directly.
  void SetContent(views::View* content) { clip_->AddChildView(content); }
  std::vector<views::View*> TakeContent() {
    std::vector<views::View*> content(clip_->children().begin(),
                                      clip_->children().end());
    return content;
  }

  // `pill` is where the address bar sits in the panel, in the parent's
  // coordinates; `editing_width` is how wide it grows.
  void SetGeometry(const gfx::Rect& pill, int editing_width) {
    clip_->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kCornerRadius));
    pill_ = pill;
    editing_width_ = std::max(editing_width, pill.width());
    LayoutContent();
    UpdateBounds();
  }
  // Growing is the entrance, so it takes a little longer than shrinking back:
  // M3 gives exits the shorter duration, since nothing needs to be read on
  // the way out.
  void Expand() {
    animation_.SetSlideDuration(RichDuration(kExpandDuration));
    animation_.Show();
    LayoutContent();
    UpdateBounds();
  }
  void Collapse() {
    animation_.SetSlideDuration(RichDuration(kCollapseDuration));
    animation_.Hide();
    LayoutContent();
    UpdateBounds();
  }
  bool IsExpanding() const { return animation_.IsShowing(); }

  // views::View:
  void Layout(PassKey key) override {
    clip_->SetBoundsRect(GetCapsule());
    LayoutContent();
  }
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    on_escape_.Run();
    return true;
  }
  // Only while it is open. Shrinking back, the edit is already over, and an
  // Escape then is the page's (stop loading), not this view's to swallow.
  bool CanHandleAccelerators() const override {
    return animation_.IsShowing() && views::View::CanHandleAccelerators();
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    // The capsule, raised off the page. Opaque, so the page does not show
    // through the pill; the address bar paints its own outline over it.
    const gfx::RectF capsule(GetCapsule());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(
        zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHigh));
    flags.setLooper(gfx::CreateShadowDrawLooper(
        gfx::ShadowValue::MakeMdShadowValues(kElevation)));
    canvas->DrawRoundRect(capsule, kCornerRadius, flags);
  }

  // views::MaskedTargeterDelegate: the capsule only. The shadow margin around
  // it is the page's, and must not swallow a click meant for it.
  bool GetHitTestMask(SkPath* mask) const override {
    const gfx::Rect capsule = GetCapsule();
    *mask = SkPath::RRect(SkRRect::MakeRectXY(gfx::RectToSkRect(capsule),
                                              kCornerRadius, kCornerRadius));
    return true;
  }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override {
    UpdateBounds();
  }
  void AnimationEnded(const gfx::Animation* animation) override {
    UpdateBounds();
    if (!animation_.IsShowing()) {
      // Posted: the handler hands the address bar back and deletes this view,
      // which owns the animation that is calling us.
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                               on_collapsed_);
    }
  }

 private:
  // M3 level 3, the elevation of a search bar that has been opened.
  static constexpr int kElevation = 6;
  // M3 short4 in, short3 out. The spatial curve front-loads the motion, so
  // at the old 350ms the last third was an almost-still tail.
  static constexpr base::TimeDelta kExpandDuration = base::Milliseconds(200);
  static constexpr base::TimeDelta kCollapseDuration = base::Milliseconds(150);

  static base::TimeDelta RichDuration(base::TimeDelta duration) {
    return gfx::Animation::ShouldRenderRichAnimation() ? duration
                                                       : base::TimeDelta();
  }

  // The address bar at the width it is heading for: the editing width while
  // growing (so the text and suggestions are placed once, not per frame), the
  // pill's while shrinking (so its text re-centres once, in the width it will
  // have back in the panel, instead of sliding out of the shrinking clip).
  void LayoutContent() {
    const int width = animation_.IsShowing() ? editing_width_ : pill_.width();
    for (views::View* child : clip_->children()) {
      child->SetBounds(0, 0, width, pill_.height());
    }
  }

  gfx::Rect GetCapsule() const {
    gfx::Rect capsule = GetLocalBounds();
    capsule.Inset(kShadowMargin);
    return capsule;
  }

  void UpdateBounds() {
    const int width = gfx::Tween::IntValueBetween(
        animation_.GetCurrentValue(), pill_.width(), editing_width_);
    gfx::Rect bounds(pill_.x(), pill_.y(), width, pill_.height());
    bounds.Outset(kShadowMargin);
    SetBoundsRect(bounds);
  }

  base::RepeatingClosure on_escape_;
  base::RepeatingClosure on_collapsed_;
  raw_ptr<views::View> clip_ = nullptr;
  gfx::SlideAnimation animation_{this};
  gfx::Rect pill_;
  int editing_width_ = 0;
};

BEGIN_METADATA(ZephyrusOmniboxOverlay)
END_METADATA

namespace {

// The tab itself, once it has left the panel.
//
// WHY A SEPARATE VIEW AND NOT THE ROW
// -----------------------------------
// The row is a child of the sidebar's list, so it is clipped to the panel and
// laid out in a vertical stack. It can be nudged up and down inside that column
// and no further -- which is why the earlier version could never actually
// travel to the content card: the thing being dragged was structurally
// incapable of leaving the container it lived in.
//
// This is a lightweight stand-in parented into BrowserView, so it can be
// anywhere in the window. The real row stays where it was, dimmed, holding the
// slot the drop would land in.
class ZephyrusDragProxy : public views::View {
  METADATA_HEADER(ZephyrusDragProxy, views::View)

 public:
  ZephyrusDragProxy(const gfx::ImageSkia& favicon,
                    const std::u16string& title,
                    SkColor foreground,
                    SkColor panel) {
    // Never takes input. It sits under the cursor, over everything, for the
    // whole gesture -- the one shape of view most able to swallow a click it
    // should not have.
    SetCanProcessEventsWithinSubtree(false);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);

    SetBackground(views::CreateRoundedRectBackground(
        SkColorSetA(panel, 0xF2), zephyrus::kRadiusCard));
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(6, 10), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    panel_ = panel;
    icon_ = AddChildView(std::make_unique<views::ImageView>());
    if (!favicon.isNull()) {
      icon_->SetImage(ui::ImageModel::FromImageSkia(favicon));
    }
    icon_->SetImageSize(gfx::Size(16, 16));

    auto* label = AddChildView(std::make_unique<views::Label>(title));
    label->SetEnabledColor(foreground);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetSubpixelRenderingEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    layout->SetFlexForView(label, 1);
  }

  // Dressed as a set's header: the set's own icon, and the capsule's shape.
  void UseSetLook(std::unique_ptr<views::View> glyph, int height) {
    RemoveChildViewT(icon_.get());
    icon_ = nullptr;
    AddChildViewAt(std::move(glyph), 0);
    SetBackground(views::CreateRoundedRectBackground(SkColorSetA(panel_, 0xF2),
                                                     height / 2.0f));
    if (auto* box = static_cast<views::BoxLayout*>(GetLayoutManager())) {
      box->set_inside_border_insets(gfx::Insets::TLBR(0, 12, 0, 14));
    }
  }

 private:
  raw_ptr<views::ImageView> icon_ = nullptr;
  SkColor panel_ = SK_ColorBLACK;
};

BEGIN_METADATA(ZephyrusDragProxy)
END_METADATA

// "Drop here to split", drawn over the page while a tab is held above it.
//
// The affordance on the dragged row alone was not enough: it told the user
// something had changed, not what would happen. This shows the shape of the
// outcome -- the half the incoming tab will occupy -- which is the part that
// makes the gesture discoverable rather than something you have to be told
// about.
class ZephyrusSplitDropIndicator : public views::View {
  METADATA_HEADER(ZephyrusSplitDropIndicator, views::View)

 public:
  explicit ZephyrusSplitDropIndicator(SkColor accent) : accent_(accent) {
    // Cannot take input, at any depth. It sits over the whole page, and an
    // overlay that can receive events is one layout mistake away from eating
    // every click in the window.
    SetCanProcessEventsWithinSubtree(false);
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetOpacity(0.f);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    // The right half: where AddToNewSplit places the incoming tab relative to
    // the one already open.
    gfx::RectF target(width() / 2.f, 0, width() / 2.f,
                      static_cast<float>(height()));
    target.Inset(6.f);

    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(SkColorSetA(accent_, 0x1F));
    canvas->DrawRoundRect(target, zephyrus::kRadiusCard, fill);

    cc::PaintFlags stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(cc::PaintFlags::kStroke_Style);
    // The accent EARNS its place here: a drop target is a live, momentary
    // state, which is exactly what the one red is reserved for. It is also the
    // only place in the sidebar that uses it.
    stroke.setStrokeWidth(zephyrus::kHairline * 2.f);
    stroke.setColor(accent_);
    canvas->DrawRoundRect(target, zephyrus::kRadiusCard, stroke);
  }

 private:
  SkColor accent_;
};

BEGIN_METADATA(ZephyrusSplitDropIndicator)
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
  // Preserve alpha for the DWM desktop backdrop. OnPaintBackground uses the
  // themed fill below as a fallback when the native effect is unavailable.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  // A flush column, the same in every layout.
  SetBackground(views::CreateRoundedRectBackground(GetPanelColor(), 0));

  auto* box_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kClassicPadding),
      kRowSpacing));

  // Quick-actions card REMOVED.
  //
  // It held Open Private Workspace / Downloads / History. All three are still
  // reachable -- the settings popup rail lists Downloads and History, and
  // Private Workspace has Ctrl+Shift+N -- so the card was duplicating
  // navigation rather than providing it, at the cost of the top third of the
  // panel on every window.
  //
  // Nothing here needs the row ink any more either: section headers read
  // their own roles (see ZephyrusSectionHeader), and the favourite rows take
  // theirs in RebuildFavorites.

  // ---- Compact-mode chrome --------------------------------------------------
  // First child, so the address bar sits where the title bar used to be: at
  // the top of the window. Built on demand -- see RebuildCompactChrome().
  compact_chrome_ = AddChildView(std::make_unique<views::View>());
  compact_chrome_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  compact_chrome_->SetVisible(false);
  // Controls first, omnibox under them: the buttons are what the hand goes
  // for, and putting the address bar on top pushed them into the middle of
  // the panel where they read as a loose scatter of glyphs.
  // ONE run, not the bar's two.
  //
  // The bar splits navigation from the page actions because a centred omnibox
  // sits between them and they would otherwise read as one long bar. Stacked
  // in a 180dp panel there is no omnibox between them, and two runs of three
  // read as two unrelated controls -- plus they cost a second row of height in
  // the narrowest place in the browser. Six cells across one run is the same
  // information in half the space.
  compact_controls_row_ =
      compact_chrome_->AddChildView(std::make_unique<ZephyrusControlRow>());
  containers_pref_.Init(browser_view_->browser()->GetProfile()->GetPrefs());
  containers_pref_.Add(
      "zephyrus.appearance.m3_containers",
      base::BindRepeating(&ZephyrusSidebarView::OnContainersChanged,
                          base::Unretained(this)));
  if (auto* row =
          views::AsViewClass<ZephyrusControlRow>(compact_controls_row_.get())) {
    row->SetContained(ContainersOn());
  }
  compact_address_row_ =
      compact_chrome_->AddChildView(std::make_unique<views::View>());
  compact_address_row_->SetLayoutManager(
      std::make_unique<views::FillLayout>());
  compact_address_row_->SetVisible(false);
  // Parking space for the bar's controls that are currently hidden.
  compact_hidden_ =
      compact_chrome_->AddChildView(std::make_unique<views::View>());
  compact_hidden_->SetVisible(false);

  // ---- Favorites (bookmark bar entries) -------------------------------------
  // Kept so the heading can be hidden when there are no bookmarks, rather than
  // labelling an empty space.
  favorites_header_ =
      AddChildView(std::make_unique<ZephyrusSectionHeader>(u"Favorites"));
  favorites_container_ = AddChildView(std::make_unique<views::View>());
  favorites_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kRowSpacing));
  RebuildFavorites();

  // ---- Tabs ------------------------------------------------------------------
  // No static heading here: RebuildTabList emits "Pinned tabs" and "Tabs"
  // itself, so the pinned group can sit above the workspace's own tabs.
  // SCROLLABLE. The tab list had no ScrollView at all, so once the tabs ran
  // past the window height the overflow was simply unreachable -- no scrollbar,
  // no wheel response, the rows just stopped. With ~20 tabs open the bottom of
  // the list was inaccessible, which for a vertical tab strip is the whole
  // feature failing.
  //
  // The ScrollView takes the flex (so it still pushes the version label to the
  // foot of the panel); the container inside it keeps its own preferred height
  // and scrolls within.
  auto* tab_scroll = AddChildView(std::make_unique<views::ScrollView>());
  // ClipHeightTo is REQUIRED, not decoration. Without it a ScrollView takes its
  // preferred size from its contents, and the contents here are empty at
  // construction (RebuildTabList fills them later) -- so it resolved to zero
  // height and the whole tab list vanished. This is the same pairing the agent
  // panel's log uses, and for the same reason.
  tab_scroll->ClipHeightTo(0, kUnboundedScrollHeight);
  // Transparent: the sidebar already paints the panel behind this, and an
  // opaque scroll viewport would punch a square hole through the panel's
  // rounded corners.
  tab_scroll->SetBackgroundColor(std::nullopt);
  tab_scroll->SetDrawOverflowIndicator(false);
  // Vertical only. A horizontal bar here would appear whenever a long tab title
  // widened the contents, and the rows already elide.
  tab_scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  // OVERLAY scrollbar, not the default.
  //
  // Chromium's default ScrollBarViews draws an OPAQUE track the full height of
  // the viewport. In a rounded panel that reads as a dark column punched
  // through the sidebar, and it squares off the panel's own rounded corners
  // where the track runs past them.
  //
  // An overlay bar has no track, reserves no layout width, and fades its thumb
  // when idle -- which is both what M3 specifies for scrollbars and the only
  // variant that can sit inside a rounded surface without cutting it.
  tab_scroll->SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(
      views::ScrollBar::Orientation::kVertical));

  auto tab_list = std::make_unique<views::BoxLayoutView>();
  tab_list->SetOrientation(views::BoxLayout::Orientation::kVertical);
  tab_list->SetBetweenChildSpacing(kRowSpacing);
  // STRETCH, so rows fill the viewport width. Left to the default the rows size
  // to their own text and the list reads as ragged.
  tab_list->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  tab_list_container_ = tab_scroll->SetContents(std::move(tab_list));
  // The scroll area takes the remaining height, so it pushes the version label
  // to the very bottom of the panel.
  box_layout->SetFlexForView(tab_scroll, 1);


  // The foot bar, while the title bar is hidden: downloads on the left, the
  // workspace switcher taking the middle, and add-workspace on the right.
  //
  // The add button lives HERE rather than inside the switcher: the switcher
  // is as wide as its workspaces, and when it ran out of room the bar simply
  // cut off whatever came last -- which was that button. Out here it cannot
  // be cut off, and the switcher shrinks instead (ToolbarView::
  // SetZephyrusWorkspaceStripCompact).
  compact_workspaces_ = AddChildView(std::make_unique<views::View>());
  auto* foot_layout = compact_workspaces_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(8, 4, 0, 4), 4));
  foot_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  // Downloads opens the real downloads bubble -- the same popup the toolbar's
  // own button opens, through the controller that owns it. The button itself
  // cannot be borrowed: it lives inside the pinned-actions container, which
  // stays on the bar with the rest of the user's controls.
  downloads_button_ = compact_workspaces_->AddChildView(
      std::make_unique<ZephyrusFootIconButton>(
          base::BindRepeating(&ZephyrusSidebarView::ShowDownloads,
                              base::Unretained(this)),
          kDownloadToolbarButtonChromeRefreshOldIcon, u"Downloads"));
  compact_workspaces_->SetVisible(false);

  // Start tucked off-screen and transparent to events so it doesn't intercept
  // input over the web contents until revealed.
  //
  // Hidden by POSITION only. This used to also set opacity to 0, with the
  // reveal fading it back — two mechanisms for one state. Once the fade was
  // removed as part of unifying the animation, nothing restored the opacity
  // and the panel stayed invisible while still reserving its column, so the
  // unpainted window behind it showed through as a grey slab.
  layer()->SetTransform(TransformForReveal(0.0, GetSidebarWidth()));
  SetCanProcessEventsWithinSubtree(false);
  // The panel itself is a context-menu source, not just its tab rows: the
  // sidebar-level items (Pin sidebar) must be reachable by right-clicking
  // anywhere on it, including the empty space below the tab list.
  set_context_menu_controller(this);

  // Extend the watched zone left of the panel so the cursor sitting on the
  // edge hot-zone (left of the panel's margin) doesn't immediately re-tuck it.
  mouse_watcher_ = std::make_unique<views::MouseWatcher>(
      std::make_unique<views::MouseWatcherViewHost>(
          this, gfx::Insets::TLBR(0, 16, 0, 0)),
      this);

  // 50ms, not 100. This poll is what NOTICES the cursor at the edge, so its
  // interval is dead time in front of the animation: at 100ms the worst case
  // was ~380ms from arriving at the edge to the panel being open, and no
  // amount of tuning the slide duration would have shown that up. The check
  // itself is a cursor position and a bounds test.
  reveal_poll_timer_.Start(FROM_HERE, base::Milliseconds(50), this,
                           &ZephyrusSidebarView::OnRevealPoll);

  tab_strip_model_->AddObserver(this);
  RebuildTabList();
}

ZephyrusSidebarView::~ZephyrusSidebarView() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
  // Hand the address bar back to the panel before the overlay goes: the
  // overlay deletes its children, and the address bar is the toolbar's. The
  // sidebar is BrowserView's earlier child, so the overlay is still alive here.
  omnibox_hold_ = false;
  DropOmniboxOverlay();
}

void ZephyrusSidebarView::Layout(PassKey key) {
  LayoutSuperclass<views::View>(this);
  // The pill may have moved (a window resize, the panel resized): keep the
  // overlay on it.
  if (omnibox_overlay_) {
    const gfx::Rect pill = GetOmniboxPillRect();
    omnibox_overlay_->SetGeometry(pill, GetOmniboxEditingWidth(pill));
  }
}

void ZephyrusSidebarView::OnOmniboxFocusChanged() {
  // Posted: this is called from inside the omnibox's own focus change, and
  // expanding reparents the omnibox.
  if (omnibox_update_pending_) {
    return;
  }
  omnibox_update_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusSidebarView::UpdateOmniboxExpansion,
                                weak_factory_.GetWeakPtr()));
}

void ZephyrusSidebarView::UpdateOmniboxExpansion() {
  omnibox_update_pending_ = false;
  ToolbarView* toolbar = browser_view_ ? browser_view_->toolbar() : nullptr;
  LocationBarView* location_bar =
      toolbar ? toolbar->location_bar_view() : nullptr;
  if (!location_bar || !compact_address_row_) {
    return;
  }
  // The focus as it is NOW, not as the posted call found it: a blur and a
  // refocus can both land before this runs.
  const bool editing = IsCompactMode() && toolbar->IsZephyrusCompact() &&
                       location_bar->omnibox_view() &&
                       location_bar->omnibox_view()->HasFocus();
  VLOG(1) << "compact omnibox: editing=" << editing
          << " overlay=" << !!omnibox_overlay_;
  if (!editing) {
    if (omnibox_overlay_) {
      const gfx::Rect pill = GetOmniboxPillRect();
      omnibox_overlay_->SetGeometry(pill, GetOmniboxEditingWidth(pill));
      omnibox_overlay_->Collapse();
    }
    return;
  }
  const gfx::Rect pill = GetOmniboxPillRect();
  if (!omnibox_overlay_) {
    if (location_bar->parent() != compact_address_row_) {
      return;
    }
    // Hold the row's height, or everything below it jumps up into the space
    // the address bar leaves.
    compact_address_row_->SetPreferredSize(compact_address_row_->size());
    omnibox_overlay_ =
        browser_view_->AddChildView(std::make_unique<ZephyrusOmniboxOverlay>(
            base::BindRepeating(&ZephyrusSidebarView::EndOmniboxEdit,
                                weak_factory_.GetWeakPtr()),
            base::BindRepeating(&ZephyrusSidebarView::OnOmniboxOverlayCollapsed,
                                weak_factory_.GetWeakPtr())));
    omnibox_overlay_->SetContent(location_bar);
    // Views clears the focus of any view removed from its parent, even one
    // only moving within the widget (Widget::ViewHierarchyChanged), so it is
    // handed straight back. The omnibox keeps its text through the blur and
    // restores its own selection on refocus. The blur and refocus also post
    // an OnOmniboxFocusChanged, which finds the omnibox focused and leaves
    // the expansion alone.
    location_bar->omnibox_view()->RequestFocus();
    // Keep the panel out while the address is being edited, and bring it out
    // if the edit began from the keyboard (Ctrl+L) with the panel tucked:
    // otherwise the pointer moving down to a suggestion leaves the panel and
    // tucks the omnibox away mid-edit.
    ++reveal_holds_;
    omnibox_hold_ = true;
    Reveal();
  }
  omnibox_overlay_->SetGeometry(pill, GetOmniboxEditingWidth(pill));
  omnibox_overlay_->Expand();
}

gfx::Rect ZephyrusSidebarView::GetOmniboxPillRect() const {
  // Summed by hand rather than converted: a conversion applies the panel's
  // slide transform, so while the panel is still sliding in (an edit begun
  // from Ctrl+L) the pill would be placed off the window's edge. This is where
  // it sits once the panel is out.
  gfx::Rect rect = compact_address_row_->GetLocalBounds();
  for (const views::View* view = compact_address_row_;
       view && view != browser_view_; view = view->parent()) {
    rect.Offset(view->GetMirroredPosition().OffsetFromOrigin());
  }
  return rect;
}

int ZephyrusSidebarView::GetOmniboxEditingWidth(const gfx::Rect& pill) const {
  // Wide enough to read a whole address and its suggestions, and never past
  // the window's right edge. 640 read as a second toolbar laid across the
  // page; 520 still fits a long address with its suggestions.
  constexpr int kEditingWidth = 520;
  constexpr int kWindowMargin = 16;
  const int room = browser_view_->width() - pill.x() - kWindowMargin -
                   ZephyrusOmniboxOverlay::kShadowMargin;
  return std::max(pill.width(), std::min(kEditingWidth, room));
}

void ZephyrusSidebarView::EndOmniboxEdit() {
  // Focus goes back to the page, as it does when you leave the title bar's
  // omnibox; the blur is what collapses the overlay.
  if (content::WebContents* contents =
          tab_strip_model_ ? tab_strip_model_->GetActiveWebContents()
                           : nullptr) {
    contents->Focus();
  }
}

void ZephyrusSidebarView::OnOmniboxOverlayCollapsed() {
  // Refocused while this was posted: it is growing again, leave it be.
  if (!omnibox_overlay_ || omnibox_overlay_->IsExpanding()) {
    return;
  }
  DropOmniboxOverlay();
}

void ZephyrusSidebarView::DropOmniboxOverlay() {
  if (!omnibox_overlay_) {
    return;
  }
  ZephyrusOmniboxOverlay* overlay = omnibox_overlay_;
  omnibox_overlay_ = nullptr;
  // Moved, never deleted: these are the toolbar's views.
  for (views::View* child : overlay->TakeContent()) {
    compact_address_row_->AddChildView(child);
  }
  compact_address_row_->SetPreferredSize(std::nullopt);
  if (browser_view_) {
    browser_view_->RemoveChildViewT(overlay);
  }
  if (omnibox_hold_) {
    omnibox_hold_ = false;
    ReleaseRevealHold();
  }
}

void ZephyrusSidebarView::OnPaintBackground(gfx::Canvas* canvas) {
  // The desktop blur belongs to DWM, outside Chromium's compositor. A Views
  // backdrop filter here can only sample the window's own layers.
  //
  // Floating (not attached) the card is over the PAGE, not over window ground
  // the desktop shows through, so it must be opaque there: transparent, the
  // page's text would run straight through the tab list.
  if (!zephyrus::HasWindowBackdrop(GetWidget()) ||
      !browser_view_->IsZephyrusSidebarAttached()) {
    views::View::OnPaintBackground(canvas);
  }
  // No edge line: docked the page's own margin is the seam, and covering the
  // page the column's low shadow and the card's curved corners say where it
  // stops.
}

void ZephyrusSidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  // The panel's own fill too: it was set once, in the constructor, before the
  // view had a colour provider, and never refreshed on a theme change.
  SetBackground(
      views::CreateRoundedRectBackground(GetPanelColor(), PanelRadius()));
  RebuildFavorites();
  RebuildTabList();
}

void ZephyrusSidebarView::MouseMovedOutOfHost() {
  if (pinned_) {
    return;
  }
  TuckAway();
}

void ZephyrusSidebarView::Reveal() {
  if (revealed_ || hidden_by_layout_) {
    return;
  }
  revealed_ = true;
  reveal_poll_timer_.Stop();
  // CLIPPING layout: a hover reveal slides the column OVER the page and the page
  // keeps its size and its layout; only a pinned panel takes the column beside
  // it. (Classic's column always takes the page's column.) Resizing the page on
  // every hover was also the most expensive thing a hover could do.
  browser_view_->SetZephyrusSidebarAttached(pinned_ || !clipping_);
  // Zero duration under reduced motion, matching the layer slide below, which
  // has always honoured it. Without this the panel would snap into place while
  // the page's edge kept easing open behind it — reduced motion half-applied
  // looks more broken than not applying it at all.
  // One tween for the whole component, now chosen by ROLE rather than by
  // matching whatever the neighbouring animation happened to use. The panel
  // slides, so it is spatial.
  reveal_animation_.SetTweenType(
      zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
  reveal_animation_.SetSlideDuration(
      gfx::Animation::ShouldRenderRichAnimation() ? kSlideInDuration
                                                  : base::TimeDelta());
  reveal_animation_.Show();
  SetCanProcessEventsWithinSubtree(true);
  // Refresh rows (titles/favicons/favorites) and clear any stale hover state.
  RebuildTabList();
  RebuildFavorites();

  if (GetWidget()) {
    mouse_watcher_->Start(GetWidget()->GetNativeWindow());
  }
}

void ZephyrusSidebarView::TogglePinned() {
  if (hidden_by_layout_) {
    return;
  }
  pinned_ = !pinned_;
  // Pinned, the column docks beside the page; unpinned in the clipping layout
  // it covers the page.
  if (revealed_) {
    browser_view_->SetZephyrusSidebarAttached(pinned_ || !clipping_);
    UpdateFloatShadow();
  }
  if (pinned_) {
    reveal_poll_timer_.Stop();
    // Pinning while tucked must take effect immediately, not on next hover.
    Reveal();
    return;
  }
  // Unpinned: hand control back to the cursor.
  if (!IsMouseHovered()) {
    TuckAway();
    return;
  }
  // Cursor is still on the panel, so let it leave first — but the watcher has
  // to be RESTARTED, not merely relied on. views::MouseWatcher stops itself
  // once it has fired, and while pinned MouseMovedOutOfHost swallowed that
  // notification, so by now it has almost certainly fired and stopped. Without
  // this the sidebar would unpin but never tuck again.
  if (GetWidget()) {
    mouse_watcher_->Start(GetWidget()->GetNativeWindow());
  }
}

void ZephyrusSidebarView::TuckAway() {
  // Pinned means it stays out. Guard here rather than at every caller, so no
  // future caller can tuck it by accident.
  //
  // dragging_row_ belongs here for a stronger reason than resizing_ does. A
  // tab drag is SUPPOSED to leave the panel -- carrying a tab out over the page
  // is the whole gesture -- so the cursor leaving fires MouseMovedOutOfHost
  // almost immediately. Tucking then calls SetCanProcessEventsWithinSubtree
  // (false), the row stops receiving events mid-gesture, and the drag simply
  // never ends: no release, no drop, and the drop indicator left on screen with
  // nothing to take it down. Mouse capture cannot rescue that, because the
  // subtree has been switched off underneath it.
  // reveal_holds_ is the same kind of guard as dragging_row_ above: a popup
  // anchored into the panel (the downloads bubble) is positioned from its
  // anchor view, so sliding the panel out drags the popup off the screen edge
  // with it. That is what made the downloads button look like it opened
  // nothing -- the bubble was created, shown, and carried away.
  if (!revealed_ || pinned_ || resizing_ || dragging_row_ ||
      reveal_holds_ > 0) {
    return;
  }
  revealed_ = false;
  known_tabs_.clear();
  known_sets_.clear();
  SetCanProcessEventsWithinSubtree(false);
  float_shadow_.reset();

  reveal_poll_timer_.Start(FROM_HERE, base::Milliseconds(100), this,
                           &ZephyrusSidebarView::OnRevealPoll);
  // One tween for the whole component, now chosen by ROLE rather than by
  // matching whatever the neighbouring animation happened to use. The panel
  // slides, so it is spatial.
  reveal_animation_.SetTweenType(
      zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
  reveal_animation_.SetSlideDuration(
      gfx::Animation::ShouldRenderRichAnimation() ? kSlideOutDuration
                                                  : base::TimeDelta());
  reveal_animation_.Hide();
}

void ZephyrusSidebarView::AnimationProgressed(const gfx::Animation* animation) {
  // Pin first, THEN ask for the layout: both values are in place before the
  // pass starts, so it runs once and does not get re-dirtied halfway through.
  browser_view_->UpdateZephyrusSidebarPin();
  browser_view_->InvalidateLayout();
}

void ZephyrusSidebarView::AnimationEnded(const gfx::Animation* animation) {
  if (!revealed_) {
    browser_view_->SetZephyrusSidebarAttached(false);
  }
  UpdateFloatShadow();
  // Clearing the pin here is the one real resize of the interaction, spent with
  // the panel stationary.
  browser_view_->UpdateZephyrusSidebarPin();
  browser_view_->InvalidateLayout();
}

void ZephyrusSidebarView::OnWindowActivationChanged(bool active) {
  // The poll runs only while tucked and unpinned; Reveal() and pinning stop it
  // themselves, and TuckAway() starts it.
  if (pinned_ || revealed_ || hidden_by_layout_) {
    return;
  }
  if (!active) {
    reveal_poll_timer_.Stop();
    return;
  }
  if (!reveal_poll_timer_.IsRunning()) {
    reveal_poll_timer_.Start(FROM_HERE, base::Milliseconds(50), this,
                             &ZephyrusSidebarView::OnRevealPoll);
  }
}

void ZephyrusSidebarView::OnRevealPoll() {
  if (hidden_by_layout_) {
    reveal_poll_timer_.Stop();
    return;
  }
  if (pinned_) {
    reveal_poll_timer_.Stop();  // Nothing to poll for; it is already out.
    return;
  }
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

// If the tab being dragged disappears mid-gesture, the drag has nothing left
// to act on. Checked before the generic handling below, which would otherwise
// merely defer a rebuild and let the drop run against a freed pointer.
void ZephyrusSidebarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  // Checked FIRST, and before the tucked-away early return. If the dragged tab
  // is being removed, the generic path below would merely defer a rebuild and
  // then let the drop resolve an index for a tab that no longer exists.
  if (dragging_row_ && change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      if (removed.contents == drag_contents_) {
        CancelRowDrag();
        break;
      }
    }
  }

  // Sets. A tab opened FROM a tab that is in a set (a link) joins that set: tabs
  // do not leave their set until the user takes them out. A tab opened any other
  // way -- the sidebar's New tab button, Ctrl+T -- does not; only a set's own
  // plus opens a tab in it (NewTabInSet). And the set holding a newly active tab
  // opens, so the tab can be seen. Before the tucked-away return below: hidden
  // or not, the state has to stay right.
  std::optional<tab_groups::TabGroupId> joining;
  if (change.type() == TabStripModelChange::kInserted &&
      selection.active_tab_changed() && selection.old_contents &&
      selection.new_contents) {
    const int old_index =
        tab_strip_model_->GetIndexOfWebContents(selection.old_contents);
    const int new_index =
        tab_strip_model_->GetIndexOfWebContents(selection.new_contents);
    if (old_index != TabStripModel::kNoTab &&
        new_index != TabStripModel::kNoTab &&
        !tab_strip_model_->IsTabPinned(old_index) &&
        // The New tab button and Ctrl+T also open the tab from the active one
        // (it is its opener); what tells them from a link is the page.
        !IsNewTabPage(selection.new_contents) &&
        tab_strip_model_->GetOpenerOfTabAt(new_index) ==
            tab_strip_model_->GetTabAtIndex(old_index)) {
      joining = tab_strip_model_->GetTabGroupForTab(old_index);
    }
  }
  bool opened_a_set = false;
  if (selection.active_tab_changed()) {
    const int active = tab_strip_model_->active_index();
    if (active != TabStripModel::kNoTab) {
      if (const std::optional<tab_groups::TabGroupId> group =
              tab_strip_model_->GetTabGroupForTab(active)) {
        PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
        SetMeta meta = ReadSetMeta(prefs, *group);
        if (meta.collapsed) {
          meta.collapsed = false;
          WriteSetMeta(prefs, *group, meta);
          opened_a_set = true;
        }
      }
    }
  }
  if (joining && change.type() == TabStripModelChange::kInserted &&
      selection.new_contents) {
    // Posted: the model must not be changed from inside its own notification.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusSidebarView::JoinSet,
                       weak_factory_.GetWeakPtr(),
                       selection.new_contents->GetWeakPtr(), *joining));
  }

  // While tucked away the list isn't visible and Reveal() rebuilds it anyway,
  // so skip the (comparatively expensive) row churn during background tab
  // activity — this keeps the hidden sidebar at zero cost.
  if (!revealed_) {
    return;
  }
  // Anything but a removal or a plain switch changes the indices the rows were
  // built with, and a closing animation finds its row by them.
  if (change.type() != TabStripModelChange::kRemoved &&
      change.type() != TabStripModelChange::kSelectionOnly) {
    index_map_stale_ = true;
  }
  if (TryAnimateClose(change, selection)) {
    // The rebuild (which the set opening, or a join, also needs) comes when the
    // rows have finished closing.
    rebuild_deferred_ = true;
    return;
  }
  if (opened_a_set) {
    ScheduleRebuildTabList();
    return;
  }
  // A plain tab switch changes two rows' highlight, not the list. It used to
  // rebuild every row -- destroying and recreating the whole list, with the
  // row under the cursor -- on every click of a tab.
  if (change.type() == TabStripModelChange::kSelectionOnly &&
      selection.active_tab_changed() &&
      UpdateActiveRowsInPlace(selection.old_contents, selection.new_contents)) {
    return;
  }
  ScheduleRebuildTabList();
}

bool ZephyrusSidebarView::TryAnimateClose(
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() != TabStripModelChange::kRemoved ||
      !gfx::Animation::ShouldRenderRichAnimation() || dragging_row_ ||
      rename_active_ || index_map_stale_ || !tab_list_container_) {
    return false;
  }
  // The index a tab had when the rows were built: the model's own index counts
  // only the tabs that are still there.
  auto build_index = [&](int index) {
    for (int removed : removed_since_build_) {
      if (removed <= index) {
        ++index;
      }
    }
    return index;
  };
  std::vector<int> closing_indices;
  for (const auto& removed : change.GetRemove()->contents) {
    closing_indices.push_back(build_index(removed.index));
  }
  std::vector<ZephyrusTabRow*> rows;
  CollectTabRows(tab_list_container_, &rows);
  std::vector<ZephyrusTabRow*> closing;
  for (ZephyrusTabRow* row : rows) {
    if (std::find(closing_indices.begin(), closing_indices.end(),
                  row->model_index()) != closing_indices.end()) {
      closing.push_back(row);
    }
  }
  if (closing.empty()) {
    return false;
  }
  // A row in a list or a set folds away; one inside a split card is left to
  // the plain rebuild.
  for (ZephyrusTabRow* row : closing) {
    views::View* parent = row->parent();
    if (parent != tab_list_container_ &&
        !views::IsViewClass<ZephyrusSetBody>(parent)) {
      return false;
    }
  }

  // A rebuild may already be waiting (a tab leaving its set schedules one just
  // before the removal arrives). The one after the fold covers it.
  rebuild_pending_ = false;
  removed_since_build_.insert(removed_since_build_.end(),
                              closing_indices.begin(), closing_indices.end());
  std::sort(removed_since_build_.begin(), removed_since_build_.end());

  for (ZephyrusTabRow* row : closing) {
    closers_.push_back(std::make_unique<ZephyrusRowCloser>(
        row, base::BindOnce(
                 [](base::WeakPtr<ZephyrusSidebarView> sidebar) {
                   // Posted: this is called from inside the animation, and the
                   // closer is about to be destroyed.
                   base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
                       FROM_HERE,
                       base::BindOnce(
                           &ZephyrusSidebarView::OnRowCloseFinished, sidebar));
                 },
                 weak_factory_.GetWeakPtr())));
  }

  // The tab that took over as the active one is lit now, not after the fold.
  if (selection.active_tab_changed() && selection.new_contents) {
    const int now = tab_strip_model_->GetIndexOfWebContents(selection.new_contents);
    if (now != TabStripModel::kNoTab) {
      const int at_build = build_index(now);
      for (ZephyrusTabRow* row : rows) {
        if (row->model_index() == at_build) {
          row->SetFallbackIcon(GetTabFallbackIcon(selection.new_contents));
          row->UpdateContent(GetTabFavicon(selection.new_contents),
                             GetTabTitle(selection.new_contents),
                             /*is_active=*/true);
        }
      }
    }
  }
  return true;
}

void ZephyrusSidebarView::OnRowCloseFinished() {
  std::erase_if(closers_, [](const std::unique_ptr<ZephyrusRowCloser>& closer) {
    return closer->finished();
  });
  if (closers_.empty()) {
    // The rows are gone from sight; now the list is rebuilt from the model.
    rebuild_deferred_ = false;
    ScheduleRebuildTabList();
  }
}

bool ZephyrusSidebarView::UpdateActiveRowsInPlace(
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  if (!new_contents) {
    return false;
  }
  const int new_index = tab_strip_model_->GetIndexOfWebContents(new_contents);
  const int old_index =
      old_contents ? tab_strip_model_->GetIndexOfWebContents(old_contents)
                   : TabStripModel::kNoTab;
  ZephyrusTabRow* new_row = nullptr;
  ZephyrusTabRow* old_row = nullptr;
  std::vector<ZephyrusTabRow*> all_rows;
  CollectTabRows(tab_list_container_, &all_rows);
  for (ZephyrusTabRow* row : all_rows) {
    if (row->model_index() == new_index) {
      new_row = row;
    } else if (old_index != TabStripModel::kNoTab &&
               row->model_index() == old_index) {
      old_row = row;
    }
  }
  // The new tab has no row here -- another workspace, or a list about to
  // change for some other reason. Only a rebuild gets that right.
  if (!new_row) {
    return false;
  }
  if (old_row) {
    old_row->SetFallbackIcon(GetTabFallbackIcon(old_contents));
    old_row->UpdateContent(GetTabFavicon(old_contents),
                           GetTabTitle(old_contents),
                           /*is_active=*/false);
  }
  new_row->SetFallbackIcon(GetTabFallbackIcon(new_contents));
  new_row->UpdateContent(GetTabFavicon(new_contents), GetTabTitle(new_contents),
                         /*is_active=*/true);
  return true;
}

void ZephyrusSidebarView::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                  int index) {
  if (!revealed_) {
    return;
  }
  // A newly pinned tab becomes visible in every workspace, so the list has to
  // be rebuilt rather than just refreshed in place.
  index_map_stale_ = true;
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::OnTabChangedAt(tabs::TabInterface* tab,
                                         TabChangeType change_type) {
  if (!revealed_) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfTab(tab);
  // Update only the affected row's favicon/title in place. Rebuilding the whole
  // list here would destroy the row under the cursor on every loading tick,
  // causing the hover background and close button to flicker.
  content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
  if (!contents) {
    return;
  }
  // Rows are a filtered subset of the model, so find the row by model index.
  std::vector<ZephyrusTabRow*> all_rows;
  CollectTabRows(tab_list_container_, &all_rows);
  for (ZephyrusTabRow* row : all_rows) {
    if (row->model_index() == index) {
      row->SetFallbackIcon(GetTabFallbackIcon(contents));
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
          browser_view_->browser()->GetProfile());
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
        node->GetTitle(), vector_icons::kGlobeIcon, fg_ink,
        // Loads on first request; the next rebuild (each reveal) picks it up.
        model->GetFavicon(node.get()).AsImageSkia()));
    ++added;
  }
  const auto& fav_rows = favorites_container_->children();
  for (size_t i = 0; i < fav_rows.size(); ++i) {
    if (auto* fav = views::AsViewClass<ZephyrusActionRow>(fav_rows[i])) {
      fav->SetSegmentPosition(i == 0, i + 1 == fav_rows.size());
      fav->SetContained(ContainersOn());
    }
  }

  // Don't leave a "Favorites" heading standing over an empty space when the
  // user has no bookmarks.
  if (favorites_header_) {
    favorites_header_->SetVisible(added > 0);
    // The member is typed as a plain View because the class lives in this
    // file's anonymous namespace and the header cannot name it.
    views::AsViewClass<ZephyrusSectionHeader>(favorites_header_)
        ->SetCount(added);
  }
}

// Moves the borrowed new-tab button out of the tab list, if that is where it
// currently lives. Safe to call at any time.
void ZephyrusSidebarView::ParkBorrowedNewTabButton() {
  if (!browser_view_ || !compact_hidden_) {
    return;
  }
  ToolbarView* toolbar = browser_view_->toolbar();
  if (!toolbar || !toolbar->IsZephyrusCompact()) {
    return;
  }
  views::View* new_tab = toolbar->zephyrus_new_tab_button();
  if (!new_tab || !new_tab->parent() || new_tab->parent() == compact_hidden_) {
    return;
  }
  if (tab_list_container_ && tab_list_container_->Contains(new_tab)) {
    compact_hidden_->AddChildView(new_tab);
  }
}

// The panel's own add-workspace button: created once, and always the last
// thing in the foot bar so nothing can push it off the end.
void ZephyrusSidebarView::ShowDownloads(const ui::Event& event) {
  Browser* browser = browser_view_ ? zephyrus::AsBrowser(browser_view_->browser()) : nullptr;
  if (!browser) {
    return;
  }
  if (auto* downloads = DownloadToolbarUIController::From(browser)) {
    // BOTTOM_CENTER: the button is at the foot of the panel, so the popup
    // grows upward over the tab list instead of off the bottom of the window.
    if (views::Widget* popup = downloads->ZephyrusShowDetailsAnchoredTo(
            downloads_button_, views::BubbleBorder::BOTTOM_CENTER)) {
      ++reveal_holds_;
      popup->widget_delegate()->RegisterWindowClosingCallback(
          base::BindOnce(&ZephyrusSidebarView::ReleaseRevealHold,
                         weak_factory_.GetWeakPtr()));
      return;
    }
  }
  // Nothing recent to show. This button is always on the panel, unlike the
  // toolbar's, so it always has to do something: a control that answers a
  // click with silence reads as broken, which is exactly how the bubble's own
  // "no anchor, no downloads, return" path felt here.
  chrome::ShowDownloads(browser);
}

void ZephyrusSidebarView::ReleaseRevealHold() {
  if (reveal_holds_ > 0) {
    --reveal_holds_;
  }
  // The cursor has usually left the panel by the time a popup is dismissed,
  // and the mouse watcher does not fire again for a cursor that is already
  // outside -- so without this the panel would stay out until the user
  // happened to re-enter and leave it.
  TuckAwayIfCursorLeft();
}

void ZephyrusSidebarView::EnsureAddWorkspaceButton() {
  if (add_workspace_button_ || !compact_workspaces_ || !browser_view_) {
    return;
  }
  add_workspace_button_ = compact_workspaces_->AddChildView(
      std::make_unique<ZephyrusFootIconButton>(
          base::BindRepeating(
              [](ZephyrusSidebarView* self, const ui::Event&) {
                // Set up first, then create: see zephyrus_workspace_setup.h.
                zephyrus::ShowWorkspaceSetup(self->browser_view_,
                                             self->add_workspace_button_,
                                             /*workspace_id=*/0);
              },
              base::Unretained(this)),
          vector_icons::kAdd2Icon, u"New workspace"));
}

bool ZephyrusSidebarView::IsCompactMode() const {
  // Compact mode belongs to the sidebar layout. In the horizontal layout the
  // title-bar pin only hides the title bar; nothing is lent to this panel,
  // which is not even on screen.
  return browser_view_ && !browser_view_->IsZephyrusTitlebarPinned() &&
         !hidden_by_layout_;
}

int ZephyrusSidebarView::PanelRadius() const {
  return 0;
}

void ZephyrusSidebarView::OnUiLayoutChanged(zephyrus::UiLayout layout) {
  const bool clipping = layout == zephyrus::UiLayout::kClippingSidebar;
  if (clipping != clipping_) {
    clipping_ = clipping;
    float_shadow_.reset();
    // A revealed panel changes its relationship to the page on the spot:
    // classic's column always takes the page's column, a clipping panel only
    // when pinned.
    if (revealed_) {
      browser_view_->SetZephyrusSidebarAttached(pinned_ || !clipping_);
      UpdateFloatShadow();
    }
  }
  // Both strip layouts hold the tabs themselves; the sidebar has no work.
  const bool horizontal = layout == zephyrus::UiLayout::kHorizontalTabs ||
                          layout == zephyrus::UiLayout::kOneStrip;
  if (horizontal == hidden_by_layout_) {
    return;
  }
  hidden_by_layout_ = horizontal;
  if (horizontal) {
    // Out of the way completely: not pinned, not revealed, not polling the
    // cursor, and not holding the page's column.
    DropOmniboxOverlay();
    reveal_holds_ = 0;
    pinned_ = false;
    revealed_ = false;
  known_tabs_.clear();
  known_sets_.clear();
    reveal_poll_timer_.Stop();
    mouse_watcher_->Stop();
    reveal_animation_.Reset(0.0);
    float_shadow_.reset();
    SetCanProcessEventsWithinSubtree(false);
    browser_view_->SetZephyrusSidebarAttached(false);
    SetVisible(false);
  } else {
    SetVisible(true);
    reveal_poll_timer_.Start(FROM_HERE, base::Milliseconds(50), this,
                             &ZephyrusSidebarView::OnRevealPoll);
  }
  // Lends the title bar's controls, or hands them back.
  OnCompactModeChanged();
}

void ZephyrusSidebarView::UpdateFloatShadow() {
  // Only while resting OUT and covering the page. During the slide the shadow
  // would not move with the panel -- it follows the layer's bounds, and the
  // slide is a transform -- so it would sit in place ahead of the column. (The
  // hairline down its edge is painted in the view, and travels.)
  const bool want = revealed_ && !hidden_by_layout_ &&
                    !browser_view_->IsZephyrusSidebarAttached() &&
                    !reveal_animation_.is_animating();
  if (!want) {
    float_shadow_.reset();
    return;
  }
  if (!float_shadow_) {
    // Square, and low: the column is chrome, not a card.
    float_shadow_ = std::make_unique<views::ViewShadow>(this, kCoverElevation);
    float_shadow_->SetRoundedCornerRadius(0);
  }
}

void ZephyrusSidebarView::OnCompactModeChanged() {
  // RebuildTabList() rebuilds the chrome block as its last step, and the list
  // itself changes shape here: pinned tabs become the essentials grid, so
  // their rows have to go.
  RebuildTabList();
  InvalidateLayout();
}

// Borrows the title bar's controls while it is hidden, and hands them back
// when it returns.
//
// They are MOVED, not re-made: the same back button, the same omnibox, the
// same three-dot menu, with their own look and their own behaviour. Nothing
// here changes what a control IS -- only where it sits.
//
// The three containers are permanent and are never emptied with
// RemoveAllChildViews: that DELETES children, and these children belong to the
// toolbar -- destroying them left the toolbar holding dangling pointers and
// took the browser down on the next layout. Placement only ever moves a view
// from one container to another, which reparents without destroying.
//
// Placement is redone on every call because which controls are on the bar is
// not fixed: most of them (home, extensions, the avatar, the battery saver)
// are hidden most of the time, and a hidden control must not hold a space in
// the panel. Hidden ones go to a zero-size holder so they still have a parent
// to be reclaimed from.
bool ZephyrusSidebarView::ContainersOn() const {
  return browser_view_->browser()->GetProfile()->GetPrefs()->GetBoolean(
      "zephyrus.appearance.m3_containers");
}

void ZephyrusSidebarView::OnContainersChanged() {
  if (auto* row =
          views::AsViewClass<ZephyrusControlRow>(compact_controls_row_.get())) {
    row->SetContained(ContainersOn());
  }
  // The rows take the setting when they are built. Posted, as every rebuild
  // of this list is: it deletes the rows.
  ScheduleRebuildTabList();
  RebuildFavorites();
}

void ZephyrusSidebarView::RebuildCompactChrome() {
  if (!compact_chrome_ || !browser_view_) {
    return;
  }
  ToolbarView* toolbar = browser_view_->toolbar();
  const bool compact = IsCompactMode();
  compact_chrome_->SetVisible(compact);
  // Room between the address bar and the first tab. Focused, the bar grows into
  // a taller pill (the omnibox overlay) that reaches past its row; with no
  // heading above the cards any more, nothing was there to keep it off the first
  // tab. Only in compact mode, where the bar is in this panel at all.
  if (auto* list = views::AsViewClass<views::BoxLayoutView>(tab_list_container_)) {
    list->SetInsideBorderInsets(compact ? gfx::Insets::TLBR(kCompactListGap, 0, 0, 0)
                                        : gfx::Insets());
  }
  if (!toolbar) {
    return;
  }

  if (compact_workspaces_) {
    compact_workspaces_->SetVisible(compact);
  }
  if (!compact) {
    // Back in the panel first, so the reclaim below finds it there.
    DropOmniboxOverlay();
    if (toolbar->IsZephyrusCompact()) {
      // Reclaim takes each view back by pointer, whatever it is parented to
      // now.
      toolbar->ReclaimZephyrusChrome();
      toolbar->SetZephyrusWorkspaceStripCompact(false);
    }
    return;
  }
  if (!toolbar->IsZephyrusCompact()) {
    toolbar->LendZephyrusChromeTo(compact_chrome_);
    toolbar->SetZephyrusWorkspaceStripCompact(true);
  }
  EnsureAddWorkspaceButton();
  if (!compact_address_row_ || !compact_controls_row_ || !compact_hidden_ ||
      !compact_workspaces_) {
    return;
  }

  views::View* location_bar = toolbar->location_bar_view();
  views::View* workspaces = toolbar->zephyrus_workspace_strip();
  views::View* new_tab = toolbar->zephyrus_new_tab_button();
  bool address_visible = false;
  bool workspaces_visible = false;
  bool controls_visible = false;
  for (views::View* view : toolbar->ZephyrusLentViews()) {
    if (view == location_bar) {
      // Mid-edit it is in the overlay, and stays there until the edit ends.
      if (!omnibox_overlay_) {
        compact_address_row_->AddChildView(view);
      }
      address_visible = view->GetVisible();
    } else if (view == workspaces) {
      // The workspace switcher goes to the FOOT of the panel, away from the
      // page controls: switching workspace is not the same kind of act as
      // going back or reloading. It carries its own "new workspace" button at
      // its trailing edge.
      // Straight after downloads, taking whatever width is left.
      compact_workspaces_->AddChildViewAt(view, 1);
      static_cast<views::BoxLayout*>(compact_workspaces_->GetLayoutManager())
          ->SetFlexForView(view, 1);
      workspaces_visible = view->GetVisible();
    } else if (view == new_tab) {
      // New tab belongs to the tab list, beside its count -- not with the
      // page controls, which act on the page you are already on.
      auto* tabs_header =
          views::AsViewClass<ZephyrusSectionHeader>(tabs_header_);
      if (tabs_header && view->GetVisible()) {
        tabs_header->SetTrailingView(view);
      } else {
        compact_hidden_->AddChildView(view);
      }
    } else if (!view->GetVisible() ||
               view->GetPreferredSize().IsEmpty()) {
      // Zero-width children (the toolbar divider, an empty container) are not
      // controls; giving them a cell each is what put blank cells in the run.
      compact_hidden_->AddChildView(view);
    } else {
      // In the bar's own left-to-right order: back, forward, reload, then the
      // page actions. Nothing reorders them, so the strip in the panel is the
      // strip the user already knows from the title bar.
      compact_controls_row_->AddChildView(view);
      controls_visible = true;
    }
  }
  compact_address_row_->SetVisible(address_visible);
  compact_workspaces_->SetVisible(workspaces_visible);
  compact_controls_row_->SetVisible(controls_visible);
}

void ZephyrusSidebarView::ScheduleRebuildTabList() {
  // RebuildTabList() calls RemoveAllChildViews(), which deletes the row whose
  // close button is being clicked right now — and Views touches the button
  // again after its callback returns, so rebuilding synchronously from a
  // tab-strip observer is a use-after-free. It only became reliably fatal once
  // closing the last tab stopped tearing the window down (the window now stays
  // open on the empty state), which is what made this reachable at all.
  // A drag in progress is the one case where even a POSTED rebuild is wrong:
  // it would delete the row still under the user's finger. Hold it until the
  // gesture finishes. Tab titles change constantly while pages load, so
  // cancelling the drag instead would make dragging fail at random.
  if (dragging_row_ || rename_active_ || !closers_.empty()) {
    rebuild_deferred_ = true;
    return;
  }

  // Posting lets the input event unwind first. One posted rebuild covers any
  // number of events before it runs.
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusSidebarView::RunScheduledRebuild,
                                weak_factory_.GetWeakPtr()));
}

void ZephyrusSidebarView::RunScheduledRebuild() {
  // A direct RebuildTabList() since the post already did the work.
  if (!rebuild_pending_) {
    return;
  }
  RebuildTabList();
}

void ZephyrusSidebarView::CreateDragProxy(views::View* row) {
  auto* tab_row = views::AsViewClass<ZephyrusTabRow>(row);
  if (!tab_row || !browser_view_ || drag_proxy_) {
    return;
  }
  const int index = tab_row->model_index();
  if (index < 0 || index >= tab_strip_model_->count()) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
  if (!contents) {
    return;
  }

  drag_grab_offset_ = tab_row->press_point();
  auto proxy = std::make_unique<ZephyrusDragProxy>(
      GetTabFavicon(contents), GetTabTitle(contents), GetForegroundColor(),
      GetPanelColor());
  // Added last, so it is above the sidebar, the page, and the drop indicator.
  drag_proxy_ = browser_view_->AddChildView(std::move(proxy));
  drag_proxy_->SetSize(row->size());
}

void ZephyrusSidebarView::MoveDragProxyTo(const gfx::Point& cursor_screen) {
  if (!drag_proxy_ || !browser_view_) {
    return;
  }
  gfx::Point point = cursor_screen;
  views::View::ConvertPointFromScreen(browser_view_, &point);
  // Positioned by the grab offset, so the tab stays under the part of it the
  // user actually took hold of. Snapping its corner to the cursor instead makes
  // the tab appear to jump the moment it is picked up.
  drag_proxy_->SetPosition(
      gfx::Point(point.x() - drag_grab_offset_.x(),
                 point.y() - drag_grab_offset_.y()));
}

void ZephyrusSidebarView::DestroyDragProxy() {
  if (!drag_proxy_ || !browser_view_) {
    drag_proxy_ = nullptr;
    return;
  }
  browser_view_->RemoveChildViewT(drag_proxy_.get());
  drag_proxy_ = nullptr;
}

void ZephyrusSidebarView::UpdateSplitDropIndicator(bool visible) {
  if (!browser_view_) {
    return;
  }
  if (!split_drop_indicator_) {
    if (!visible) {
      return;
    }
    split_drop_indicator_ = browser_view_->AddChildView(
        // PRIMARY. It was handed the window's GROUND colour -- the colour of
        // the very surface it is drawn over -- so "drop here" was a faint
        // outline in the page's own background tone.
        std::make_unique<ZephyrusSplitDropIndicator>(
            zephyrus::m3::Role(*this, kColorZephyrusPrimary)));
  }

  if (visible) {
    // Positioned over the contents area only -- never the toolbar, and never
    // the sidebar itself, so the panel the tab came from stays legible.
    // Derived from the PANEL's own rectangle rather than from the window's.
    // The sidebar already spans exactly the content area vertically and stops
    // exactly where the page begins horizontally, so its bounds answer both
    // questions without needing to know anything about the toolbar's height.
    const gfx::Rect panel = bounds();
    split_drop_indicator_->SetBoundsRect(
        gfx::Rect(panel.right(), panel.y(),
                  std::max(0, browser_view_->width() - panel.right()),
                  panel.height()));
    split_drop_indicator_->SetVisible(true);
  }

  // Fades rather than appears. It arrives while the user is mid-gesture and
  // already watching the cursor; something snapping into existence beside it
  // reads as a glitch. 120ms -- long enough to register as a transition,
  // short enough that it is fully present before they decide to release.
  ui::ScopedLayerAnimationSettings settings(
      split_drop_indicator_->layer()->GetAnimator());
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  settings.SetTransitionDuration(base::Milliseconds(120));
  settings.SetTweenType(
      zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
  split_drop_indicator_->layer()->SetOpacity(visible ? 1.f : 0.f);
}

void ZephyrusSidebarView::BreakSplit(split_tabs::SplitTabId split_id) {
  split_tabs::SplitTabData* data = tab_strip_model_->GetSplitData(split_id);
  if (!data) {
    return;
  }

  // Pick the survivor BEFORE unsplitting: RemoveSplit rearranges the strip, and
  // the indices read afterwards would not be the ones these decisions were made
  // from.
  //
  // "Most used" is the one the user was in more recently. Taken from
  // WebContents' own last-active timestamp rather than a counter we keep --
  // a counter would need to survive session restore, agree with itself across
  // windows, and be maintained on every activation path, to answer a question
  // the browser already tracks.
  content::WebContents* best = nullptr;
  base::TimeTicks best_active;
  for (tabs::TabInterface* member : data->ListTabs()) {
    content::WebContents* contents = member->GetContents();
    if (!contents) {
      continue;
    }
    const base::TimeTicks active = contents->GetLastActiveTimeTicks();
    if (!best || active > best_active) {
      best = contents;
      best_active = active;
    }
  }

  tab_strip_model_->RemoveSplit(split_id);

  // Both tabs survive an unsplit -- only the arrangement goes. Activating the
  // more-used one is what makes the gesture read as "keep this one", which is
  // what the control appears to promise.
  if (best) {
    const int index = tab_strip_model_->GetIndexOfWebContents(best);
    if (index >= 0) {
      tab_strip_model_->ActivateTabAt(index);
    }
  }
}

void ZephyrusSidebarView::UpdateSetDropTarget(const gfx::Point& cursor_screen,
                                              int dragged_model_index) {
  std::optional<tab_groups::TabGroupId> target;
  bool loose = false;
  // A pinned tab is in no set and cannot be put in one; nor is anything
  // dropped while the cursor is over the page (that is a split).
  const bool pinned = dragged_model_index >= 0 &&
                      tab_strip_model_->IsTabPinned(dragged_model_index);
  const std::optional<tab_groups::TabGroupId> own =
      dragged_model_index >= 0
          ? tab_strip_model_->GetTabGroupForTab(dragged_model_index)
          : std::nullopt;
  if (!pinned && !drag_over_content_ &&
      GetBoundsInScreen().Contains(cursor_screen)) {
    loose = true;
    for (views::View* child : tab_list_container_->children()) {
      auto* section = views::AsViewClass<ZephyrusSetSection>(child);
      if (section && section->GetBoundsInScreen().Contains(cursor_screen)) {
        // Over its own set the tab is only being put in order: no ring, no
        // join, and the rows reorder live (OnRowDragged).
        if (!(own && section->set_id() == *own)) {
          target = section->set_id();
        }
        loose = false;
        break;
      }
    }
  }
  if (target == drag_target_set_ && loose == drag_target_loose_) {
    return;
  }
  if (drag_target_set_) {
    if (auto* old = FindSetSection(tab_list_container_, *drag_target_set_)) {
      old->SetDropHighlight(false);
    }
  }
  drag_target_set_ = target;
  drag_target_loose_ = loose;
  if (target) {
    if (auto* now = FindSetSection(tab_list_container_, *target)) {
      now->SetDropHighlight(true);
    }
  }
}

void ZephyrusSidebarView::ClearSetDropTarget() {
  if (drag_target_set_ && tab_list_container_) {
    if (auto* section = FindSetSection(tab_list_container_, *drag_target_set_)) {
      section->SetDropHighlight(false);
    }
  }
  drag_target_set_.reset();
  drag_target_loose_ = false;
}

void ZephyrusSidebarView::CreateSetDragProxy(const std::u16string& title,
                                             const std::string& icon_key,
                                             const gfx::Size& size,
                                             const gfx::Point& grab) {
  if (!browser_view_ || drag_proxy_) {
    return;
  }
  drag_grab_offset_ = grab;
  auto proxy = std::make_unique<ZephyrusDragProxy>(
      gfx::ImageSkia(), title, GetForegroundColor(), GetPanelColor());
  auto glyph = std::make_unique<ZephyrusSetGlyph>(icon_key);
  glyph->SetColor(GetForegroundColor());
  proxy->UseSetLook(std::move(glyph), size.height());
  drag_proxy_ = browser_view_->AddChildView(std::move(proxy));
  drag_proxy_->SetSize(size);
}

ZephyrusSidebarView::FlipPositions ZephyrusSidebarView::CaptureFlip() const {
  FlipPositions positions;
  if (!tab_list_container_) {
    return positions;
  }
  auto add = [&](views::View* parent) {
    for (views::View* child : parent->children()) {
      float visual_y = static_cast<float>(child->bounds().y());
      if (child->layer()) {
        visual_y += child->layer()->transform().To2dTranslation().y();
      }
      positions.emplace_back(child, visual_y);
    }
  };
  add(tab_list_container_);
  for (views::View* child : tab_list_container_->children()) {
    if (auto* section = views::AsViewClass<ZephyrusSetSection>(child)) {
      add(section->body());
    }
  }
  return positions;
}

void ZephyrusSidebarView::PlayFlip(const FlipPositions& before,
                                   const views::View* skip) {
  for (const auto& [child, old_y] : before) {
    if (child == skip || !child->parent()) {
      continue;
    }
    const float delta = old_y - static_cast<float>(child->bounds().y());
    if (std::abs(delta) < 0.5f) {
      continue;
    }
    if (!child->layer()) {
      child->SetPaintToLayer();
      child->layer()->SetFillsBoundsOpaquely(false);
    }
    gfx::Transform offset;
    offset.Translate(0, delta);
    child->layer()->SetTransform(offset);
    ui::ScopedLayerAnimationSettings settings(child->layer()->GetAnimator());
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    settings.SetTransitionDuration(base::Milliseconds(180));
    settings.SetTweenType(
        zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
    child->layer()->SetTransform(gfx::Transform());
  }
}

void ZephyrusSidebarView::PlaceDropSlot(views::View* container, size_t index) {
  index = std::min(index, container->children().size());
  if (drop_slot_ && drop_slot_->parent() == container) {
    const auto& siblings = container->children();
    size_t at = 0;
    for (views::View* child : siblings) {
      if (child == drop_slot_) {
        break;
      }
      ++at;
    }
    // Just before or just after itself is where it already is.
    if (at == index || at + 1 == index) {
      return;
    }
  }
  // The old gap closes where it was (it takes itself out when it is shut); the
  // new one opens where the tab would land, and everything else glides.
  const FlipPositions before = CaptureFlip();
  if (drop_slot_) {
    if (auto* old = views::AsViewClass<ZephyrusDropSlot>(drop_slot_.get())) {
      old->Dismiss();
    }
    drop_slot_ = nullptr;
  }
  drop_slot_ = container->AddChildViewAt(std::make_unique<ZephyrusDropSlot>(),
                                         index);
  tab_list_container_->DeprecatedLayoutImmediately();
  PlayFlip(before, nullptr);
}

void ZephyrusSidebarView::RemoveDropSlot() {
  if (!drop_slot_) {
    return;
  }
  if (auto* slot = views::AsViewClass<ZephyrusDropSlot>(drop_slot_.get())) {
    slot->Dismiss();
  }
  drop_slot_ = nullptr;
}

void ZephyrusSidebarView::UpdateDropSlot(
    const gfx::Point& cursor_screen,
    const std::optional<tab_groups::TabGroupId>& own_set) {
  views::View* container = nullptr;
  if (drag_target_set_) {
    if (auto* section = FindSetSection(tab_list_container_, *drag_target_set_)) {
      container = section->body();
    }
  } else if (drag_target_loose_ && own_set) {
    container = tab_list_container_;
  }
  if (!container) {
    RemoveDropSlot();
    return;
  }
  gfx::Point point = cursor_screen;
  views::View::ConvertPointFromScreen(container, &point);
  // The index among the container's children, where the gap would be put. In
  // the top-level list only the loose tabs are candidates.
  size_t index = 0;
  size_t seen = 0;
  bool any = false;
  size_t first_candidate = 0;
  for (views::View* child : container->children()) {
    if (child == drop_slot_) {
      // The open gap is a child like any other: it stays in the list (closing)
      // when it moves, so the index it is replaced at counts it.
      ++seen;
      continue;
    }
    auto* row = views::AsViewClass<ZephyrusTabRow>(child);
    const bool candidate =
        container == tab_list_container_
            ? row && row->model_index() >= 0 &&
                  !tab_strip_model_->IsTabPinned(row->model_index())
            : true;
    if (!candidate) {
      ++seen;
      continue;
    }
    if (!any) {
      first_candidate = seen;
    }
    any = true;
    ++seen;
    if (child->bounds().CenterPoint().y() < point.y()) {
      index = seen;
    }
  }
  if (container == tab_list_container_) {
    if (!any) {
      RemoveDropSlot();
      return;
    }
    // Above the first loose tab it goes BEFORE that tab, not at the top of the
    // whole list (where the sets are).
    index = std::max(index, first_candidate);
  }
  PlaceDropSlot(container, index);
}

ZephyrusSidebarView::DropSlotNeighbours ZephyrusSidebarView::TakeDropSlot() {
  DropSlotNeighbours neighbours;
  if (!drop_slot_ || !drop_slot_->parent()) {
    return neighbours;
  }
  const auto& siblings = drop_slot_->parent()->children();
  size_t at = 0;
  for (views::View* child : siblings) {
    if (child == drop_slot_) {
      break;
    }
    ++at;
  }
  auto contents_of = [&](views::View* view) -> content::WebContents* {
    std::vector<ZephyrusTabRow*> rows;
    if (auto* row = views::AsViewClass<ZephyrusTabRow>(view)) {
      rows.push_back(row);
    } else {
      CollectTabRows(view, &rows);
    }
    if (rows.empty() || rows.front()->model_index() < 0 ||
        rows.front()->model_index() >= tab_strip_model_->count()) {
      return nullptr;
    }
    return tab_strip_model_->GetWebContentsAt(rows.front()->model_index());
  };
  for (size_t i = at + 1; i < siblings.size() && !neighbours.after; ++i) {
    neighbours.after = contents_of(siblings[i]);
  }
  for (size_t i = at; i-- > 0 && !neighbours.before;) {
    neighbours.before = contents_of(siblings[i]);
  }
  RemoveDropSlot();
  return neighbours;
}

void ZephyrusSidebarView::ReorderChildWithFlip(views::View* moved,
                                               size_t target) {
  const auto& children = tab_list_container_->children();
  // FLIP, as OnRowDragged does it: note where everything is, move the view,
  // then play every other child from its old place to its new one.
  std::vector<std::pair<views::View*, float>> before;
  before.reserve(children.size());
  for (views::View* child : children) {
    float visual_y = static_cast<float>(child->bounds().y());
    if (child->layer()) {
      visual_y += child->layer()->transform().To2dTranslation().y();
    }
    before.emplace_back(child, visual_y);
  }
  tab_list_container_->ReorderChildView(moved, target);
  tab_list_container_->DeprecatedLayoutImmediately();
  for (const auto& [child, old_y] : before) {
    if (child == moved) {
      continue;
    }
    const float delta = old_y - static_cast<float>(child->bounds().y());
    if (std::abs(delta) < 0.5f) {
      continue;
    }
    if (!child->layer()) {
      child->SetPaintToLayer();
      child->layer()->SetFillsBoundsOpaquely(false);
    }
    gfx::Transform offset;
    offset.Translate(0, delta);
    child->layer()->SetTransform(offset);
    ui::ScopedLayerAnimationSettings settings(child->layer()->GetAnimator());
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    settings.SetTransitionDuration(base::Milliseconds(180));
    settings.SetTweenType(
        zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
    child->layer()->SetTransform(gfx::Transform());
  }
}

void ZephyrusSidebarView::OnSetDragged(const tab_groups::TabGroupId& id,
                                       const gfx::Point& grab,
                                       const gfx::Point& cursor_screen) {
  ZephyrusSetSection* section = FindSetSection(tab_list_container_, id);
  if (!section) {
    return;
  }
  if (!dragging_row_) {
    dragging_row_ = true;
    CreateSetDragProxy(
        section->header()->GetTitle(),
        ReadSetMeta(browser_view_->browser()->GetProfile()->GetPrefs(), id).icon,
        section->header()->size(), grab);
    // The set itself stays in the list, dimmed, as the slot it will land in.
    section->SetPaintToLayer();
    section->layer()->SetFillsBoundsOpaquely(false);
    section->layer()->SetOpacity(0.35f);
  }
  MoveDragProxyTo(cursor_screen);

  // Reorder among the sets next to this one (a run of sets is bounded by
  // anything else in the list, so the pinned sets and the others stay apart).
  const auto& children = tab_list_container_->children();
  const auto it = std::find(children.begin(), children.end(), section);
  if (it == children.end()) {
    return;
  }
  const size_t self = static_cast<size_t>(it - children.begin());
  auto is_set = [&](size_t i) {
    return views::AsViewClass<ZephyrusSetSection>(children[i]) != nullptr;
  };
  size_t first = self;
  while (first > 0 && is_set(first - 1)) {
    --first;
  }
  size_t last = self;
  while (last + 1 < children.size() && is_set(last + 1)) {
    ++last;
  }
  gfx::Point point = cursor_screen;
  views::View::ConvertPointFromScreen(tab_list_container_, &point);
  size_t target = self;
  for (size_t i = first; i <= last; ++i) {
    if (i == self) {
      continue;
    }
    const int mid = children[i]->bounds().CenterPoint().y();
    if (i < self && point.y() < mid) {
      target = i;
      break;
    }
    if (i > self && point.y() > mid) {
      target = i;
    }
  }
  if (target != self) {
    ReorderChildWithFlip(section, target);
  }
}

void ZephyrusSidebarView::OnSetDragFinished(const tab_groups::TabGroupId& id,
                                            bool cancelled) {
  if (!dragging_row_) {
    return;
  }
  dragging_row_ = false;
  DestroyDragProxy();
  if (!cancelled && tab_list_container_) {
    // Keep the order the list now shows: the sets on screen first, in that
    // order, then any not on screen (another workspace's) as they were.
    std::vector<std::string> shown;
    for (views::View* child : tab_list_container_->children()) {
      if (auto* section = views::AsViewClass<ZephyrusSetSection>(child)) {
        shown.push_back(section->set_id().ToString());
      }
    }
    PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
    std::vector<std::string> rest;
    for (const base::Value& value : prefs->GetList(kSetsOrderPref)) {
      if (value.is_string() &&
          std::find(shown.begin(), shown.end(), value.GetString()) ==
              shown.end()) {
        rest.push_back(value.GetString());
      }
    }
    ScopedListPrefUpdate update(prefs, kSetsOrderPref);
    update->clear();
    for (const std::string& token : shown) {
      update->Append(token);
    }
    for (const std::string& token : rest) {
      update->Append(token);
    }
  }
  rebuild_deferred_ = false;
  ScheduleRebuildTabList();
  TuckAwayIfCursorLeft();
}

void ZephyrusSidebarView::OnRowDragged(views::View* row, int y_in_container) {
  if (!tab_list_container_ || !row) {
    return;
  }
  auto* dragged = views::AsViewClass<ZephyrusTabRow>(row);
  if (!dragged) {
    return;
  }

  if (!dragging_row_) {
    const int index = dragged->model_index();
    if (index < 0 || index >= tab_strip_model_->count()) {
      return;
    }
    drag_contents_ = tab_strip_model_->GetWebContentsAt(index);
    if (!drag_contents_) {
      return;
    }
    dragging_row_ = true;
    CreateDragProxy(row);
  }

  // Which gesture is this? Decided by where the cursor is, every frame, so the
  // user can change their mind by moving back over the list.
  const gfx::Point cursor =
      display::Screen::Get()->GetCursorScreenPoint();
  MoveDragProxyTo(cursor);
  const gfx::Rect panel = GetBoundsInScreen();
  const gfx::Rect window = browser_view_->GetBoundsInScreen();
  const bool over_content =
      window.Contains(cursor) && cursor.x() > panel.right();
  if (over_content != drag_over_content_) {
    drag_over_content_ = over_content;
    dragged->SetSplitAffordance(over_content);
    UpdateSplitDropIndicator(over_content);
  }
  // Sets: over a set, a drop adds the tab to it; over the loose tabs, a drop
  // takes it out of its set.
  UpdateSetDropTarget(cursor, dragged->model_index());
  // Over the page there is no slot to open a gap for, so the list is left
  // alone -- reordering underneath a gesture that is no longer about order
  // would rearrange tabs the user never meant to touch.
  if (over_content) {
    RemoveDropSlot();
    return;
  }
  // Moving between lists (into another set, or out of its own): a gap opens
  // where the tab would land, and the drop puts it there.
  const std::optional<tab_groups::TabGroupId> own_set =
      dragged->model_index() >= 0
          ? tab_strip_model_->GetTabGroupForTab(dragged->model_index())
          : std::nullopt;
  if (drag_target_set_ || (drag_target_loose_ && own_set)) {
    UpdateDropSlot(cursor, own_set);
    return;
  }
  RemoveDropSlot();

  // Reordering inside its own list: the loose tabs, the pinned ones, or the
  // tabs of its set. The row's parent is that list.
  views::View* const container = row->parent();
  if (!container) {
    return;
  }
  const auto& children = container->children();
  const auto it = std::find(children.begin(), children.end(), row);
  if (it == children.end()) {
    return;
  }
  const size_t self = static_cast<size_t>(it - children.begin());

  // The top-level list interleaves section headers ("Pinned tabs", "Tabs") with
  // rows. A drag may only move a row WITHIN its own run of rows: crossing a
  // header would mean pinning or unpinning, which is a different operation
  // with different consequences and belongs to the context menu, not to a
  // gesture the user might make by accident.
  auto is_row = [&](size_t i) {
    return views::AsViewClass<ZephyrusTabRow>(children[i]) != nullptr;
  };
  size_t first = self;
  while (first > 0 && is_row(first - 1)) {
    --first;
  }
  size_t last = self;
  while (last + 1 < children.size() && is_row(last + 1)) {
    ++last;
  }

  // Move past a neighbour once the cursor crosses ITS midpoint, not the
  // dragged row's. Using the neighbour's midpoint is what stops the row
  // oscillating between two slots when the cursor sits near a boundary.
  size_t target = self;
  for (size_t i = first; i <= last; ++i) {
    if (i == self) {
      continue;
    }
    const int mid = children[i]->bounds().CenterPoint().y();
    if (i < self && y_in_container < mid) {
      target = i;
      break;
    }
    if (i > self && y_in_container > mid) {
      target = i;
    }
  }

  if (target == self) {
    return;
  }

  // FLIP: read where everything is, move it, then put it visually back and let
  // it animate forward. Without this the rows that make way jump to their new
  // slots in a single frame, which is the part that reads as broken -- the
  // dragged row glides and everything around it teleports.
  //
  // Positions are read as VISUAL positions (layout slot plus whatever offset a
  // still-running animation is applying), not layout positions. Reading layout
  // alone would snap any row that is still in flight back to its slot before
  // re-animating it, and during a drag rows are in flight most of the time.
  const FlipPositions before = CaptureFlip();
  container->ReorderChildView(row, target);
  container->DeprecatedLayoutImmediately();
  // The dragged row is driven by the cursor, not by this.
  PlayFlip(before, row);
}

void ZephyrusSidebarView::OnRowDragFinished() {
  if (!dragging_row_) {
    return;
  }
  dragging_row_ = false;

  content::WebContents* const contents = drag_contents_;
  drag_contents_ = nullptr;

  // Resolve the index NOW rather than trusting the one captured at press time.
  // Between then and now a tab may have opened, closed or moved, and acting on
  // the stale number would reorder somebody else's tab.
  const int from =
      contents ? tab_strip_model_->GetIndexOfWebContents(contents) : -1;

  const bool split_drop = drag_over_content_;
  drag_over_content_ = false;
  UpdateSplitDropIndicator(false);
  DestroyDragProxy();

  // Sets: dropped onto a set, or out of one onto the loose tabs. The gap that
  // was open says where in that list.
  const std::optional<tab_groups::TabGroupId> set_target = drag_target_set_;
  const bool to_loose = drag_target_loose_;
  const DropSlotNeighbours slot = TakeDropSlot();
  ClearSetDropTarget();
  if (!split_drop && from >= 0 && !tab_strip_model_->IsTabPinned(from) &&
      (set_target || to_loose)) {
    const std::optional<tab_groups::TabGroupId> current =
        tab_strip_model_->GetTabGroupForTab(from);
    bool changed = false;
    if (set_target && current != set_target) {
      tab_strip_model_->AddToExistingGroup({from}, *set_target);
      changed = true;
    } else if (!set_target && to_loose && current) {
      tab_strip_model_->RemoveFromGroup({from});
      changed = true;
    }
    if (changed) {
      // Then to the place the gap was: just before the tab that followed it, or
      // just after the one that preceded it.
      const int moved = contents ? tab_strip_model_->GetIndexOfWebContents(contents)
                                 : -1;
      if (moved >= 0) {
        int destination = -1;
        if (slot.after) {
          const int next = tab_strip_model_->GetIndexOfWebContents(slot.after);
          if (next >= 0) {
            destination = moved < next ? next - 1 : next;
          }
        } else if (slot.before) {
          const int prev = tab_strip_model_->GetIndexOfWebContents(slot.before);
          if (prev >= 0) {
            destination = moved < prev ? prev : prev + 1;
          }
        }
        if (destination >= 0 && destination != moved &&
            destination < tab_strip_model_->count()) {
          tab_strip_model_->MoveWebContentsAt(moved, destination,
                                              /*select_after_move=*/false);
        }
      }
      rebuild_deferred_ = false;
      ScheduleRebuildTabList();
      TuckAwayIfCursorLeft();
      return;
    }
  }

  if (split_drop) {
    // AddToNewSplit pairs the given tabs with the ACTIVE one, which is exactly
    // the tab the user dropped onto -- the page under the cursor is the active
    // tab by definition.
    tabs::TabInterface* tab =
        from >= 0 ? tab_strip_model_->GetTabAtIndex(from) : nullptr;
    const bool already_split = tab && tab->GetSplit().has_value();
    // Dropping a tab onto itself, or onto a page it is already sharing, is a
    // no-op rather than an error: the user aimed at the page they were already
    // looking at, and nothing about that should destroy an arrangement.
    if (from >= 0 && from != tab_strip_model_->active_index() &&
        !already_split) {
      tab_strip_model_->AddToNewSplit(
          {from}, split_tabs::SplitTabVisualData(),
          split_tabs::SplitTabCreatedSource::kDragAndDropTab);
    }
    rebuild_deferred_ = false;
    ScheduleRebuildTabList();
    TuckAwayIfCursorLeft();
    return;
  }

  if (from >= 0 && tab_list_container_) {
    // Where the row ended up visually, versus the order the model still holds.
    // Its siblings: the rows in the same list, which is the loose tabs, the
    // pinned ones, or the tabs of its set.
    std::vector<ZephyrusTabRow*> all;
    CollectTabRows(tab_list_container_, &all);
    views::View* list = nullptr;
    for (ZephyrusTabRow* r : all) {
      if (r->model_index() == from) {
        list = r->parent();
      }
    }
    std::vector<ZephyrusTabRow*> visual;
    if (list) {
      for (views::View* child : list->children()) {
        if (auto* r = views::AsViewClass<ZephyrusTabRow>(child)) {
          if (r->model_index() >= 0 &&
              tab_strip_model_->IsTabPinned(r->model_index()) ==
                  tab_strip_model_->IsTabPinned(from)) {
            visual.push_back(r);
          }
        }
      }
    }
    std::vector<ZephyrusTabRow*> by_model = visual;
    std::sort(by_model.begin(), by_model.end(),
              [](ZephyrusTabRow* a, ZephyrusTabRow* b) {
                return a->model_index() < b->model_index();
              });

    size_t position = 0;
    for (; position < visual.size(); ++position) {
      if (visual[position]->model_index() == from) {
        break;
      }
    }
    // The destination is the model index currently occupied by whatever sits
    // at that visual position. Derived this way rather than by arithmetic on
    // the position, because workspace filtering means the displayed tabs are
    // NOT contiguous in the model -- other workspaces' tabs sit between them.
    if (position < by_model.size()) {
      const int to = by_model[position]->model_index();
      if (to != from) {
        tab_strip_model_->MoveWebContentsAt(from, to,
                                            /*select_after_move=*/false);
      }
    }
  }

  // The move above fires a tab-strip notification, which defers straight back
  // into rebuild_deferred_. Either way the list is rebuilt exactly once, from
  // the model, discarding whatever provisional order the drag left behind.
  //
  // Delayed past the settle animation on purpose: rebuilding immediately
  // destroys the row mid-flight and the drop snaps instead of landing. The
  // delay is slightly longer than the animation so the two never race.
  if (rebuild_deferred_ || from >= 0) {
    rebuild_deferred_ = false;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&ZephyrusSidebarView::RebuildTabList,
                       weak_factory_.GetWeakPtr()),
        base::Milliseconds(170));
  }
}

void ZephyrusSidebarView::TuckAwayIfCursorLeft() {
  // MouseMovedOutOfHost fired while the drag was in progress and was ignored.
  // The watcher will not fire again for a cursor that is already outside, so
  // without this the panel stays out until the user happens to re-enter and
  // leave it -- which, after dropping a tab onto the page, is the last thing
  // they are about to do.
  if (pinned_ || !revealed_) {
    return;
  }
  const gfx::Point cursor = display::Screen::Get()->GetCursorScreenPoint();
  if (!GetBoundsInScreen().Contains(cursor)) {
    TuckAway();
  }
}

void ZephyrusSidebarView::CancelRowDrag() {
  if (!dragging_row_) {
    return;
  }
  dragging_row_ = false;
  drag_over_content_ = false;
  UpdateSplitDropIndicator(false);
  ClearSetDropTarget();
  RemoveDropSlot();
  DestroyDragProxy();
  drag_contents_ = nullptr;
  rebuild_deferred_ = false;
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::RebuildTabList() {
  rebuild_pending_ = false;
  // The tab list is about to be cleared, and clearing DELETES children. The
  // new-tab button in the heading belongs to the toolbar, so it goes back to
  // the parking view first; RebuildCompactChrome() re-adopts it at the end of
  // this function.
  ParkBorrowedNewTabButton();
  tabs_header_ = nullptr;
  drop_slot_ = nullptr;
  closers_.clear();
  removed_since_build_.clear();
  index_map_stale_ = false;

  if (!tab_list_container_) {
    return;
  }
  tab_list_container_->RemoveAllChildViews();

  const SkColor fg = GetForegroundColor();
  // These were alphas of the ink (0x33 / 0x1A), which is the right derivation
  // on a DARK base -- a wash of white lifts a dark surface. On the warm light
  // theme it inverts: a wash of wine over cream DARKENS the row, so the active
  // tab would be the muddiest thing in the list instead of the brightest.
  //
  // Raised things go lighter here. The active row is the near-white ground
  // lifted off the cream panel, which is exactly the relationship the reference
  // uses between its cards and the surface behind them. Hover is a much
  // gentler tint of the same idea, since it also carries a 1px outline.
  // The active row NO LONGER INVERTS.
  //
  // Full inversion made the active row solid ink -- pure white on the dark
  // theme -- and favicons are images we deliberately never recolour. Any site
  // with a white logo therefore vanished into the active row, and unlike our
  // own glyphs there is no ink to "flip": the content belongs to the site.
  //
  // A mid-tone step fixes it from both directions at once: it is clearly
  // distinct from the panel, and it is far enough from both white and black
  // that a favicon of either extreme still reads. It also retires the bug that
  // has now cost five fixes (label, close glyph, menu selection, fallback
  // globe, and this) -- nothing on the row inverts, so nothing on the row can
  // disappear into it.
  //
  // What marks "this one" is the CONTAINER: an M3 navigation-drawer item on
  // secondary-container, fully rounded. It replaced both the inversion and the
  // accent bar that briefly stood in for it -- a filled pill and a separate
  // marker were the same statement made twice.
  const bool contained = ContainersOn();
  // The TAB rows are always the settings rail's segmented tonal cards, whatever
  // the containers setting says: that setting is for the chrome around them
  // (favourites, the new-tab button), and a list of tabs without its cards read
  // as bare text.
  const bool rows_contained = true;
  const SkColor on_surface = zephyrus::m3::Role(*this, kColorZephyrusOnSurface);
  const SkColor row_container =
      zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHigh);
  const ZephyrusRowColors row_colors{
      .foreground = fg,
      // One step ABOVE the panel, which already paints surface-container:
      // a segment filled with its own background is an invisible segment
      // (measured -- rows and panel came out at exactly 31,32,32).
      .idle_bg = rows_contained ? row_container : SK_ColorTRANSPARENT,
      .active_bg = zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer),
      .hover_bg = rows_contained
                      ? zephyrus::m3::WithStateLayer(row_container, on_surface,
                                                     zephyrus::m3::kHover)
                      : zephyrus::m3::StateLayer(on_surface,
                                                 zephyrus::m3::kHover),
      .pressed_bg = rows_contained
                        ? zephyrus::m3::WithStateLayer(
                              row_container, on_surface, zephyrus::m3::kPressed)
                        : zephyrus::m3::StateLayer(on_surface,
                                                   zephyrus::m3::kPressed),
      .active_pressed_bg = zephyrus::m3::WithStateLayer(
          zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer),
          zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer),
          zephyrus::m3::kPressed),
      .active_fg =
          zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer),
      .contained = rows_contained,
  };

  ZephyrusWorkspaceManager* workspace_manager =
      browser_view_->zephyrus_workspace_manager();
  const int active_index = tab_strip_model_->active_index();

  // Every row drawn, with its tab, so the new one can be found afterwards.
  std::vector<std::pair<ZephyrusTabRow*, const content::WebContents*>>
      rendered_rows;

  // Adds one row for the tab at `index`. Shared by both sections below.
  auto add_row_into = [&](views::View* parent, int index) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
    auto* row = parent->AddChildView(
        std::make_unique<ZephyrusTabRow>(
            GetTabFavicon(contents), GetTabTitle(contents),
            index == active_index, index, row_colors,
            contents->IsCurrentlyAudible(), contents->IsAudioMuted(),
            // The TAB, not its index. An index captured here goes stale the
            // moment anything reorders the strip before the next rebuild --
            // and rebuilds are posted, and held for the whole of a drag -- so
            // a row's close button could close whichever tab had slid into
            // that slot.
            base::BindRepeating(&ZephyrusSidebarView::ToggleTabMuted,
                                base::Unretained(this),
                                contents->GetWeakPtr()),
            base::BindRepeating(&ZephyrusSidebarView::ActivateTab,
                                base::Unretained(this),
                                contents->GetWeakPtr()),
            base::BindRepeating(&ZephyrusSidebarView::CloseTab,
                                base::Unretained(this),
                                contents->GetWeakPtr())));
    row->SetFallbackIcon(GetTabFallbackIcon(contents));
    rendered_rows.emplace_back(row, contents);
    // Right-click a row -> "Pin tab" / "Move to workspace" / "Close tab".
    row->set_context_menu_controller(this);
    row->SetDragCallbacks(
        base::BindRepeating(&ZephyrusSidebarView::OnRowDragged,
                            base::Unretained(this)),
        base::BindRepeating(&ZephyrusSidebarView::OnRowDragFinished,
                            base::Unretained(this)),
        base::BindRepeating(&ZephyrusSidebarView::CancelRowDrag,
                            base::Unretained(this)));
    return row;
  };

  // Ordinary rows go straight into the list.
  auto add_row = [&](int index) {
    add_row_into(tab_list_container_, index);
  };

  // Tabs already rendered as part of a split card, so the partner is not also
  // emitted as a loose row below it.
  std::set<int> consumed;

  // Emits `index` as a joined split card if it belongs to a split whose other
  // members are also visible here. Returns false if it is an ordinary tab.
  auto add_split_card = [&](int index) -> bool {
    tabs::TabInterface* tab = tab_strip_model_->GetTabAtIndex(index);
    if (!tab) {
      return false;
    }
    const std::optional<split_tabs::SplitTabId> split = tab->GetSplit();
    if (!split.has_value()) {
      return false;
    }
    split_tabs::SplitTabData* data = tab_strip_model_->GetSplitData(*split);
    if (!data) {
      return false;
    }
    std::vector<int> members;
    for (tabs::TabInterface* member : data->ListTabs()) {
      const int member_index = tab_strip_model_->GetIndexOfTab(member);
      if (member_index >= 0) {
        members.push_back(member_index);
      }
    }
    // A split of one is not a split. Chromium dissolves them, but the list can
    // observe the strip mid-change, and half a card is worse than a plain row.
    if (members.size() < 2) {
      return false;
    }

    auto* card = tab_list_container_->AddChildView(
        std::make_unique<ZephyrusSplitCard>(
            fg, base::BindRepeating(&ZephyrusSidebarView::BreakSplit,
                                    base::Unretained(this), *split)));
    for (int member_index : members) {
      add_row_into(card, member_index);
      consumed.insert(member_index);
    }
    card->AddBreakControl(fg);
    return true;
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

  // Sets: the unpinned tabs in a set go under that set's header, the rest are
  // loose. Pinned sets come first of all, the others after the pinned tabs.
  PrefService* const prefs = browser_view_->browser()->GetProfile()->GetPrefs();
  const bool sets_baseline = !known_tabs_.empty();
  std::vector<std::pair<tab_groups::TabGroupId, std::vector<int>>> grouped;
  std::vector<int> loose;
  for (int index : unpinned) {
    const std::optional<tab_groups::TabGroupId> group =
        tab_strip_model_->GetTabGroupForTab(index);
    if (!group) {
      loose.push_back(index);
      continue;
    }
    auto it = std::find_if(grouped.begin(), grouped.end(),
                           [&](const auto& entry) {
                             return entry.first == *group;
                           });
    if (it == grouped.end()) {
      grouped.emplace_back(*group, std::vector<int>());
      it = grouped.end() - 1;
    }
    it->second.push_back(index);
  }
  std::vector<tab_groups::TabGroupId> ordered_sets;
  for (const auto& entry : grouped) {
    ordered_sets.push_back(entry.first);
  }
  ordered_sets = OrderedSets(prefs, std::move(ordered_sets));
  std::set<std::string> current_set_tokens;
  std::vector<ZephyrusSetSection*> new_sections;
  const int active_index_for_sets = tab_strip_model_->active_index();
  auto emit_sets = [&](bool pinned_sets) {
    for (const tab_groups::TabGroupId& id : ordered_sets) {
      const SetMeta meta = ReadSetMeta(prefs, id);
      if (meta.pinned != pinned_sets) {
        continue;
      }
      const auto it = std::find_if(grouped.begin(), grouped.end(),
                                   [&](const auto& entry) {
                                     return entry.first == id;
                                   });
      const std::vector<int>& indices = it->second;
      const bool has_active =
          std::find(indices.begin(), indices.end(), active_index_for_sets) !=
          indices.end();
      current_set_tokens.insert(id.ToString());
      auto* section = tab_list_container_->AddChildView(
          std::make_unique<ZephyrusSetSection>(
              id, GetSetTitle(tab_strip_model_, id), meta.icon, meta.pinned,
              meta.collapsed, has_active, static_cast<int>(indices.size()),
              ZephyrusSetSection::Callbacks{
                  base::BindRepeating(&ZephyrusSidebarView::SetSetCollapsed,
                                      base::Unretained(this), id),
                  base::BindRepeating(&ZephyrusSidebarView::ToggleSetPinned,
                                      base::Unretained(this), id),
                  base::BindRepeating(&ZephyrusSidebarView::RenameSet,
                                      base::Unretained(this), id),
                  base::BindRepeating(&ZephyrusSidebarView::SetRenameActive,
                                      base::Unretained(this)),
                  base::BindRepeating(&ZephyrusSidebarView::NewTabInSet,
                                      base::Unretained(this), id),
                  base::BindRepeating(&ZephyrusSidebarView::OnSetDragged,
                                      base::Unretained(this), id),
                  base::BindRepeating(&ZephyrusSidebarView::OnSetDragFinished,
                                      base::Unretained(this), id),
                  base::BindRepeating(&ZephyrusSidebarView::ShowSetIconPicker,
                                      base::Unretained(this), id)}));
      section->header()->set_context_menu_controller(this);
      // Air between one set and whatever is next: the loose tabs read as part of
      // the set without it.
      section->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 8, 0));
      for (int index : indices) {
        add_row_into(section->body(), index);
      }
      if (sets_baseline && !known_sets_.count(id.ToString())) {
        new_sections.push_back(section);
      }
    }
  };

  // In compact mode the pinned tabs are the essentials grid at the top of the
  // panel (RebuildCompactChrome), so listing them here as well would show
  // every pinned tab twice.
  emit_sets(/*pinned_sets=*/true);
  if (!pinned.empty() && !IsCompactMode()) {
    tab_list_container_
        ->AddChildView(std::make_unique<ZephyrusSectionHeader>(u"Pinned tabs"))
        ->SetCount(pinned.size());
    for (int index : pinned) {
      if (consumed.count(index)) {
        continue;
      }
      if (!add_split_card(index)) {
        add_row(index);
      }
    }
  }
  emit_sets(/*pinned_sets=*/false);
  // A rounded rule between the sets and the loose tabs under them.
  if (!ordered_sets.empty() && !loose.empty()) {
    tab_list_container_->AddChildView(
        std::make_unique<ZephyrusRoundedDivider>());
  }
  if (!loose.empty()) {
    // No heading above the cards in any layout. In compact mode it used to name
    // the workspace and carry a count and a + new-tab button; the cards are the
    // list, the New tab button under them does that job, and tabs_header_ stays
    // null so RebuildCompactChrome parks its + out of sight.
    for (int index : loose) {
      if (consumed.count(index)) {
        continue;
      }
      if (!add_split_card(index)) {
        add_row(index);
      }
    }
  }

  AssignSegmentPositions(tab_list_container_);
  for (views::View* child : tab_list_container_->children()) {
    if (auto* section = views::AsViewClass<ZephyrusSetSection>(child)) {
      AssignSegmentPositions(section->body());
    }
  }
  // A set made just now arrives: its header slides and fades in while its body
  // opens from nothing.
  for (ZephyrusSetSection* section : new_sections) {
    AnimateRowIn(section->header());
    section->PlayCreation();
  }
  known_sets_ = std::move(current_set_tokens);

  // Slide in the row of a tab that was just opened: exactly one tab more than
  // the last rebuild drew, and exactly one of them unknown. A workspace switch
  // changes many at once and a swapped-out page keeps the count, so neither
  // animates.
  ZephyrusTabRow* slide_after = nullptr;
  {
    std::set<const void*> now;
    for (const auto& entry : rendered_rows) {
      now.insert(entry.second);
    }
    std::vector<ZephyrusTabRow*> added;
    if (!known_tabs_.empty() && now.size() == known_tabs_.size() + 1) {
      for (const auto& entry : rendered_rows) {
        if (!known_tabs_.count(entry.second)) {
          added.push_back(entry.first);
        }
      }
    }
    known_tabs_ = std::move(now);
    if (added.size() == 1) {
      AnimateRowIn(added[0]);
      slide_after = added[0];
    }
  }

  // The New tab button is part of the LIST, not the panel: it sits directly
  // under the last tab and scrolls with it, rather than anchoring the bottom
  // edge of the sidebar where the workspace bar now lives.
  tab_list_container_
      ->AddChildView(std::make_unique<ZephyrusNewTabButton>(base::BindRepeating(
          [](ZephyrusSidebarView* self) {
            // kNewTabButton: this IS the new-tab button in Zephyrus -- the
            // horizontal strip that would otherwise carry one is hidden.
            chrome::NewTab(self->browser_view_->browser(),
                           NewTabTypes::kNewTabButton);
          },
          base::Unretained(this))))
      ->SetContained(contained);

  // After the New tab button exists, so it slides down with the rows.
  if (slide_after) {
    SlideRowsBelow(tab_list_container_, slide_after,
                   slide_after->GetPreferredSize().height() + kRowSpacing);
  }

  // Same beats, same data: whatever moved the tab list (a switch, a
  // navigation, a workspace change) also moved the address, the back/forward
  // states and the pinned set.
  RebuildCompactChrome();
}

// Rounds the outside of each RUN of rows and nothing in between.
//
// A run ends at anything that is not a plain row -- a section header, or a
// split card, which is its own object with its own corners. Done here rather
// than in the rows because a row cannot see its neighbours, and the same walk
// serves the favourites list.
// static
void ZephyrusSidebarView::AssignSegmentPositions(views::View* container) {
  if (!container) {
    return;
  }
  const auto& children = container->children();
  for (size_t i = 0; i < children.size(); ++i) {
    auto* row = views::AsViewClass<ZephyrusTabRow>(children[i]);
    if (!row) {
      continue;
    }
    const bool first =
        i == 0 || !views::AsViewClass<ZephyrusTabRow>(children[i - 1]);
    const bool last = i + 1 == children.size() ||
                      !views::AsViewClass<ZephyrusTabRow>(children[i + 1]);
    row->SetSegmentPosition(first, last);
  }
}

void ZephyrusSidebarView::TabGroupedStateChanged(
    TabStripModel* tab_strip_model,
    std::optional<tab_groups::TabGroupId> old_group,
    std::optional<tab_groups::TabGroupId> new_group,
    tabs::TabInterface* tab,
    int index) {
  if (revealed_) {
    ScheduleRebuildTabList();
  }
}

void ZephyrusSidebarView::OnTabGroupChanged(const TabGroupChange& change) {
  if (revealed_) {
    ScheduleRebuildTabList();
  }
}

std::vector<tab_groups::TabGroupId> ZephyrusSidebarView::SetsInCurrentWorkspace()
    const {
  std::vector<tab_groups::TabGroupId> sets;
  ZephyrusWorkspaceManager* manager =
      browser_view_->zephyrus_workspace_manager();
  TabGroupModel* model = tab_strip_model_->group_model();
  for (const tab_groups::TabGroupId& id : model->ListTabGroups()) {
    const gfx::Range tabs = model->GetTabGroup(id)->ListTabs();
    bool here = !manager;
    for (size_t i = tabs.start(); i < tabs.end() && !here; ++i) {
      content::WebContents* contents =
          tab_strip_model_->GetWebContentsAt(static_cast<int>(i));
      here = contents && manager->IsContentsInCurrentWorkspace(contents);
    }
    if (here) {
      sets.push_back(id);
    }
  }
  return sets;
}

ZephyrusSidebarView* FindZephyrusSidebar(BrowserView* browser_view) {
  if (!browser_view) {
    return nullptr;
  }
  for (views::View* child : browser_view->children()) {
    if (auto* sidebar = views::AsViewClass<ZephyrusSidebarView>(child)) {
      return sidebar;
    }
  }
  return nullptr;
}

ZephyrusSetInfo GetZephyrusSetInfo(PrefService* prefs,
                                   const tab_groups::TabGroupId& id) {
  const SetMeta meta = ReadSetMeta(prefs, id);
  ZephyrusSetInfo info;
  info.icon = meta.icon;
  info.pinned = meta.pinned;
  info.collapsed = meta.collapsed;
  return info;
}

void SetZephyrusSetOrder(PrefService* prefs,
                         const std::vector<tab_groups::TabGroupId>& shown) {
  std::vector<std::string> tokens;
  for (const tab_groups::TabGroupId& id : shown) {
    tokens.push_back(id.ToString());
  }
  std::vector<std::string> rest;
  for (const base::Value& value : prefs->GetList(kSetsOrderPref)) {
    if (value.is_string() &&
        std::find(tokens.begin(), tokens.end(), value.GetString()) ==
            tokens.end()) {
      rest.push_back(value.GetString());
    }
  }
  ScopedListPrefUpdate update(prefs, kSetsOrderPref);
  update->clear();
  for (const std::string& token : tokens) {
    update->Append(token);
  }
  for (const std::string& token : rest) {
    update->Append(token);
  }
}

std::vector<tab_groups::TabGroupId> OrderZephyrusSets(
    PrefService* prefs,
    std::vector<tab_groups::TabGroupId> sets) {
  sets = OrderedSets(prefs, std::move(sets));
  std::stable_partition(sets.begin(), sets.end(),
                        [&](const tab_groups::TabGroupId& id) {
                          return ReadSetMeta(prefs, id).pinned;
                        });
  return sets;
}

std::vector<ZephyrusSidebarView::SetSummary> ZephyrusSidebarView::SummariseSets()
    const {
  std::vector<SetSummary> summaries;
  ZephyrusWorkspaceManager* manager =
      browser_view_->zephyrus_workspace_manager();
  PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
  TabGroupModel* model = tab_strip_model_->group_model();
  for (const tab_groups::TabGroupId& id : SetsInCurrentWorkspace()) {
    SetSummary summary;
    summary.name = GetSetTitle(tab_strip_model_, id);
    summary.icon = ReadSetMeta(prefs, id).icon;
    const gfx::Range tabs = model->GetTabGroup(id)->ListTabs();
    for (size_t i = tabs.start(); i < tabs.end(); ++i) {
      content::WebContents* contents =
          tab_strip_model_->GetWebContentsAt(static_cast<int>(i));
      if (contents && (!manager || manager->IsContentsInCurrentWorkspace(contents))) {
        summary.tabs.push_back(contents);
      }
    }
    summaries.push_back(std::move(summary));
  }
  return summaries;
}

std::string ZephyrusSidebarView::GroupTabsIntoSet(
    const std::vector<content::WebContents*>& tabs,
    const std::u16string& name,
    const std::string& icon) {
  std::u16string title;
  base::TrimWhitespace(name, base::TRIM_ALL, &title);
  if (title.size() > kMaxSetNameLength) {
    title.resize(kMaxSetNameLength);
  }
  if (title.empty()) {
    return "the set needs a name";
  }
  // Every tab is resolved by identity now, not trusted from earlier: a tab may
  // have closed, moved or been pinned since the agent listed them.
  std::vector<int> indices;
  for (content::WebContents* contents : tabs) {
    const int index =
        contents ? tab_strip_model_->GetIndexOfWebContents(contents)
                 : TabStripModel::kNoTab;
    if (index == TabStripModel::kNoTab) {
      return "a tab is no longer open; call tabs.list again";
    }
    if (tab_strip_model_->IsTabPinned(index)) {
      return "a pinned tab cannot join a set; leave it out";
    }
    indices.push_back(index);
  }
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
  if (indices.empty()) {
    return "no tabs were given";
  }

  PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
  std::optional<tab_groups::TabGroupId> target;
  for (const tab_groups::TabGroupId& id : SetsInCurrentWorkspace()) {
    if (base::EqualsCaseInsensitiveASCII(GetSetTitle(tab_strip_model_, id),
                                         title)) {
      target = id;
      break;
    }
  }
  if (target) {
    tab_strip_model_->AddToExistingGroup(indices, *target);
  } else {
    const size_t number =
        tab_strip_model_->group_model()->ListTabGroups().size();
    const tab_groups::TabGroupId id = tab_strip_model_->AddToNewGroup(indices);
    const auto color =
        static_cast<tab_groups::TabGroupColorId>(1 + (number % 8));
    tab_strip_model_->group_model()->GetTabGroup(id)->SetVisualData(
        tab_groups::TabGroupVisualData(title, color), /*is_customized=*/true);
    AppendToSetOrder(prefs, id);
    target = id;
  }
  // An icon is a nicety: one that is not in the set is ignored, not an error.
  if (!icon.empty() &&
      zephyrus::FindWorkspaceIcon(std::string_view(icon)) != nullptr) {
    SetMeta meta = ReadSetMeta(prefs, *target);
    meta.icon = icon;
    WriteSetMeta(prefs, *target, meta);
  }
  ScheduleRebuildTabList();
  return std::string();
}

void ZephyrusSidebarView::CreateSetWithTab(int index) {
  if (index == TabStripModel::kNoTab || index >= tab_strip_model_->count() ||
      tab_strip_model_->IsTabPinned(index)) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
  const size_t number = tab_strip_model_->group_model()->ListTabGroups().size();
  const tab_groups::TabGroupId id = tab_strip_model_->AddToNewGroup({index});
  // Cycled through the colours after grey, so two sets are not alike.
  const auto color =
      static_cast<tab_groups::TabGroupColorId>(1 + (number % 8));
  tab_strip_model_->group_model()->GetTabGroup(id)->SetVisualData(
      tab_groups::TabGroupVisualData(
          u"Set " + base::NumberToString16(number + 1), color));
  AppendToSetOrder(browser_view_->browser()->GetProfile()->GetPrefs(), id);
  // Grouping moved the tab; find it again by identity.
  const int now = tab_strip_model_->GetIndexOfWebContents(contents);
  if (now != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(now);
  }
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::UngroupSet(const tab_groups::TabGroupId& set) {
  TabGroupModel* model = tab_strip_model_->group_model();
  const std::vector<tab_groups::TabGroupId> all = model->ListTabGroups();
  if (std::find(all.begin(), all.end(), set) == all.end()) {
    return;
  }
  const gfx::Range tabs = model->GetTabGroup(set)->ListTabs();
  std::vector<int> indices;
  for (size_t i = tabs.start(); i < tabs.end(); ++i) {
    indices.push_back(static_cast<int>(i));
  }
  tab_strip_model_->RemoveFromGroup(indices);
  ForgetSet(browser_view_->browser()->GetProfile()->GetPrefs(), set);
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::CloseSet(const tab_groups::TabGroupId& set) {
  TabGroupModel* model = tab_strip_model_->group_model();
  const std::vector<tab_groups::TabGroupId> all = model->ListTabGroups();
  if (std::find(all.begin(), all.end(), set) == all.end()) {
    return;
  }
  const gfx::Range tabs = model->GetTabGroup(set)->ListTabs();
  for (size_t i = tabs.end(); i > tabs.start(); --i) {
    tab_strip_model_->CloseWebContentsAt(static_cast<int>(i - 1),
                                         CLOSE_USER_GESTURE);
  }
  ForgetSet(browser_view_->browser()->GetProfile()->GetPrefs(), set);
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::JoinSet(base::WeakPtr<content::WebContents> contents,
                                  tab_groups::TabGroupId set) {
  if (!contents) {
    return;
  }
  const int index = tab_strip_model_->GetIndexOfWebContents(contents.get());
  if (index == TabStripModel::kNoTab || tab_strip_model_->IsTabPinned(index) ||
      tab_strip_model_->GetTabGroupForTab(index)) {
    return;
  }
  const std::vector<tab_groups::TabGroupId> all =
      tab_strip_model_->group_model()->ListTabGroups();
  if (std::find(all.begin(), all.end(), set) == all.end()) {
    return;
  }
  tab_strip_model_->AddToExistingGroup({index}, set);
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::SetSetCollapsed(const tab_groups::TabGroupId& set,
                                          bool collapsed) {
  PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
  SetMeta meta = ReadSetMeta(prefs, set);
  meta.collapsed = collapsed;
  WriteSetMeta(prefs, set, meta);
}

void ZephyrusSidebarView::ToggleSetPinned(const tab_groups::TabGroupId& set) {
  PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
  SetMeta meta = ReadSetMeta(prefs, set);
  meta.pinned = !meta.pinned;
  WriteSetMeta(prefs, set, meta);
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::RenameSet(const tab_groups::TabGroupId& set,
                                    const std::u16string& name) {
  TabGroupModel* model = tab_strip_model_->group_model();
  const std::vector<tab_groups::TabGroupId> all = model->ListTabGroups();
  if (name.empty() || std::find(all.begin(), all.end(), set) == all.end()) {
    return;
  }
  TabGroup* group = model->GetTabGroup(set);
  const tab_groups::TabGroupVisualData* data = group->visual_data();
  group->SetVisualData(
      tab_groups::TabGroupVisualData(
          name, data ? data->color() : tab_groups::TabGroupColorId::kBlue),
      /*is_customized=*/true);
}

void ZephyrusSidebarView::ChangeSetIcon(const tab_groups::TabGroupId& set,
                                        const std::u16string& key) {
  PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
  SetMeta meta = ReadSetMeta(prefs, set);
  meta.icon = base::UTF16ToASCII(key);
  WriteSetMeta(prefs, set, meta);
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::NewTabInSet(const tab_groups::TabGroupId& set) {
  TabGroupModel* model = tab_strip_model_->group_model();
  const std::vector<tab_groups::TabGroupId> all = model->ListTabGroups();
  if (std::find(all.begin(), all.end(), set) == all.end()) {
    return;
  }
  // Straight into the set, after its last tab: not through the New tab button's
  // path, which opens a loose tab.
  const gfx::Range tabs = model->GetTabGroup(set)->ListTabs();
  if (tabs.is_empty()) {
    return;
  }
  chrome::AddTabAt(browser_view_->browser(), GURL(chrome::kChromeUINewTabURL),
                   static_cast<int>(tabs.end()), /*foreground=*/true, set);
}

void ZephyrusSidebarView::ShowSetIconPicker(const tab_groups::TabGroupId& set) {
  ZephyrusSetSection* section = FindSetSection(tab_list_container_, set);
  if (!section) {
    return;
  }
  if (views::Widget* picker = zephyrus::ShowIconPicker(
          section->header(),
          base::BindRepeating(&ZephyrusSidebarView::ChangeSetIcon,
                              weak_factory_.GetWeakPtr(), set))) {
    // Held until the picker closes. Taken before a menu that chose it releases
    // its own, so the count never reaches zero between.
    ++reveal_holds_;
    picker->widget_delegate()->RegisterWindowClosingCallback(
        base::BindOnce(&ZephyrusSidebarView::ReleaseRevealHold,
                       weak_factory_.GetWeakPtr()));
  }
}

void ZephyrusSidebarView::SetRenameActive(bool active) {
  rename_active_ = active;
  // The panel stays out while a name is being typed.
  if (active) {
    ++reveal_holds_;
    return;
  }
  ReleaseRevealHold();
  if (rebuild_deferred_) {
    rebuild_deferred_ = false;
    ScheduleRebuildTabList();
  }
}

void ZephyrusSidebarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  // A row was right-clicked, or the panel's empty space was. The tab-specific
  // items only apply in the first case; the sidebar-level ones always apply.
  // A set's header.
  if (auto* header = views::AsViewClass<ZephyrusSetHeader>(source)) {
    context_menu_set_ = header->set_id();
    context_menu_contents_ = nullptr;
    PrefService* prefs = browser_view_->browser()->GetProfile()->GetPrefs();
    const SetMeta meta = ReadSetMeta(prefs, *context_menu_set_);
    context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    context_menu_model_->AddItem(kRenameSetCommand, u"Rename");
    context_menu_model_->AddItem(kChangeSetIconCommand, u"Change icon\u2026");
    context_menu_model_->AddItem(kPinSetCommand,
                                 meta.pinned ? u"Unpin from top" : u"Pin to top");
    context_menu_model_->AddItem(kNewTabInSetCommand, u"New tab in set");
    context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
    context_menu_model_->AddItem(kCloseSetCommand, u"Close set");
    context_menu_model_->AddItem(kUngroupSetCommand, u"Ungroup set");
    // The panel stays out for as long as the menu is up: the cursor is off it
    // (in the menu) and it would otherwise tuck away under the menu.
    ++reveal_holds_;
    context_menu_runner_ = std::make_unique<views::MenuRunner>(
        context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU,
        base::BindRepeating(&ZephyrusSidebarView::ReleaseRevealHold,
                            weak_factory_.GetWeakPtr()));
    context_menu_runner_->RunMenuAt(
        GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
        views::MenuAnchorPosition::kTopLeft, source_type);
    return;
  }
  auto* row = views::AsViewClass<ZephyrusTabRow>(source);
  int row_index = -1;
  if (row) {
    row_index = row->model_index();
    if (row_index < 0 || row_index >= tab_strip_model_->count()) {
      return;
    }
  }
  // Remember the tab itself, not its index — see the member's comment. Cleared
  // when the panel rather than a row was the source, so no stale tab from a
  // previous menu can be acted on.
  context_menu_contents_ =
      row ? tab_strip_model_->GetWebContentsAt(row_index) : nullptr;
  if (row && !context_menu_contents_) {
    return;
  }

  ZephyrusWorkspaceManager* manager =
      browser_view_->zephyrus_workspace_manager();
  context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  context_menu_workspace_ids_.clear();
  context_menu_set_ids_.clear();
  context_menu_set_.reset();

  if (row && manager && manager->workspaces().size() > 1) {
    const int current_ws =
        manager->GetWorkspaceForContents(context_menu_contents_);
    workspace_submenu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
    for (const ZephyrusWorkspaceManager::Workspace& ws :
         manager->workspaces()) {
      const size_t idx = context_menu_workspace_ids_.size();
      context_menu_workspace_ids_.push_back(ws.id);
      // Named as the strip names it. An unnamed workspace was an EMPTY menu
      // item, and `emoji` now usually holds an icon KEY ("work"), which was
      // printed raw in front of the name. Only a real emoji is shown.
      std::u16string label =
          ws.name.empty() ? u"Workspace " + base::NumberToString16(idx + 1)
                          : ws.name;
      if (!ws.emoji.empty() && !zephyrus::FindWorkspaceIcon(ws.emoji)) {
        label = ws.emoji + u"  " + label;
      }
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
  if (row) {
    // Pinning is PER WORKSPACE (see IsContentsInCurrentWorkspace): a tab is
    // one WebContents in one cookie jar. The label said "(all workspaces)",
    // promising a pinned tab would follow the user -- signed in -- into every
    // other workspace. It never did, and should not.
    const bool tab_pinned = tab_strip_model_->IsTabPinned(row_index);
    // Sets. A pinned tab is in none, and cannot be added to one.
    if (!tab_pinned) {
      const std::optional<tab_groups::TabGroupId> current_set =
          tab_strip_model_->GetTabGroupForTab(row_index);
      set_submenu_model_ = std::make_unique<ui::SimpleMenuModel>(this);
      for (const tab_groups::TabGroupId& id : SetsInCurrentWorkspace()) {
        if (id == current_set) {
          continue;
        }
        set_submenu_model_->AddItem(
            kAddToSetBase + static_cast<int>(context_menu_set_ids_.size()),
            GetSetTitle(tab_strip_model_, id));
        context_menu_set_ids_.push_back(id);
      }
      if (!context_menu_set_ids_.empty()) {
        set_submenu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
      }
      set_submenu_model_->AddItem(kNewSetCommand, u"New set");
      context_menu_model_->AddSubMenu(kAddToSetSubmenu, u"Add to set",
                                      set_submenu_model_.get());
      if (current_set) {
        context_menu_model_->AddItem(kRemoveFromSetCommand, u"Remove from set");
      }
    }
    context_menu_model_->AddItem(kPinTabCommand,
                                 tab_pinned ? u"Unpin tab" : u"Pin tab");
    context_menu_model_->AddItem(kCloseTabCommand, u"Close tab");
    context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
  // Sidebar-level, always present.
  context_menu_model_->AddCheckItem(
      kPinSidebarCommand, pinned_ ? u"Unpin sidebar" : u"Pin sidebar");
  // A discoverable way back to the default for anyone who resized the panel and
  // does not know about double-clicking the edge.
  context_menu_model_->AddItem(kResetSidebarWidthCommand,
                               u"Reset sidebar width");

  ++reveal_holds_;
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU,
      base::BindRepeating(&ZephyrusSidebarView::ReleaseRevealHold,
                          weak_factory_.GetWeakPtr()));
  context_menu_runner_->RunMenuAt(
      GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
      views::MenuAnchorPosition::kTopLeft, source_type);
}

bool ZephyrusSidebarView::IsCommandIdChecked(int command_id) const {
  return command_id == kPinSidebarCommand && pinned_;
}

bool ZephyrusSidebarView::IsCommandIdEnabled(int command_id) const {
  return true;
}

void ZephyrusSidebarView::ExecuteCommand(int command_id, int event_flags) {
  // Handled first, and deliberately ABOVE the tab lookup below: pinning the
  // sidebar has nothing to do with the tab the menu was opened on, so it must
  // not be dropped by that early-return.
  if (command_id == kResetSidebarWidthCommand) {
    ResetWidthToDefault();
    return;
  }
  if (command_id == kPinSidebarCommand) {
    TogglePinned();
    return;
  }
  if (command_id >= kUngroupSetCommand && command_id <= kCloseSetCommand) {
    if (!context_menu_set_) {
      return;
    }
    const tab_groups::TabGroupId set = *context_menu_set_;
    switch (command_id) {
      case kUngroupSetCommand:
        UngroupSet(set);
        break;
      case kCloseSetCommand:
        CloseSet(set);
        break;
      case kPinSetCommand:
        ToggleSetPinned(set);
        break;
      case kNewTabInSetCommand:
        NewTabInSet(set);
        break;
      case kRenameSetCommand:
        if (ZephyrusSetSection* section =
                FindSetSection(tab_list_container_, set)) {
          section->header()->BeginRename();
        }
        break;
      case kChangeSetIconCommand:
        ShowSetIconPicker(set);
        break;
    }
    return;
  }
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
    tab_strip_model_->CloseWebContentsAt(index, CLOSE_USER_GESTURE);
    return;
  }
  if (command_id == kPinTabCommand) {
    // SetTabPinned moves the tab to the strip's pinned section and returns its
    // new index, so nothing else may rely on `index` after this.
    tab_strip_model_->SetTabPinned(index, !tab_strip_model_->IsTabPinned(index));
    return;
  }
  if (command_id == kNewSetCommand) {
    CreateSetWithTab(index);
    return;
  }
  if (command_id == kRemoveFromSetCommand) {
    tab_strip_model_->RemoveFromGroup({index});
    return;
  }
  if (command_id >= kAddToSetBase) {
    const size_t pos = static_cast<size_t>(command_id - kAddToSetBase);
    if (pos < context_menu_set_ids_.size()) {
      content::WebContents* contents = context_menu_contents_;
      tab_strip_model_->AddToExistingGroup({index}, context_menu_set_ids_[pos]);
      const int now = tab_strip_model_->GetIndexOfWebContents(contents);
      if (now != TabStripModel::kNoTab) {
        tab_strip_model_->ActivateTabAt(now);
      }
    }
    return;
  }
  if (command_id >= kMoveToWorkspaceBase) {
    // A set lives in one workspace: a tab moving out leaves its set.
    if (tab_strip_model_->GetTabGroupForTab(index)) {
      tab_strip_model_->RemoveFromGroup({index});
    }
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

void ZephyrusSidebarView::ActivateTab(
    base::WeakPtr<content::WebContents> contents) {
  const int model_index =
      contents ? tab_strip_model_->GetIndexOfWebContents(contents.get())
               : TabStripModel::kNoTab;
  if (model_index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(model_index);
  }
}

void ZephyrusSidebarView::ToggleTabMuted(
    base::WeakPtr<content::WebContents> tab) {
  content::WebContents* contents = tab.get();
  const int model_index =
      contents ? tab_strip_model_->GetIndexOfWebContents(contents)
               : TabStripModel::kNoTab;
  if (model_index == TabStripModel::kNoTab) {
    return;
  }
  contents->SetAudioMuted(!contents->IsAudioMuted());
  // A POSTED rebuild, never a synchronous one: that would destroy the very
  // button whose click callback is still on the stack. It used to patch the
  // row in place, found by matching a remembered model index -- stale after a
  // reorder, and blind to rows inside a split card, which are not direct
  // children of the list.
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::CloseTab(
    base::WeakPtr<content::WebContents> contents) {
  const int model_index =
      contents ? tab_strip_model_->GetIndexOfWebContents(contents.get())
               : TabStripModel::kNoTab;
  if (model_index != TabStripModel::kNoTab) {
    tab_strip_model_->CloseWebContentsAt(model_index, CLOSE_USER_GESTURE);
  }
}

void ZephyrusSidebarView::ExecuteBrowserCommand(int command) {
  chrome::ExecuteCommand(browser_view_->browser(), command);
}


SkColor ZephyrusSidebarView::GetZephyrusBase() const {
  // BrowserView pushes the window's base color through SetZephyrusColor(), so
  // the sidebar picks up the Private Workspace variant automatically rather
  // than hardcoding the normal theme.
  return page_color_.value_or(
      BrowserView::ZephyrusGround(browser_view_ && browser_view_->GetIncognito()));
}

SkColor ZephyrusSidebarView::GetForegroundColor() const {
  // onSurface from THIS window's colour provider. The legacy ink below is
  // process-wide: a window wearing its own workspace theme (light, say, while
  // another window's workspace is dark) filled its rows from M3 roles but
  // wrote their titles in the other palette's ink. The legacy value is only
  // the fallback for the moment before the view has a provider.
  if (GetColorProvider()) {
    return zephyrus::m3::Role(*this, kColorZephyrusOnSurface);
  }
  // Ink. Every other sidebar color (row hover/active fills, section headings,
  // the quick-actions card, favicon fallbacks) is derived from this by alpha,
  // so they all follow from this one value.
  //
  // This deliberately does NOT use GetColorWithMaxContrast(). On the warm light
  // theme max contrast returns pure black, and black on cream is exactly the
  // clinical look the palette exists to avoid -- the warmth lives in the ink
  // being wine, not in the background alone. Derived alphas stay warm for the
  // same reason: a 12% wine wash tints toward the ground, where a 12% black
  // wash just greys it.
  //
  // Private Workspace shifts the ink with its ground rather than keeping wine,
  // so the mode reads as a mode and not as a mis-set colour.
  return BrowserView::ZephyrusInk(browser_view_ &&
                                  browser_view_->GetIncognito());
}

SkColor ZephyrusSidebarView::GetPanelColor() const {
  // surfaceContainer from this window's provider -- the surface the rows'
  // surfaceContainerHigh steps up from -- for the same reason as the ink
  // above: the legacy Surface() is one colour for every window.
  if (!page_color_.has_value()) {
    return GetColorProvider()
               ? zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainer)
               : zephyrus::Surface();
  }
  // The theme base, flat. The sidebar reads as the same surface as the title
  // bar above it, exactly as a vertical tab strip does — the thing that
  // separates it from the page is the page's own edge sitting against it, not a
  // fill of its own.
  //
  // This was a lift of 0x1A, and before that frosted glass over the page. Both
  // were trying to make the sidebar a distinct surface. It should not be one:
  // the structure inside it comes from the quick-actions card and the row
  // highlights, which carry their own alpha over this base.
  return GetZephyrusBase();
}

void ZephyrusSidebarView::SetZephyrusColor(std::optional<SkColor> page_color) {
  if (page_color_ == page_color) {
    return;
  }
  page_color_ = page_color;
  SetBackground(
      views::CreateRoundedRectBackground(GetPanelColor(), PanelRadius()));
  // Row recolor only matters when visible; Reveal() rebuilds with the latest
  // colors anyway.
  if (revealed_) {
    RebuildTabList();
  }
}

int ZephyrusSidebarView::GetSidebarWidth() const {
  if (drag_width_) {
    return *drag_width_;
  }
  // GetOriginalProfile(), not profile(): in a Private Workspace window this is
  // an off-the-record profile whose pref overlay is thrown away with the
  // session, so a resize made there would silently forget itself -- the same
  // trap documented for zephyrus.private_workspace.lock_enabled. Reading and
  // writing the original profile also means the width is one setting across
  // regular and private windows, which is what a user resizing a panel
  // expects.
  const PrefService* prefs =
      browser_view_->browser()->GetProfile()->GetOriginalProfile()->GetPrefs();
  // Clamped on READ, not just on write. The pref is a plain integer in
  // Preferences that a user can edit, sync can deliver, and an older build
  // could have written under a different range -- and a sidebar stuck at 2px
  // or 3000px is unrecoverable through the UI, because the handle that would
  // fix it is off screen or too small to grab.
  return std::clamp(prefs->GetInteger(kWidthPrefName), kMinSidebarWidth,
                    kMaxSidebarWidth);
}

void ZephyrusSidebarView::OnResizeDragged(int new_width) {
  const int clamped =
      std::clamp(new_width, kMinSidebarWidth, kMaxSidebarWidth);
  if (drag_width_ == clamped) {
    return;
  }
  drag_width_ = clamped;
  resizing_ = true;

  // The panel's own bounds come from BrowserView's layout, which also reserves
  // the column the page is inset by. Invalidating there rather than calling
  // SetBounds() here is what keeps the two edges of the seam in step: one
  // layout pass moves both, using one width.
  browser_view_->InvalidateLayout();
}

void ZephyrusSidebarView::OnResizeFinished() {
  if (!drag_width_) {
    return;
  }
  resizing_ = false;
  const int final_width = *drag_width_;
  // Cleared BEFORE the pref write so GetSidebarWidth() reads the persisted
  // value from here on; leaving it set would pin the width to a drag that is
  // over, and the next pref change would not be reflected.
  drag_width_.reset();
  browser_view_->browser()->GetProfile()->GetOriginalProfile()->GetPrefs()->SetInteger(
      kWidthPrefName, final_width);
  browser_view_->InvalidateLayout();
}

void ZephyrusSidebarView::ResetWidthToDefault() {
  drag_width_.reset();
  browser_view_->browser()->GetProfile()->GetOriginalProfile()->GetPrefs()->SetInteger(
      kWidthPrefName, kDefaultSidebarWidth);
  browser_view_->InvalidateLayout();
}

BEGIN_METADATA(ZephyrusSidebarView)
END_METADATA

ZephyrusSidebarResizeHandle::ZephyrusSidebarResizeHandle(
    base::RepeatingCallback<int()> current_width,
    base::RepeatingCallback<void(int)> on_width,
    base::RepeatingClosure on_finished,
    base::RepeatingClosure on_reset)
    : current_width_(std::move(current_width)),
      on_width_(std::move(on_width)),
      on_finished_(std::move(on_finished)),
      on_reset_(std::move(on_reset)) {}

ZephyrusSidebarResizeHandle::~ZephyrusSidebarResizeHandle() = default;

ui::Cursor ZephyrusSidebarResizeHandle::GetCursor(const ui::MouseEvent& event) {
  return ui::Cursor(ui::mojom::CursorType::kEastWestResize);
}

bool ZephyrusSidebarResizeHandle::OnMousePressed(const ui::MouseEvent& event) {
  if (!event.IsOnlyLeftMouseButton()) {
    return false;
  }
  // Double-click restores the default width -- the standard splitter gesture,
  // and the only way back for someone who dragged the panel narrow and does
  // not know the context menu has a reset item.
  if (event.GetClickCount() == 2) {
    on_reset_.Run();
    return true;
  }
  press_root_x_ = event.root_location().x();
  start_width_ = current_width_.Run();
  return true;
}

bool ZephyrusSidebarResizeHandle::OnMouseDragged(const ui::MouseEvent& event) {
  on_width_.Run(start_width_ + event.root_location().x() - press_root_x_);
  return true;
}

void ZephyrusSidebarResizeHandle::OnMouseReleased(
    const ui::MouseEvent& event) {
  on_finished_.Run();
}

// Capture can be taken away mid-drag (alt-tab, a system dialog). Without this
// the pending width would never reach the pref and the panel would silently
// revert on the next launch.
void ZephyrusSidebarResizeHandle::OnMouseCaptureLost() {
  on_finished_.Run();
}

BEGIN_METADATA(ZephyrusSidebarResizeHandle)
END_METADATA

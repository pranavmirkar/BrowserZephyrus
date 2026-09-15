// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_sidebar_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_window_backdrop.h"


#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_private_workspace.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/task/sequenced_task_runner.h"
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
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_manager.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/display/screen.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/color_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/cursor/cursor.h"
#include "ui/views/view_class_properties.h"
#include "ui/base/cursor/mojom/cursor_type.mojom-shared.h"
#include "ui/compositor/layer.h"
#include "base/i18n/case_conversion.h"
#include "cc/paint/paint_flags.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/canvas.h"
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
#include "ui/views/mouse_watcher_view_host.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace {

// Effectively unbounded: ScrollView::ClipHeightTo needs a real maximum.
constexpr int kUnboundedScrollHeight = 100000;

constexpr int kRowHeight = 36;
// Tab rows were full pills (kRowHeight / 2) while the action rows next to them
// used the system's 10 — two different row shapes in one panel. Both are 10
// now, matching every other Zephyrus row (workspace dropdown, Shield toggles).
constexpr int kRowCornerRadius = zephyrus::kCornerRadius;

// Square. The sidebar is not a card any more — it is a flush column of window
// chrome running from the toolbar to the bottom edge, so there is no free side
// for a corner to round against. Was 18 when it floated over the page, then 8
// briefly when it was still being treated as a panel.
constexpr int kPanelCornerRadius = 0;
constexpr int kFaviconSize = 16;
constexpr int kRowSpacing = 3;

// Sidebar row context-menu command ids.
constexpr int kCloseTabCommand = 1;
constexpr int kMoveToWorkspaceSubmenu = 2;
// Zephyrus hides the horizontal tab strip, so this menu is the only place a
// tab can be pinned — there is no strip to right-click.
constexpr int kPinTabCommand = 3;
constexpr int kPinSidebarCommand = 4;
constexpr int kResetSidebarWidthCommand = 5;
constexpr int kMoveToWorkspaceBase = 100;  // + workspace index
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
constexpr base::TimeDelta kSlideInDuration = base::Milliseconds(210);
constexpr base::TimeDelta kSlideOutDuration = base::Milliseconds(200);

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
  SkColor active_fg;   // Ink ON the active row -- it is inverted, so this is
                       // the ground colour, not the ink.
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
std::unique_ptr<views::View> MakeSectionHeader(const std::u16string& text,
                                               SkColor foreground) {
  auto header = std::make_unique<views::Label>(base::i18n::ToUpper(text));
  header->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  header->SetEnabledColor(SkColorSetA(foreground, 0x8C));
  header->SetAutoColorReadabilityEnabled(false);
  header->SetSubpixelRenderingEnabled(false);
  // label-small. These are the uppercase section headings ("Favorites"), which
  // is exactly the utilitarian role M3 sizes label-small for.
  header->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelSmall));
  header->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(14, 12, 5, 12)));
  return header;
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
  void SetRowInk(SkColor on_row) {
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(views::kCloseIcon, on_row,
                                                 kGlyph));
    const SkColor on_accent = zephyrus::Current().accent_ink;
    SetImageModel(views::Button::STATE_HOVERED,
                  ui::ImageModel::FromVectorIcon(views::kCloseIcon, on_accent,
                                                 kGlyph));
    SetImageModel(views::Button::STATE_PRESSED,
                  ui::ImageModel::FromVectorIcon(views::kCloseIcon, on_accent,
                                                 kGlyph));
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == STATE_HOVERED;
    const bool pressed = GetState() == STATE_PRESSED;
    if (!hovered && !pressed) {
      return;
    }
    SkColor fill = zephyrus::Accent();
    if (pressed) {
      // Darker on press. The accent is already the most saturated thing in the
      // panel, so lightening it would read as losing focus.
      fill = color_utils::AlphaBlend(SK_ColorBLACK, fill, SkAlpha{0x2E});
    }
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
};

BEGIN_METADATA(ZephyrusTabCloseButton)
END_METADATA

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

    close_button_ = AddChildView(std::make_unique<ZephyrusTabCloseButton>(
        base::BindRepeating(
            [](base::RepeatingClosure cb, const ui::Event&) { cb.Run(); },
            std::move(close_callback))));
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
    const float lift =
        static_cast<float>(gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT_3, t));

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
    settings.SetTweenType(gfx::Tween::EASE_OUT_3);
    layer()->SetTransform(gfx::Transform());
    // Back to solid: the tab has returned to the list.
    layer()->SetOpacity(1.f);
  }

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
        vector_icons::kGlobeIcon,
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
  // The ACTIVE row INVERTS instead. In a palette with one accent and no second
  // colour to spend, value is what is left to say "this one" -- and inverting
  // is unambiguous at a glance in a way that a slightly different grey is not.
  // It is also why the row has to hand its label a different ink: text that
  // stays dark on a now-dark row disappears.
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const bool hovered = GetState() == views::Button::STATE_HOVERED ||
                         GetState() == views::Button::STATE_PRESSED;
    if (!is_active_ && !hovered) {
      return;
    }

    gfx::RectF body(GetLocalBounds());
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(is_active_ ? colors_.active_bg : colors_.hover_bg);
    canvas->DrawRoundRect(body, kRowCornerRadius, fill);

    if (is_active_) {
      // A short accent bar on the leading edge. This is the "you are here"
      // mark that the inversion used to carry -- the same role the accent
      // plays on a focus ring, and it costs the row nothing: it sits in the
      // margin rather than under any content.
      constexpr float kBarWidth = 3.f;
      constexpr float kBarInsetY = 7.f;
      gfx::RectF bar(body.x(), body.y() + kBarInsetY, kBarWidth,
                     std::max(0.f, body.height() - 2 * kBarInsetY));
      cc::PaintFlags accent;
      accent.setAntiAlias(true);
      accent.setStyle(cc::PaintFlags::kFill_Style);
      accent.setColor(zephyrus::Accent());
      canvas->DrawRoundRect(bar, kBarWidth / 2.f, accent);
    }

    if (!is_active_) {
      // Hover carries a hairline so the row reads as a defined object rather
      // than a smudge of tint. The active row does not need one -- an inverted
      // block has its own edge.
      cc::PaintFlags stroke;
      stroke.setAntiAlias(true);
      stroke.setStyle(cc::PaintFlags::kStroke_Style);
      stroke.setStrokeWidth(zephyrus::kHairline);
      stroke.setColor(SkColorSetA(colors_.foreground, 0x33));
      gfx::RectF outline = body;
      outline.Inset(zephyrus::kHairline / 2.f);
      canvas->DrawRoundRect(outline, kRowCornerRadius, stroke);
    }
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
    ApplyFallbackIconTint();
    SchedulePaint();
  }


  bool is_active_;
  int model_index_;
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
    const float eased = static_cast<float>(
        gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT_3, t));
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
  raw_ptr<views::ImageButton> audio_button_ = nullptr;
  raw_ptr<ZephyrusTabCloseButton> close_button_ = nullptr;
};

BEGIN_METADATA(ZephyrusTabRow)
END_METADATA

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
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(5, 4), 0));
    // A raised surface rather than an outline: the rows inside already carry
    // their own hover and selection fills, and a border around them would be a
    // third rectangle competing with those two. Lifted from 0x14 to 0x1F --
    // at the lower value the card was so close to the panel behind it that the
    // two tabs read as loose rows that happened to be adjacent.
    SetBackground(views::CreateRoundedRectBackground(
        SkColorSetA(foreground, 0x1F), 12));
    break_callback_ = std::move(break_callback);
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

    auto* icon = AddChildView(std::make_unique<views::ImageView>());
    if (!favicon.isNull()) {
      icon->SetImage(ui::ImageModel::FromImageSkia(favicon));
    }
    icon->SetImageSize(gfx::Size(16, 16));

    auto* label = AddChildView(std::make_unique<views::Label>(title));
    label->SetEnabledColor(foreground);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetSubpixelRenderingEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    layout->SetFlexForView(label, 1);
  }
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
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kPanelCornerRadius));
  SetBackground(
      views::CreateRoundedRectBackground(GetPanelColor(), kPanelCornerRadius));

  auto* box_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(10), kRowSpacing));

  // Quick-actions card REMOVED.
  //
  // It held Open Private Workspace / Downloads / History. All three are still
  // reachable -- the settings popup rail lists Downloads and History, and
  // Private Workspace has Ctrl+Shift+N -- so the card was duplicating
  // navigation rather than providing it, at the cost of the top third of the
  // panel on every window.
  //
  // `fg_ink` stays: the sections below it still need the row ink.
  const SkColor fg_ink = GetForegroundColor();

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

  // Zephyrus product version, quiet at the foot of the sidebar.
  auto* version = AddChildView(std::make_unique<views::Label>(
      u"Zephyrus " + std::u16string(zephyrus::kVersion)));
  version->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  version->SetAutoColorReadabilityEnabled(false);
  version->SetSubpixelRenderingEnabled(false);
  // on-surface-variant, not a hand-rolled 40% of the ink.
  //
  // 0x66 of the foreground is M3's DISABLED strength territory, and this label
  // is not disabled -- it is secondary. At 12px on a light surface it fell
  // below readable contrast, which is what made it look broken rather than
  // quiet. M3 has a role for exactly this and it stays legible on both themes.
  version->SetEnabledColor(zephyrus::Muted());
  version->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelSmall));
  version->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(6, 0, 2, 0)));

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
}

void ZephyrusSidebarView::OnPaintBackground(gfx::Canvas* canvas) {
  // The desktop blur belongs to DWM, outside Chromium's compositor. A Views
  // backdrop filter here can only sample the window's own layers.
  if (!zephyrus::HasWindowBackdrop(GetWidget())) {
    views::View::OnPaintBackground(canvas);
  }
}

void ZephyrusSidebarView::OnThemeChanged() {
  views::View::OnThemeChanged();
  RebuildTabList();
}

void ZephyrusSidebarView::MouseMovedOutOfHost() {
  if (pinned_) {
    return;
  }
  TuckAway();
}

void ZephyrusSidebarView::Reveal() {
  if (revealed_) {
    return;
  }
  revealed_ = true;
  reveal_poll_timer_.Stop();
  browser_view_->SetZephyrusSidebarAttached(true);
  // Zero duration under reduced motion, matching the layer slide below, which
  // has always honoured it. Without this the panel would snap into place while
  // the page's edge kept easing open behind it — reduced motion half-applied
  // looks more broken than not applying it at all.
  // EASE_OUT_3, matching the drag and settle animations. Two different
  // ease-out variants inside one component is the kind of incoherence that
  // makes an interface feel assembled rather than authored.
  reveal_animation_.SetTweenType(gfx::Tween::EASE_OUT_3);
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
  pinned_ = !pinned_;
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
  if (!revealed_ || pinned_ || resizing_ || dragging_row_) {
    return;
  }
  revealed_ = false;
  SetCanProcessEventsWithinSubtree(false);

  reveal_poll_timer_.Start(FROM_HERE, base::Milliseconds(100), this,
                           &ZephyrusSidebarView::OnRevealPoll);
  // EASE_OUT_3, matching the drag and settle animations. Two different
  // ease-out variants inside one component is the kind of incoherence that
  // makes an interface feel assembled rather than authored.
  reveal_animation_.SetTweenType(gfx::Tween::EASE_OUT_3);
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
  // Clearing the pin here is the one real resize of the interaction, spent with
  // the panel stationary.
  browser_view_->UpdateZephyrusSidebarPin();
  browser_view_->InvalidateLayout();
}

void ZephyrusSidebarView::OnRevealPoll() {
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

  // While tucked away the list isn't visible and Reveal() rebuilds it anyway,
  // so skip the (comparatively expensive) row churn during background tab
  // activity — this keeps the hidden sidebar at zero cost.
  if (!revealed_) {
    return;
  }
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::OnTabPinnedStateChanged(tabs::TabInterface* tab,
                                                  int index) {
  if (!revealed_) {
    return;
  }
  // A newly pinned tab becomes visible in every workspace, so the list has to
  // be rebuilt rather than just refreshed in place.
  ScheduleRebuildTabList();
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
  if (dragging_row_) {
    rebuild_deferred_ = true;
    return;
  }

  // Posting lets the input event unwind first.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusSidebarView::RebuildTabList,
                                weak_factory_.GetWeakPtr()));
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
        std::make_unique<ZephyrusSplitDropIndicator>(GetZephyrusBase()));
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
  settings.SetTweenType(gfx::Tween::EASE_OUT_3);
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
  // Over the page there is no slot to open a gap for, so the list is left
  // alone -- reordering underneath a gesture that is no longer about order
  // would rearrange tabs the user never meant to touch.
  if (over_content) {
    return;
  }

  const auto& children = tab_list_container_->children();
  const auto it = std::find(children.begin(), children.end(), row);
  if (it == children.end()) {
    return;
  }
  const size_t self = static_cast<size_t>(it - children.begin());

  // The container interleaves section headers ("Pinned tabs", "Tabs") with
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
  std::vector<std::pair<views::View*, float>> before;
  before.reserve(children.size());
  for (views::View* child : children) {
    float visual_y = static_cast<float>(child->bounds().y());
    if (child->layer()) {
      visual_y += child->layer()->transform().To2dTranslation().y();
    }
    before.emplace_back(child, visual_y);
  }

  tab_list_container_->ReorderChildView(row, target);
  tab_list_container_->DeprecatedLayoutImmediately();

  for (const auto& [child, old_y] : before) {
    // The dragged row is driven by the cursor, not by this.
    if (child == row) {
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

    // INVERT -- instantly, outside any animation scope.
    gfx::Transform offset;
    offset.Translate(0, delta);
    child->layer()->SetTransform(offset);

    // PLAY. IMMEDIATELY_ANIMATE_TO_NEW_TARGET is the load-bearing setting: a
    // drag crosses boundaries repeatedly, and the default strategy would queue
    // each reorder behind the last. Rows have to RETARGET from wherever they
    // currently are, the same reason CSS transitions beat keyframes for
    // anything the user can trigger rapidly.
    ui::ScopedLayerAnimationSettings settings(child->layer()->GetAnimator());
    settings.SetPreemptionStrategy(
        ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
    // 180ms: inside the 150-250ms band for this size of movement. Slower and
    // the list feels like it is wading; faster and the reorder is invisible,
    // which defeats the point of animating it at all.
    settings.SetTransitionDuration(base::Milliseconds(180));
    settings.SetTweenType(gfx::Tween::EASE_OUT_3);
    child->layer()->SetTransform(gfx::Transform());
  }
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
    std::vector<ZephyrusTabRow*> visual;
    for (views::View* child : tab_list_container_->children()) {
      if (auto* r = views::AsViewClass<ZephyrusTabRow>(child)) {
        if (r->model_index() >= 0 &&
            tab_strip_model_->IsTabPinned(r->model_index()) ==
                tab_strip_model_->IsTabPinned(from)) {
          visual.push_back(r);
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
  DestroyDragProxy();
  drag_contents_ = nullptr;
  rebuild_deferred_ = false;
  ScheduleRebuildTabList();
}

void ZephyrusSidebarView::RebuildTabList() {
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
  // What replaces the inversion as the "this one" marker is the accent bar in
  // OnPaintBackground: a mark of its own rather than a colour every child has
  // to compensate for.
  const ZephyrusRowColors row_colors{
      .foreground = fg,
      .active_bg = zephyrus::Raise(zephyrus::Surface(), 0x38),
      .hover_bg = zephyrus::Surface(),
      .active_fg = fg,
  };

  ZephyrusWorkspaceManager* workspace_manager =
      browser_view_->zephyrus_workspace_manager();
  const int active_index = tab_strip_model_->active_index();

  // Adds one row for the tab at `index`. Shared by both sections below.
  auto add_row_into = [&](views::View* parent, int index) {
    content::WebContents* contents = tab_strip_model_->GetWebContentsAt(index);
    auto* row = parent->AddChildView(
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

  const SkColor header_ink = GetForegroundColor();
  if (!pinned.empty()) {
    tab_list_container_->AddChildView(
        MakeSectionHeader(u"Pinned tabs", header_ink));
    for (int index : pinned) {
      if (consumed.count(index)) {
        continue;
      }
      if (!add_split_card(index)) {
        add_row(index);
      }
    }
  }
  if (!unpinned.empty()) {
    tab_list_container_->AddChildView(MakeSectionHeader(u"Tabs", header_ink));
    for (int index : unpinned) {
      if (consumed.count(index)) {
        continue;
      }
      if (!add_split_card(index)) {
        add_row(index);
      }
    }
  }
}

void ZephyrusSidebarView::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  // A row was right-clicked, or the panel's empty space was. The tab-specific
  // items only apply in the first case; the sidebar-level ones always apply.
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

  if (row && manager && manager->workspaces().size() > 1) {
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
  if (row) {
    // Pinned tabs are global: they stay visible in every workspace.
    const bool tab_pinned = tab_strip_model_->IsTabPinned(row_index);
    context_menu_model_->AddItem(
        kPinTabCommand,
        tab_pinned ? u"Unpin tab" : u"Pin tab (all workspaces)");
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

  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      context_menu_model_.get(), views::MenuRunner::CONTEXT_MENU);
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


SkColor ZephyrusSidebarView::GetZephyrusBase() const {
  // BrowserView pushes the window's base color through SetZephyrusColor(), so
  // the sidebar picks up the Private Workspace variant automatically rather
  // than hardcoding the normal theme.
  return page_color_.value_or(
      BrowserView::ZephyrusGround(browser_view_ && browser_view_->GetIncognito()));
}

SkColor ZephyrusSidebarView::GetForegroundColor() const {
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
  // The panel is the cream SURFACE, not the ground. On the warm theme the two
  // differ by one warm step, which is enough to separate the tab list from the
  // page beside it without a border doing the work.
  if (!page_color_.has_value()) {
    return zephyrus::Surface();
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
      views::CreateRoundedRectBackground(GetPanelColor(), kPanelCornerRadius));
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
      browser_view_->browser()->profile()->GetOriginalProfile()->GetPrefs();
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
  browser_view_->browser()->profile()->GetOriginalProfile()->GetPrefs()->SetInteger(
      kWidthPrefName, final_width);
  browser_view_->InvalidateLayout();
}

void ZephyrusSidebarView::ResetWidthToDefault() {
  drag_width_.reset();
  browser_view_->browser()->profile()->GetOriginalProfile()->GetPrefs()->SetInteger(
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

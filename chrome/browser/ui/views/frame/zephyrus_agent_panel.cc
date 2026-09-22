// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_agent_panel.h"

#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/zephyrus/agent/dev_model_client.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/progress_bar.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_class_properties.h"

namespace zephyrus::agent {
namespace {

// Everything in the sheet sits this far in from its edges -- M3's side sheet
// inset.
constexpr int kInset = 16;
constexpr int kLineGap = 12;

// An M3 standard icon button: a 40dp target around a 24dp icon.
constexpr int kIconButtonSize = 40;
constexpr int kIconSize = 24;

// The input: an M3 search-bar-shaped field, full-round, with the send button
// inside its trailing end.
constexpr int kInputHeight = 48;

// Bubbles and cards in the log: M3's large shape.
constexpr int kBubbleRadius = zephyrus::kRadiusLarge;

// The user's own message never runs the full width of the sheet, the way a sent
// message never does -- that is most of what makes it read as "mine".
constexpr int kTaskBubbleMaxWidth =
    ZephyrusAgentPanel::kDefaultWidth - 2 * kInset - 56;

// M3's linear progress indicator is 4dp.
constexpr int kProgressHeight = 4;

// How close to the end of the log still counts as "at the end". A little
// slack, so a user who scrolled down by hand and stopped just short is not
// treated as having scrolled away.
constexpr int kFollowSlack = 24;

// How many steps a task started from here may take.
//
// Small on purpose. At roughly four seconds a step, a budget of twenty is over
// a minute of a browser doing things on its own, which is longer than anyone
// will watch without wondering whether it has hung.
// Twelve was too few for any real task, and a wasted step is expensive: at
// roughly forty seconds of model time each, the budget IS the time limit.
//
// Raised, but not far. The answer to a slow loop is fewer wasted steps -- the
// repeat guard, verification that reports what actually happened, an
// Observation worth reading -- not more of them.
constexpr uint32_t kMaxSteps = 20;

// Lines kept in the log.
//
// Generous, because the log scrolls: this is only here so a very long session
// cannot grow views without bound. An earlier version capped at 14 to survive
// without scrolling, and that silently ate the first half of a conversation
// while leaving most of the panel empty.
constexpr size_t kMaxLines = 300;

// Upper bound handed to ClipHeightTo. Any large number works; what matters is
// that a bound EXISTS.
constexpr int kUnboundedHeight = 100000;

// The plain text out of a task.complete / task.ask payload.
//
// The loop hands these back as the tool's own JSON, which is right for a model
// reading it and wrong for a person: the panel was showing
// {"answer":"..."} verbatim. Falls back to the raw string, because an ugly
// answer beats a missing one.
std::string PlainAnswer(const std::string& value_json) {
  std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(value_json, base::JSON_PARSE_RFC);
  if (!parsed) {
    return value_json;
  }
  for (const char* field : {"answer", "question"}) {
    if (const std::string* text = parsed->FindString(field)) {
      return *text;
    }
  }
  return value_json;
}

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        zephyrus::m3::Type type) {
  auto label = std::make_unique<views::Label>(text);
  label->SetFontList(zephyrus::m3::Font(type));
  label->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  label->SetAutoColorReadabilityEnabled(false);
  // These labels sit on transparent or rounded fills: subpixel text needs an
  // opaque background of its own, and DCHECKs without one.
  label->SetSubpixelRenderingEnabled(false);
  return label;
}

// Ink drop in the M3 state-layer opacities. ONCE, at construction: SetMode()
// destroys the button's ink drop, and the send button's colours change from
// inside its own click (stop -> Cancel() -> OnTaskFinished() -> ApplyRoles()).
// Re-installing there would free the ink drop the click is animating. Colour
// changes go through SetBaseColor() alone.
void InstallStateLayer(views::View* view) {
  views::InkDropHost* const ink = views::InkDrop::Get(view);
  ink->SetMode(views::InkDropHost::InkDropMode::ON);
  ink->SetHighlightOpacity(zephyrus::m3::kHover / 255.0f);
  ink->SetVisibleOpacity(zephyrus::m3::kPressed / 255.0f);
}

// One line of the log, which colours ITSELF on a theme change. The panel used
// to colour lines once, at creation, from a palette table -- so every line
// already on screen kept the old theme's ink after the theme moved.
class LogLine : public views::View {
  METADATA_HEADER(LogLine, views::View)

 public:
  LogLine(const std::u16string& text, ZephyrusAgentPanel::LineKind kind)
      : kind_(kind) {
    using Kind = ZephyrusAgentPanel::LineKind;
    switch (kind) {
      case Kind::kTask: {
        // A trailing bubble at its own width, like a sent message.
        SetLayoutManager(std::make_unique<views::BoxLayout>(
                             views::BoxLayout::Orientation::kHorizontal))
            ->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kEnd);
        bubble_ = AddChildView(std::make_unique<views::View>());
        bubble_->SetLayoutManager(std::make_unique<views::FillLayout>());
        bubble_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(10, 14)));
        label_ = bubble_->AddChildView(
            MakeLabel(text, zephyrus::m3::Type::kBodyLarge));
        label_->SetMultiLine(true);
        label_->SetMaximumWidth(kTaskBubbleMaxWidth - 28);
        break;
      }
      case Kind::kAnswer: {
        // The full width, on a filled card: the line the user came for.
        SetLayoutManager(std::make_unique<views::FillLayout>());
        bubble_ = AddChildView(std::make_unique<views::View>());
        bubble_->SetLayoutManager(std::make_unique<views::FillLayout>());
        bubble_->SetBorder(views::CreateEmptyBorder(gfx::Insets(14)));
        label_ = bubble_->AddChildView(
            MakeLabel(text, zephyrus::m3::Type::kBodyLarge));
        label_->SetMultiLine(true);
        break;
      }
      case Kind::kStep: {
        SetLayoutManager(std::make_unique<views::FillLayout>());
        SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 4)));
        label_ =
            AddChildView(MakeLabel(text, zephyrus::m3::Type::kBodyMedium));
        label_->SetMultiLine(true);
        break;
      }
    }
  }
  LogLine(const LogLine&) = delete;
  LogLine& operator=(const LogLine&) = delete;
  ~LogLine() override = default;

  // views::View:
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    using Kind = ZephyrusAgentPanel::LineKind;
    switch (kind_) {
      case Kind::kTask:
        bubble_->SetBackground(views::CreateRoundedRectBackground(
            zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer),
            kBubbleRadius));
        label_->SetEnabledColor(
            zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer));
        break;
      case Kind::kAnswer:
        bubble_->SetBackground(views::CreateRoundedRectBackground(
            zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest),
            kBubbleRadius));
        label_->SetEnabledColor(
            zephyrus::m3::Role(*this, kColorZephyrusOnSurface));
        break;
      case Kind::kStep:
        label_->SetEnabledColor(
            zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant));
        break;
    }
  }

 private:
  const ZephyrusAgentPanel::LineKind kind_;
  raw_ptr<views::View> bubble_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
};

BEGIN_METADATA(LogLine)
END_METADATA

// An M3 button: labelLarge, 40dp, full-round. A subclass only because
// LabelButton's label() is protected and the M3 type scale is a FontList, so
// there is no public way to set it. The fill and ink are set by the panel's
// ApplyRoles(), which is where the roles are readable.
class M3Button : public views::LabelButton {
  METADATA_HEADER(M3Button, views::LabelButton)

 public:
  M3Button(PressedCallback callback, const std::u16string& text)
      : views::LabelButton(std::move(callback), text) {
    label()->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge));
  }
  M3Button(const M3Button&) = delete;
  M3Button& operator=(const M3Button&) = delete;
  ~M3Button() override = default;
};

BEGIN_METADATA(M3Button)
END_METADATA

std::unique_ptr<views::LabelButton> MakeM3Button(
    views::Button::PressedCallback callback,
    const std::u16string& text,
    int horizontal_padding) {
  auto button = std::make_unique<M3Button>(std::move(callback), text);
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetMinSize(gfx::Size(0, 40));
  button->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(0, horizontal_padding)));
  button->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
  button->SetInstallFocusRingOnFocus(true);
  views::InstallPillHighlightPathGenerator(button.get());
  InstallStateLayer(button.get());
  return button;
}

}  // namespace

// Jump to the latest line: a small round button over the end of the log, shown
// while the user is scrolled away from it, carrying how many lines arrived
// since. WhatsApp's pattern, drawn as an M3 small FAB (40dp on
// surfaceContainerHighest with a soft elevation shadow) and a `primary` count
// badge.
class JumpToLatestButton : public views::Button {
  METADATA_HEADER(JumpToLatestButton, views::Button)

 public:
  explicit JumpToLatestButton(PressedCallback callback)
      : views::Button(std::move(callback)) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetTooltipText(u"Jump to latest");
    GetViewAccessibility().SetName(u"Jump to latest");
    SetInstallFocusRingOnFocus(true);
    // The ring follows the circle, not the padded box around it that exists
    // for the shadow and the badge.
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(kPad),
                                                  kCircle / 2);
  }
  JumpToLatestButton(const JumpToLatestButton&) = delete;
  JumpToLatestButton& operator=(const JumpToLatestButton&) = delete;
  ~JumpToLatestButton() override = default;

  void SetUnread(int count) {
    if (count == unread_) {
      return;
    }
    unread_ = count;
    GetViewAccessibility().SetName(
        count > 0 ? u"Jump to latest, " + base::NumberToString16(count) +
                        u" new"
                  : std::u16string(u"Jump to latest"));
    SchedulePaint();
  }

  // Fades in rather than popping, so its arrival reads as "something new" and
  // not as a flicker.
  void Reveal() {
    layer()->SetOpacity(0.0f);
    SetVisible(true);
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(base::Milliseconds(150));
    settings.SetTweenType(gfx::Tween::LINEAR_OUT_SLOW_IN);
    layer()->SetOpacity(1.0f);
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kCircle + 2 * kPad, kCircle + 2 * kPad);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    // Every colour is a role, and a role needs a Widget.
    if (!GetWidget()) {
      return;
    }
    const gfx::RectF circle(kPad, kPad, kCircle, kCircle);
    const SkColor ink = zephyrus::m3::Role(*this, kColorZephyrusOnSurface);

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    // Elevation: the one thing that says this floats OVER the log rather than
    // sitting in it.
    flags.setLooper(gfx::CreateShadowDrawLooper({
        gfx::ShadowValue(gfx::Vector2d(0, 1), 3,
                         SkColorSetA(SK_ColorBLACK, 0x4D)),
        gfx::ShadowValue(gfx::Vector2d(0, 3), 8,
                         SkColorSetA(SK_ColorBLACK, 0x26)),
    }));
    flags.setColor(
        zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest));
    canvas->DrawCircle(circle.CenterPoint(), kCircle / 2.0f, flags);
    flags.setLooper(nullptr);

    if (GetEnabled()) {
      const bool pressed = GetState() == STATE_PRESSED;
      const bool hovered = GetState() == STATE_HOVERED;
      if (pressed || hovered || HasFocus()) {
        flags.setColor(zephyrus::m3::StateLayer(
            ink, pressed   ? zephyrus::m3::kPressed
                 : hovered ? zephyrus::m3::kHover
                           : zephyrus::m3::kFocus));
        canvas->DrawCircle(circle.CenterPoint(), kCircle / 2.0f, flags);
      }
    }

    const gfx::ImageSkia arrow =
        gfx::CreateVectorIcon(kArrowDownwardIcon, kIconSize, ink);
    canvas->DrawImageInt(arrow, kPad + (kCircle - kIconSize) / 2,
                         kPad + (kCircle - kIconSize) / 2);

    if (unread_ <= 0) {
      return;
    }
    // The count, on the circle's top trailing shoulder.
    const std::u16string text =
        unread_ > 99 ? std::u16string(u"99+") : base::NumberToString16(unread_);
    const gfx::FontList font =
        zephyrus::m3::Font(zephyrus::m3::Type::kLabelSmall);
    constexpr int kBadgeHeight = 18;
    const int badge_width =
        std::max(kBadgeHeight, gfx::GetStringWidth(text, font) + 10);
    const gfx::Rect badge = GetMirroredRect(
        gfx::Rect(kPad + kCircle - 8 - badge_width / 2, 0, badge_width,
                  kBadgeHeight));
    flags.setColor(zephyrus::m3::Role(*this, kColorZephyrusPrimary));
    canvas->DrawRoundRect(gfx::RectF(badge), kBadgeHeight / 2.0f, flags);
    canvas->DrawStringRectWithFlags(
        text, font, zephyrus::m3::Role(*this, kColorZephyrusOnPrimary), badge,
        gfx::Canvas::TEXT_ALIGN_CENTER);
  }

  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
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

 private:
  // An M3 small FAB is 40dp; the padding around it holds the shadow and the
  // badge, which the button's own bounds would otherwise clip.
  static constexpr int kCircle = 40;
  static constexpr int kPad = 8;

  int unread_ = 0;
};

BEGIN_METADATA(JumpToLatestButton)
END_METADATA

ZephyrusAgentPanel::ZephyrusAgentPanel(BrowserView* browser_view)
    : browser_view_(browser_view) {
  SetVisible(false);

  // Children carry their own horizontal inset where they need one: the log's
  // scrollbar should reach the sheet's edge, the bubbles should not.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(0, 0, kInset, 0), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // ---- Header: spark, title, close -----------------------------------------
  auto* header = AddChildView(std::make_unique<views::BoxLayoutView>());
  header->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  header->SetInsideBorderInsets(gfx::Insets::TLBR(12, kInset, 8, 8));
  header->SetBetweenChildSpacing(12);
  header->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  header_icon_ = header->AddChildView(std::make_unique<views::ImageView>());
  title_ = header->AddChildView(
      MakeLabel(u"Agent", zephyrus::m3::Type::kTitleMedium));
  header->SetFlexForView(title_, 1);
  close_button_ = header->AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&ZephyrusAgentPanel::Close,
                          base::Unretained(this))));
  close_button_->SetPreferredSize(gfx::Size(kIconButtonSize, kIconButtonSize));
  close_button_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  close_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  close_button_->GetViewAccessibility().SetName(u"Close agent");
  close_button_->SetTooltipText(u"Close");
  views::InstallCircleHighlightPathGenerator(close_button_);
  InstallStateLayer(close_button_);

  // ---- Progress: M3's linear indicator, only while a task runs -------------
  //
  // Indeterminate, because the loop cannot know how many steps are left. The
  // ROW keeps its 4dp either way, so the log does not jump when a task starts
  // and stops.
  auto* progress_row = AddChildView(std::make_unique<views::View>());
  progress_row->SetLayoutManager(std::make_unique<views::FillLayout>());
  progress_row->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::VH(0, kInset)));
  progress_row->SetPreferredSize(gfx::Size(0, kProgressHeight));
  progress_ = progress_row->AddChildView(std::make_unique<views::ProgressBar>());
  progress_->SetPreferredHeight(kProgressHeight);
  progress_->SetValue(-1);  // Indeterminate.
  progress_->GetViewAccessibility().SetName(u"Agent is working");
  progress_->SetVisible(false);

  // ---- Empty state ----------------------------------------------------------
  //
  // An empty column read as broken rather than as ready. Hidden for good once
  // the first line lands.
  auto* empty = AddChildView(std::make_unique<views::BoxLayoutView>());
  empty->SetOrientation(views::BoxLayout::Orientation::kVertical);
  empty->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kCenter);
  empty->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kCenter);
  empty->SetInsideBorderInsets(gfx::Insets::VH(0, 32));
  empty->SetBetweenChildSpacing(12);
  empty_state_ = empty;
  empty_icon_ = empty->AddChildView(std::make_unique<views::ImageView>());
  empty_label_ = empty->AddChildView(MakeLabel(
      u"Ask the agent to do something on the page you're looking at.",
      zephyrus::m3::Type::kBodyMedium));
  empty_label_->SetMultiLine(true);
  empty_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  empty_label_->SetMaximumWidth(kDefaultWidth - 64);
  layout->SetFlexForView(empty_state_, 1);

  // ---- The log --------------------------------------------------------------
  //
  // The log takes whatever height is left, so the input stays pinned to the
  // bottom however long the conversation gets.
  //
  // **ClipHeightTo is the load-bearing call, and leaving it out is why two
  // earlier attempts at this rendered nothing at all.**
  //
  // ScrollView::Layout only sizes its contents to the viewport width inside
  // `if (is_bounded() && contents_)`, and `is_bounded()` is simply
  // `max_height_ >= 0 && min_height_ >= 0` -- which nothing but ClipHeightTo
  // sets. Without it the contents keep their PREFERRED width, and a multi-line
  // Label's preferred width is its whole unwrapped text, so the column laid
  // itself out far wider than the panel and every line sat off-screen to the
  // right. The panel still drew its title, input and button, so it looked alive
  // with an empty log.
  log_scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  log_scroll_->ClipHeightTo(0, kUnboundedHeight);
  log_scroll_->SetDrawOverflowIndicator(false);
  log_scroll_->SetBackgroundColor(std::nullopt);
  log_scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  log_scroll_->SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(
      views::ScrollBar::Orientation::kVertical));
  log_scroll_->SetVisible(false);

  auto log = std::make_unique<views::BoxLayoutView>();
  log->SetOrientation(views::BoxLayout::Orientation::kVertical);
  log->SetInsideBorderInsets(gfx::Insets::TLBR(12, kInset, 12, kInset));
  log->SetBetweenChildSpacing(kLineGap);
  log->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
  log_ = log_scroll_->SetContents(std::move(log));
  layout->SetFlexForView(log_scroll_, 1);
  log_scrolled_subscription_ = log_scroll_->AddContentsScrolledCallback(
      base::BindRepeating(&ZephyrusAgentPanel::OnLogScrolled,
                          base::Unretained(this)));

  // ---- The approval card ----------------------------------------------------
  //
  // Built once and hidden, rather than created per question, so a question
  // cannot arrive while its own view is still being constructed.
  approval_ = AddChildView(std::make_unique<views::BoxLayoutView>());
  approval_->SetOrientation(views::BoxLayout::Orientation::kVertical);
  approval_->SetInsideBorderInsets(gfx::Insets(kInset));
  approval_->SetBetweenChildSpacing(8);
  approval_->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  approval_->SetProperty(views::kMarginsKey,
                         gfx::Insets::TLBR(0, kInset, 12, kInset));
  approval_->SetVisible(false);

  auto* approval_head =
      approval_->AddChildView(std::make_unique<views::BoxLayoutView>());
  approval_head->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  approval_head->SetBetweenChildSpacing(8);
  approval_head->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  approval_icon_ =
      approval_head->AddChildView(std::make_unique<views::ImageView>());
  approval_title_ = approval_head->AddChildView(
      MakeLabel(u"Allow this step?", zephyrus::m3::Type::kTitleSmall));

  approval_reason_ = approval_->AddChildView(
      MakeLabel(std::u16string(), zephyrus::m3::Type::kBodyMedium));
  approval_reason_->SetMultiLine(true);

  // Text button then FILLED button, trailing: one emphasised action per view,
  // and it is the one that lets the step happen -- the quiet "Not now" is still
  // the safe default for anyone who reads nothing.
  auto* buttons =
      approval_->AddChildView(std::make_unique<views::BoxLayoutView>());
  buttons->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  buttons->SetBetweenChildSpacing(8);
  buttons->SetMainAxisAlignment(views::BoxLayout::MainAxisAlignment::kEnd);
  decline_button_ = buttons->AddChildView(MakeM3Button(
      base::BindRepeating(&ZephyrusAgentPanel::AnswerApproval,
                          base::Unretained(this), false),
      u"Not now", 12));
  allow_button_ = buttons->AddChildView(MakeM3Button(
      base::BindRepeating(&ZephyrusAgentPanel::AnswerApproval,
                          base::Unretained(this), true),
      u"Allow once", 24));

  // ---- Input: a full-round field with the send button inside it -----------
  //
  // Two ways in on purpose -- if Enter is swallowed by something up the view
  // tree there is still a way to submit, and a visible button says the panel
  // is for typing into.
  auto* input_bar = AddChildView(std::make_unique<views::BoxLayoutView>());
  input_bar->SetOrientation(views::BoxLayout::Orientation::kHorizontal);
  input_bar->SetInsideBorderInsets(gfx::Insets::TLBR(4, 20, 4, 4));
  input_bar->SetBetweenChildSpacing(8);
  input_bar->SetCrossAxisAlignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  input_bar->SetMinimumCrossAxisSize(kInputHeight);
  input_bar->SetProperty(views::kMarginsKey,
                         gfx::Insets::TLBR(0, kInset, 0, kInset));
  input_bar_ = input_bar;

  input_ = input_bar->AddChildView(std::make_unique<views::Textfield>());
  input_->set_controller(this);
  input_->SetPlaceholderText(u"Ask the agent to do something");
  input_->GetViewAccessibility().SetName(u"Agent task");
  input_->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kBodyLarge));
  // The bar is the field: no box of the textfield's own inside it.
  input_->SetBorder(views::NullBorder());
  input_->SetBackgroundEnabled(false);
  input_->RemoveHoverEffect();
  input_->SetTextColorId(kColorZephyrusOnSurface);
  input_->SetPlaceholderTextColorId(kColorZephyrusOnSurfaceVariant);
  input_bar->SetFlexForView(input_, 1);

  send_button_ = input_bar->AddChildView(std::make_unique<views::ImageButton>(
      base::BindRepeating(&ZephyrusAgentPanel::OnSendOrStop,
                          base::Unretained(this))));
  send_button_->SetPreferredSize(gfx::Size(kIconButtonSize, kIconButtonSize));
  send_button_->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
  send_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
  send_button_->GetViewAccessibility().SetName(u"Send");
  send_button_->SetTooltipText(u"Send");
  views::InstallCircleHighlightPathGenerator(send_button_);
  InstallStateLayer(send_button_);

  // Last child, so it paints over the log; placed by hand in Layout().
  jump_button_ = AddChildView(std::make_unique<JumpToLatestButton>(
      base::BindRepeating(&ZephyrusAgentPanel::ScrollLogToLatest,
                          base::Unretained(this))));
  jump_button_->SetProperty(views::kViewIgnoredByLayoutKey, true);
  jump_button_->SetVisible(false);
}

ZephyrusAgentPanel::~ZephyrusAgentPanel() {
  // A question left on screen when the panel goes away is still a question the
  // task is waiting on. Answering no is the only safe reading of "the window
  // closed".
  if (approval_answer_) {
    std::move(approval_answer_).Run(false);
  }
}

int ZephyrusAgentPanel::GetReservedWidth() const {
  return is_open_ ? kDefaultWidth : 0;
}

void ZephyrusAgentPanel::Toggle() {
  is_open_ ? Close() : Open();
}

void ZephyrusAgentPanel::Open() {
  // Start loading the model now, while the user is still typing.
  //
  // It is a 4.7GB file and measured 19.2s cold against 2.5s warm, and that
  // whole difference used to land after the send button -- which is most of why
  // a task took half a minute to visibly begin. Nothing here waits on it: if
  // the load is still running when the task starts, the first step simply
  // queues behind it as it did before.
  if (browser_view_ && browser_view_->browser()) {
    DevModelClient::WarmUp(
        browser_view_->browser()->profile()->GetURLLoaderFactory());
  }

  if (is_open_) {
    return;
  }
  is_open_ = true;
  SetVisible(true);
  if (browser_view_) {
    browser_view_->InvalidateLayout();
  }
  input_->RequestFocus();
}

void ZephyrusAgentPanel::Close() {
  if (!is_open_) {
    return;
  }
  is_open_ = false;
  SetVisible(false);
  if (browser_view_) {
    browser_view_->InvalidateLayout();
  }
}

bool ZephyrusAgentPanel::HandleKeyEvent(views::Textfield* sender,
                                        const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }
  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    Close();
    return true;
  }
  if (key_event.key_code() != ui::VKEY_RETURN) {
    return false;
  }

  Submit();
  return true;
}

void ZephyrusAgentPanel::Submit() {
  const std::string task = base::UTF16ToUTF8(input_->GetText());
  if (task.empty() || task_running_) {
    return;
  }
  input_->SetText(std::u16string());
  StartTask(task);
}

void ZephyrusAgentPanel::OnSendOrStop() {
  if (!task_running_) {
    Submit();
    return;
  }
  // Cancel() finishes the task synchronously, through OnTaskFinished, which
  // turns this button back into "send". Nothing here is freed by that, so
  // calling it from the button's own callback is safe.
  if (controller_) {
    controller_->Cancel();
  }
}

void ZephyrusAgentPanel::StartTask(const std::string& task) {
  AddLine(task, LineKind::kTask);

  // A fresh controller per task. It holds the kernel connection, the executor
  // and the Observation for one task's lifetime, and reusing one across tasks
  // would carry the last task's page knowledge into the next.
  controller_ = std::make_unique<ZephyrusAgentTaskController>(
      browser_view_ ? browser_view_->browser() : nullptr);
  controller_->SetDelegate(this);

  SetTaskRunning(true);
  if (!controller_->StartTaskWithConfiguredModel(
          task, kMaxSteps,
          base::BindOnce(&ZephyrusAgentPanel::OnTaskFinished,
                         base::Unretained(this)))) {
    SetTaskRunning(false);
    // Said plainly rather than left to look like a hang. This is the ordinary
    // state of a build nobody passed the development switches to.
    AddLine(
        "No model is configured. Start the browser with "
        "--zephyrus-agent-model-endpoint and --zephyrus-agent-model.",
        LineKind::kStep);
  }
}

void ZephyrusAgentPanel::OnTaskFinished(mojom::TaskOutcomePtr outcome) {
  SetTaskRunning(false);
  ClearApprovalCard();

  if (!outcome) {
    AddLine("The agent stopped without answering.", LineKind::kStep);
    return;
  }

  switch (outcome->status) {
    case mojom::TaskStatus::kCompleted:
    case mojom::TaskStatus::kAskedTheUser:
      AddLine(PlainAnswer(outcome->message), LineKind::kAnswer);
      break;
    case mojom::TaskStatus::kOutOfSteps:
      AddLine("Stopped after " + base::NumberToString(outcome->steps) +
                  " steps without finishing.",
              LineKind::kStep);
      break;
    case mojom::TaskStatus::kCancelled:
      // The controller's own message ("you stopped it") is written for a log
      // line under someone else's name; in the user's own panel, "Stopped" is
      // the whole story.
      AddLine("Stopped.", LineKind::kStep);
      break;
    case mojom::TaskStatus::kNeedsApproval:
    case mojom::TaskStatus::kFailed:
      AddLine(outcome->message, LineKind::kStep);
      break;
  }
}

void ZephyrusAgentPanel::OnAgentProgress(const std::string& line) {
  AddLine(line, LineKind::kStep);
}

void ZephyrusAgentPanel::OnAgentApprovalNeeded(
    const std::string& reason,
    const std::string& risk,
    base::OnceCallback<void(bool)> answer) {
  ShowApprovalCard(reason, std::move(answer));
}

void ZephyrusAgentPanel::AddLine(const std::string& text, LineKind kind) {
  // Asked BEFORE the line goes in, while the log's height is still the height
  // the user was looking at.
  const bool was_at_bottom = IsLogAtBottom();

  // The first line replaces the empty state for good.
  if (!log_scroll_->GetVisible()) {
    empty_state_->SetVisible(false);
    log_scroll_->SetVisible(true);
  }

  log_->AddChildView(std::make_unique<LogLine>(base::UTF8ToUTF16(text), kind));

  // The whole panel, not just the column: a new line changes how much height
  // the log wants, which is a question for the parent's layout.
  InvalidateLayout();

  while (log_->children().size() > kMaxLines) {
    log_->RemoveChildViewT(log_->children().front());
  }

  // Follow the newest line only if the user was already at the end -- and
  // always for their own task, the way sending a message takes a chat to the
  // bottom. This used to call ScrollViewToVisible() on the new line straight
  // away, before any layout had given it bounds: a view still at (0,0),
  // scrolled into view, is the TOP of the log, so every new step yanked the
  // reader up there.
  if (was_at_bottom || kind == LineKind::kTask) {
    ScrollLogToLatest();
  } else {
    ++unread_;
    UpdateJumpButton();
  }
}

bool ZephyrusAgentPanel::IsJumpToLatestVisibleForTesting() const {
  return jump_button_->GetVisible();
}

bool ZephyrusAgentPanel::IsLogAtBottom() const {
  if (!log_scroll_->GetVisible()) {
    return true;
  }
  return log_scroll_->GetVisibleRect().bottom() >=
         log_->height() - kFollowSlack;
}

void ZephyrusAgentPanel::ScrollLogToLatest() {
  // Lay out first, so the lines have real bounds and the log its real height.
  DeprecatedLayoutImmediately();
  const int max_offset =
      std::max(0, log_->height() - log_scroll_->GetVisibleRect().height());
  log_scroll_->ScrollToOffset(gfx::PointF(0, max_offset));
  unread_ = 0;
  UpdateJumpButton();
}

void ZephyrusAgentPanel::OnLogScrolled() {
  if (IsLogAtBottom()) {
    unread_ = 0;
  }
  UpdateJumpButton();
}

void ZephyrusAgentPanel::UpdateJumpButton() {
  const bool show = log_scroll_->GetVisible() && !IsLogAtBottom();
  jump_button_->SetUnread(unread_);
  if (show == jump_button_->GetVisible()) {
    return;
  }
  if (show) {
    jump_button_->Reveal();
  } else {
    jump_button_->SetVisible(false);
  }
}

void ZephyrusAgentPanel::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  // Over the end of the log, inset from its trailing edge -- above the input,
  // and above the approval card when there is one, since the log ends there.
  const gfx::Size size = jump_button_->GetPreferredSize();
  const gfx::Rect log = log_scroll_->bounds();
  jump_button_->SetBounds(log.right() - kInset + 8 - size.width(),
                          log.bottom() - 4 - size.height(), size.width(),
                          size.height());
}

void ZephyrusAgentPanel::SetTaskRunning(bool running) {
  task_running_ = running;
  progress_->SetVisible(running);
  // One button that is "send" at rest and "stop" while a task runs, the way M3
  // chat composers do: the thing you would want to press is always under the
  // same finger.
  const std::u16string name = running ? u"Stop" : u"Send";
  send_button_->GetViewAccessibility().SetName(name);
  send_button_->SetTooltipText(name);
  if (GetWidget()) {
    ApplyRoles();
  }
}

void ZephyrusAgentPanel::ShowApprovalCard(
    const std::string& reason,
    base::OnceCallback<void(bool)> answer) {
  // A second question while one is on screen would strand the first. It cannot
  // happen today -- the loop stops on the first -- but leaving the older
  // callback unanswered is the sort of thing that becomes a hang later.
  if (approval_answer_) {
    std::move(approval_answer_).Run(false);
  }
  const bool was_at_bottom = IsLogAtBottom();
  approval_answer_ = std::move(answer);
  approval_reason_->SetText(base::UTF8ToUTF16(reason));
  approval_->SetVisible(true);
  InvalidateLayout();
  // The card takes height from the log; keep the end in view if it was.
  if (was_at_bottom) {
    ScrollLogToLatest();
  }
}

void ZephyrusAgentPanel::ClearApprovalCard() {
  approval_->SetVisible(false);
  approval_answer_.Reset();
  InvalidateLayout();
}

void ZephyrusAgentPanel::AnswerApproval(bool approved) {
  if (!approval_answer_) {
    return;
  }
  base::OnceCallback<void(bool)> answer = std::move(approval_answer_);
  approval_->SetVisible(false);
  InvalidateLayout();
  AddLine(approved ? "You allowed it." : "You declined.", LineKind::kStep);
  std::move(answer).Run(approved);
}

void ZephyrusAgentPanel::OnThemeChanged() {
  views::View::OnThemeChanged();
  ApplyRoles();
}

void ZephyrusAgentPanel::ApplyRoles() {
  // M3 ROLES, read from this view: it is in the browser window's Widget
  // whenever this runs (OnThemeChanged, or SetTaskRunning after a check).
  const SkColor on_surface = zephyrus::m3::Role(*this, kColorZephyrusOnSurface);
  const SkColor on_variant =
      zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant);
  const SkColor primary = zephyrus::m3::Role(*this, kColorZephyrusPrimary);
  const SkColor on_primary = zephyrus::m3::Role(*this, kColorZephyrusOnPrimary);

  // A docked side sheet on surfaceContainerLow, shaped like the page card
  // beside it. No outline: M3 separates surfaces by tone, and the hairline
  // this used to carry was the retired language.
  SetBackground(views::CreateRoundedRectBackground(
      zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerLow),
      zephyrus::kRadiusCard));
  SetBorder(nullptr);

  header_icon_->SetImage(
      ui::ImageModel::FromVectorIcon(vector_icons::kChatSparkIcon, primary,
                                     kIconSize));
  title_->SetEnabledColor(on_surface);
  close_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(vector_icons::kCloseIcon, on_variant,
                                     kIconSize));
  views::InkDrop::Get(close_button_)->SetBaseColor(on_variant);

  // M3 linear progress: `primary` indicator on a secondaryContainer track.
  progress_->SetForegroundColor(primary);
  progress_->SetBackgroundColor(
      zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer));

  empty_icon_->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kChatSparkIcon, on_variant, 32));
  empty_label_->SetEnabledColor(on_variant);

  approval_->SetBackground(views::CreateRoundedRectBackground(
      zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest),
      kBubbleRadius));
  approval_icon_->SetImage(
      ui::ImageModel::FromVectorIcon(kZephyrusShieldIcon, primary, 20));
  approval_title_->SetEnabledColor(on_surface);
  approval_reason_->SetEnabledColor(on_variant);
  // Filled for the action, text for the way out.
  allow_button_->SetEnabledTextColors(on_primary);
  allow_button_->SetBackground(
      views::CreateRoundedRectBackground(primary, 20));
  views::InkDrop::Get(allow_button_)->SetBaseColor(on_primary);
  decline_button_->SetEnabledTextColors(primary);
  views::InkDrop::Get(decline_button_)->SetBaseColor(primary);

  input_bar_->SetBackground(views::CreateRoundedRectBackground(
      zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHighest),
      kInputHeight / 2));
  // A filled circle while it can send; a tonal one while it would stop, so the
  // stop state never looks like the primary action of the sheet.
  const SkColor send_fill =
      task_running_
          ? zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer)
          : primary;
  const SkColor send_ink =
      task_running_
          ? zephyrus::m3::Role(*this, kColorZephyrusOnSecondaryContainer)
          : on_primary;
  send_button_->SetBackground(
      views::CreateRoundedRectBackground(send_fill, kIconButtonSize / 2));
  send_button_->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromVectorIcon(
          task_running_ ? kStopCircleIcon : kArrowUpwardIcon, send_ink, 20));
  views::InkDrop::Get(send_button_)->SetBaseColor(send_ink);
}

BEGIN_METADATA(ZephyrusAgentPanel)
END_METADATA

}  // namespace zephyrus::agent

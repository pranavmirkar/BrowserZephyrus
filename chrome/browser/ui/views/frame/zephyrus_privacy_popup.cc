// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_privacy_popup.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/message_formatter.h"
#include "base/i18n/number_formatting.h"
#include "base/i18n/case_conversion.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/top_container_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3_switch.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_partition.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"
#include "chrome/browser/zephyrus/privacy/privacy_event.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service.h"
#include "chrome/browser/zephyrus/privacy/privacy_intelligence_service_factory.h"
#include "chrome/browser/zephyrus/privacy/privacy_scores.h"
#include "chrome/browser/zephyrus/privacy/privacy_tab_helper.h"
#include "chrome/grit/generated_resources.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_utils.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/native_theme/native_theme.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view_shadow.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"

namespace zephyrus_privacy {
namespace {

// -- M3 privacy card ---------------------------------------------------------
//
// A FLOATING card at the window's trailing edge, not a modal sheet. The report
// describes the page behind it, so covering that page -- which a full-height
// sheet over a scrim did -- takes away the thing the numbers are about. This
// leaves the page visible and clickable, and sizes itself to its own content.
constexpr int kCardWidth = 340;
// Clear of the window's edges on every side.
constexpr int kCardMargin = 12;
// M3's extra-large shape: the card floats, so it is rounded all round.
constexpr int kCardShape = 28;
// Everything inside the card is inset by this.
constexpr int kCardInset = 16;
// Cards inside the card: M3's large shape.
constexpr int kInnerRadius = zephyrus::kRadiusLarge;
constexpr int kInnerPadding = 16;
// An M3 elevation-3 drop shadow, which is what separates a floating surface
// from the page under it now that there is no scrim.
constexpr int kCardElevation = 3;
constexpr base::TimeDelta kCardDuration = base::Milliseconds(200);
// How far the card slides in from the trailing edge.
constexpr int kCardSlide = 24;
// An M3 standard icon button: a 40dp target around a 24dp icon.
constexpr int kIconButtonSize = 40;
constexpr int kIconSize = 24;
// The single-select segmented button across the top of the content.
constexpr int kSegmentHeight = 40;
// The score bars.
constexpr int kBarWidth = 56;
constexpr int kBarHeight = 6;
// The leading circle on a tracker row.
constexpr int kAvatarSize = 32;
// Effectively unbounded: ScrollView::ClipHeightTo needs a real maximum.
constexpr int kUnboundedScrollHeight = 100000;

// -- String bindings ---------------------------------------------------------
//
// §2.4: every user-facing string binds to an enum value, in one place. These
// switches have no `default:`, so adding a status or a category fails the build
// here rather than silently rendering as something it is not.

std::u16string StatusString(TrackerStatus status) {
  switch (status) {
    case TrackerStatus::kBlocked:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_STATUS_BLOCKED);
    case TrackerStatus::kAllowed:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_STATUS_ALLOWED);
    case TrackerStatus::kDetected:
    case TrackerStatus::kRandomized:
    case TrackerStatus::kPotential:
      // Rows only ever carry the first three today. Randomized and Potential
      // fall back to the weakest true claim rather than inventing a label.
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_STATUS_DETECTED);
  }
}

std::u16string CategoryString(Category category) {
  switch (category) {
    case Category::kAdvertising:
      return l10n_util::GetStringUTF16(
          IDS_ZEPHYRUS_PRIVACY_CATEGORY_ADVERTISING);
    case Category::kAnalytics:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_CATEGORY_ANALYTICS);
    case Category::kSocial:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_CATEGORY_SOCIAL);
    case Category::kContent:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_CATEGORY_CONTENT);
    case Category::kCdn:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_CATEGORY_CDN);
    case Category::kFingerprinting:
      return l10n_util::GetStringUTF16(
          IDS_ZEPHYRUS_PRIVACY_CATEGORY_FINGERPRINTING);
    case Category::kUnknown:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_CATEGORY_UNKNOWN);
  }
}

std::u16string ProtectedCategoryString(ProtectedCategory category) {
  switch (category) {
    case ProtectedCategory::kTrackingCookies:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_PROTECTED_COOKIES);
    case ProtectedCategory::kAdvertisingIdentifiers:
      return l10n_util::GetStringUTF16(
          IDS_ZEPHYRUS_PRIVACY_PROTECTED_ADVERTISING);
    case ProtectedCategory::kDeviceFingerprint:
      return l10n_util::GetStringUTF16(
          IDS_ZEPHYRUS_PRIVACY_PROTECTED_FINGERPRINT);
    case ProtectedCategory::kBrowsingActivity:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_PROTECTED_ACTIVITY);
    case ProtectedCategory::kReferrerInformation:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_PROTECTED_REFERRER);
  }
}

std::u16string BandString(IntensityBand band) {
  switch (band) {
    case IntensityBand::kMinimal:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_INTENSITY_MINIMAL);
    case IntensityBand::kLow:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_INTENSITY_LOW);
    case IntensityBand::kModerate:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_INTENSITY_MODERATE);
    case IntensityBand::kHigh:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_INTENSITY_HIGH);
    case IntensityBand::kSevere:
      return l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_INTENSITY_SEVERE);
  }
}

std::u16string Plural(int message_id, uint32_t count) {
  return base::i18n::MessageFormatter::FormatWithNamedArgs(
      l10n_util::GetStringUTF16(message_id), "COUNT", static_cast<int>(count));
}

// A §6.10 template that is BOTH an ICU plural and carries a name. The two
// substitution systems do not compose: GetStringFUTF16 scans for a literal
// "$2" and an ICU plural spells its count "#", so asking it to fill both
// DCHECKs — and in a Release build, where that DCHECK is compiled out, leaves
// raw "{COUNT, plural," syntax on screen.
//
// ICU runs FIRST, deliberately. An entity name arrives from the dataset, and a
// name containing a brace or a "#" would be parsed as plural syntax if it were
// already in the string when the formatter ran. Substituting it afterwards
// makes the name inert text.
std::u16string PluralNamed(int message_id,
                           const std::u16string& name,
                           uint32_t count) {
  return base::ReplaceStringPlaceholders(
      base::i18n::MessageFormatter::FormatWithNamedArgs(
          l10n_util::GetStringUTF16(message_id), "COUNT",
          static_cast<int>(count)),
      name, nullptr);
}

// -- Palette -----------------------------------------------------------------
//
// M3 ROLES. Read from the browser window, which is in a Widget: the panel is
// built before it joins one and so has no ColorProvider of its own to ask.
struct Palette {
  SkColor sheet;
  SkColor card;
  SkColor fg;
  SkColor muted;
  SkColor accent;
  // The hero: a filled card in `primaryContainer`, the one emphatic surface.
  SkColor hero;
  SkColor on_hero;
  // A score bar's unfilled track.
  SkColor track;
};

// §14.2: respect an increased-contrast preference, including Windows High
// Contrast. What fails for those users is secondary text: the whole score
// breakdown and every category label sit on onSurfaceVariant, which is quiet on
// purpose. Quiet is the wrong answer for someone who asked the system to make
// things louder, so secondary text collapses to full onSurface and the sheet
// drops to the plain surface for the most separation from its cards.
bool PrefersMoreContrast() {
  const ui::NativeTheme* theme = ui::NativeTheme::GetInstanceForNativeUi();
  // preferred_contrast() alone: it already reports kMore under Windows High
  // Contrast, and NativeTheme::IsForcedHighContrast() is protected.
  return theme && theme->preferred_contrast() !=
                      ui::NativeTheme::PreferredContrast::kNoPreference;
}

Palette MakePalette(const views::View& host) {
  Palette p;
  p.sheet = zephyrus::m3::Role(host, kColorZephyrusSurfaceContainerLow);
  p.card = zephyrus::m3::Role(host, kColorZephyrusSurfaceContainerHighest);
  p.fg = zephyrus::m3::Role(host, kColorZephyrusOnSurface);
  p.muted = zephyrus::m3::Role(host, kColorZephyrusOnSurfaceVariant);
  p.accent = zephyrus::m3::Role(host, kColorZephyrusPrimary);
  p.hero = zephyrus::m3::Role(host, kColorZephyrusPrimaryContainer);
  p.on_hero = zephyrus::m3::Role(host, kColorZephyrusOnPrimaryContainer);
  p.track = zephyrus::m3::Role(host, kColorZephyrusSurfaceContainerHighest);
  if (PrefersMoreContrast()) {
    p.muted = p.fg;
    p.sheet = zephyrus::m3::Role(host, kColorZephyrusSurface);
    p.accent = p.fg;
  }
  return p;
}

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        SkColor color,
                                        zephyrus::m3::Type type) {
  auto label = std::make_unique<views::Label>(text);
  label->SetEnabledColor(color);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetSubpixelRenderingEnabled(false);
  // ALIGN_TO_HEAD, not ALIGN_LEFT: in an RTL locale the whole panel
  // mirrors, and a hard left alignment would strand every label on the
  // wrong edge of its own row (§14).
  label->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  label->SetMultiLine(false);
  label->SetFontList(zephyrus::m3::Font(type));
  return label;
}

// A score bar: M3's linear progress indicator, at the size a table row can
// carry. Decoration only -- the row it sits in says the same thing in words.
class ScoreBar : public views::View {
  METADATA_HEADER(ScoreBar, views::View)

 public:
  ScoreBar(float fraction, SkColor indicator, SkColor track)
      : fraction_(std::clamp(fraction, 0.0f, 1.0f)),
        indicator_(indicator),
        track_(track) {
    GetViewAccessibility().SetIsIgnored(true);
  }
  ScoreBar(const ScoreBar&) = delete;
  ScoreBar& operator=(const ScoreBar&) = delete;
  ~ScoreBar() override = default;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kBarWidth, kBarHeight);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    const float radius = height() / 2.0f;
    gfx::RectF bar(GetLocalBounds());
    flags.setColor(track_);
    canvas->DrawRoundRect(bar, radius, flags);
    if (fraction_ <= 0.0f) {
      return;
    }
    bar.set_width(bar.width() * fraction_);
    // The filled part grows from the leading edge, which is the right one in
    // an RTL locale.
    if (base::i18n::IsRTL()) {
      bar.set_x(width() - bar.width());
    }
    flags.setColor(indicator_);
    canvas->DrawRoundRect(bar, radius, flags);
  }

 private:
  float fraction_;
  SkColor indicator_;
  SkColor track_;
};

BEGIN_METADATA(ScoreBar)
END_METADATA

// The leading circle on a tracker row, carrying the first letter of its name.
class Monogram : public views::View {
  METADATA_HEADER(Monogram, views::View)

 public:
  Monogram(const std::u16string& name, SkColor fill, SkColor ink)
      : fill_(fill) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    auto* label = AddChildView(std::make_unique<views::Label>(
        name.empty() ? std::u16string()
                     : base::i18n::ToUpper(name.substr(0, 1))));
    label->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge));
    label->SetEnabledColor(ink);
    label->SetBackgroundColor(fill);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  }
  Monogram(const Monogram&) = delete;
  Monogram& operator=(const Monogram&) = delete;
  ~Monogram() override = default;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kAvatarSize, kAvatarSize);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill_);
    canvas->DrawCircle(gfx::RectF(GetLocalBounds()).CenterPoint(),
                       kAvatarSize / 2.0f, flags);
    views::View::OnPaint(canvas);
  }

 private:
  SkColor fill_;
};

BEGIN_METADATA(Monogram)
END_METADATA

// A tracker row's status, as a small tonal pill: the word alone in a column of
// words was the least legible part of the list.
class StatusPill : public views::View {
  METADATA_HEADER(StatusPill, views::View)

 public:
  StatusPill(const std::u16string& text, SkColor ink, SkColor fill)
      : fill_(fill) {
    SetLayoutManager(std::make_unique<views::FillLayout>());
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 10)));
    auto* label = AddChildView(std::make_unique<views::Label>(text));
    label->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelMedium));
    label->SetEnabledColor(ink);
    label->SetBackgroundColor(fill);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  }
  StatusPill(const StatusPill&) = delete;
  StatusPill& operator=(const StatusPill&) = delete;
  ~StatusPill() override = default;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill_);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), height() / 2.0f,
                          flags);
    views::View::OnPaint(canvas);
  }

 private:
  SkColor fill_;
};

BEGIN_METADATA(StatusPill)
END_METADATA

// An M3 TEXT BUTTON: labelLarge in `primary`, 40dp tall, 12dp of padding, with
// a pill state layer. A subclass because LabelButton::label() is protected and
// the M3 type scale is a FontList rather than a views::style.
class TextButton : public views::LabelButton {
  METADATA_HEADER(TextButton, views::LabelButton)

 public:
  TextButton(PressedCallback callback,
             const std::u16string& text,
             SkColor color)
      : views::LabelButton(std::move(callback), text) {
    label()->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge));
    SetEnabledTextColors(color);
    // ALIGN_LEFT, not ALIGN_TO_HEAD: LabelButton DCHECKs against
    // ALIGN_TO_HEAD because it mirrors for RTL itself, unlike Label.
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 12)));
    SetMinSize(gfx::Size(0, 40));
    // §14.2 keyboard navigation. Explicit rather than inherited: a control that
    // is not in the focus ring is unreachable without a mouse.
    SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    SetInstallFocusRingOnFocus(true);
    views::InstallPillHighlightPathGenerator(this);
    views::InkDropHost* const ink = views::InkDrop::Get(this);
    ink->SetMode(views::InkDropHost::InkDropMode::ON);
    ink->SetBaseColor(color);
    ink->SetHighlightOpacity(zephyrus::m3::kHover / 255.0f);
    ink->SetVisibleOpacity(zephyrus::m3::kPressed / 255.0f);
  }
  TextButton(const TextButton&) = delete;
  TextButton& operator=(const TextButton&) = delete;
  ~TextButton() override = default;
};

BEGIN_METADATA(TextButton)
END_METADATA

// The whole panel: both layers, with only one visible at a time.
class PrivacyPanel : public views::View {
  METADATA_HEADER(PrivacyPanel, views::View)

 public:
  PrivacyPanel(base::WeakPtr<Browser> browser,
               PrivacyIntelligenceService::PageAnalysis analysis,
               PrivacyIntelligenceService::PageSignals signals,
               std::u16string site,
               bool blocking_off,
               bool can_allow,
               const Palette& palette,
               int content_width)
      : browser_(std::move(browser)),
        analysis_(std::move(analysis)),
        signals_(signals),
        site_(std::move(site)),
        blocking_off_(blocking_off),
        can_allow_(can_allow),
        palette_(palette),
        content_width_(content_width) {
    // No background: the card is the container. The panel is the card's
    // scrolling content, inset from its edges.
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(0, kCardInset, kCardInset, kCardInset), 0))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);

    summary_ = AddChildView(BuildSummary());
    details_ = AddChildView(BuildDetails());
    details_->SetVisible(false);
  }

  // Which of the two layers is showing. Driven by the card's segmented
  // button, which is why it is public.
  void ShowLayer(bool details);

  // How the panel closes its card. It never closes anything itself: the sheet
  // lives inside the BROWSER WINDOW, so the old GetWidget()->Close() would now
  // close the window.
  void set_on_close(base::RepeatingClosure on_close) {
    on_close_ = std::move(on_close);
  }

  // §14.2: "screen reader announcement of counts on popup open". Called once
  // the sheet is in the window — announcing before that is dropped by the
  // platform.
  void AnnounceOnOpen() {
    GetViewAccessibility().AnnounceText(SummaryAnnouncement());
  }

 private:
  std::u16string SummaryAnnouncement() const {
    return l10n_util::GetStringFUTF16(
        IDS_ZEPHYRUS_PRIVACY_ANNOUNCE_SUMMARY, site_,
        base::FormatNumber(static_cast<int>(analysis_.detected)),
        base::FormatNumber(static_cast<int>(analysis_.blocked)),
        BandString(analysis_.intensity.band));
  }

  void AddSpacer(views::View* parent, int height) {
    parent->AddChildView(std::make_unique<views::View>())
        ->SetPreferredSize(gfx::Size(1, height));
  }

  void ShowLayerImpl(bool details) {
    summary_->SetVisible(!details);
    details_->SetVisible(details);
    // Announce the swap: without this a screen-reader user gets a silent change
    // of everything below the header. The two branches must say DIFFERENT
    // things — an earlier version announced the same string either way, which
    // is the same as announcing nothing.
    GetViewAccessibility().AnnounceText(
        details ? l10n_util::GetStringFUTF16(
                      IDS_ZEPHYRUS_PRIVACY_ANNOUNCE_DETAILS,
                      base::FormatNumber(
                          static_cast<int>(analysis_.rows.size())))
                : SummaryAnnouncement());
    PreferredSizeChanged();
    // The card scrolls; a new layer starts at its top, not wherever the last
    // one was scrolled to.
    ScrollRectToVisible(gfx::Rect(0, 0, 1, 1));
  }

  // An M3 filled card on surfaceContainerHighest.
  views::View* MakeCard(views::View* card) {
    card->SetBackground(
        views::CreateRoundedRectBackground(palette_.card, kInnerRadius));
    return card;
  }

  // §6.1, four lines.
  std::unique_ptr<views::View> BuildSummary() {
    auto view = std::make_unique<views::View>();
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    col->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    // No header here: the card carries the title and the close button.

    // ---- Hero: the verdict, on the one filled surface in the card ---------
    //
    // The headline is bound to what was actually observed. "Protected" is
    // reserved for a page where something was really stopped; a page where
    // trackers were merely seen gets a headline that claims nothing.
    int headline_id;
    if (analysis_.blocked > 0) {
      headline_id = IDS_ZEPHYRUS_PRIVACY_HEADLINE_PROTECTED;
    } else if (analysis_.detected > 0 || analysis_.allowed > 0 ||
               signals_.fingerprint_surface_count() > 0 ||
               signals_.webrtc_address_requests > 0) {
      headline_id = IDS_ZEPHYRUS_PRIVACY_HEADLINE_DETECTED_ONLY;
    } else {
      headline_id = IDS_ZEPHYRUS_PRIVACY_HEADLINE_NOTHING;
    }

    auto* hero = view->AddChildView(std::make_unique<views::View>());
    hero->SetBackground(
        views::CreateRoundedRectBackground(palette_.hero, kInnerRadius));
    auto* hl = hero->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets(kInnerPadding), 2));
    hl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    const int hero_width = content_width_ - 2 * kInnerPadding;

    hero->AddChildView(MakeLabel(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_INTENSITY_LABEL),
        palette_.on_hero, zephyrus::m3::Type::kLabelMedium));
    hero->AddChildView(MakeLabel(BandString(analysis_.intensity.band),
                                 palette_.on_hero,
                                 zephyrus::m3::Type::kHeadlineSmall));
    auto* headline = hero->AddChildView(
        MakeLabel(l10n_util::GetStringUTF16(headline_id), palette_.on_hero,
                  zephyrus::m3::Type::kBodyMedium));
    headline->SetMultiLine(true);
    headline->SetMaximumWidth(hero_width);
    if (analysis_.protection.has_value()) {
      hero->AddChildView(MakeLabel(
          l10n_util::GetStringFUTF16(
              IDS_ZEPHYRUS_PRIVACY_PROTECTION_APPLIED,
              base::FormatNumber(analysis_.protection->percent)),
          palette_.on_hero, zephyrus::m3::Type::kBodySmall));
    }
    // Without this, a site the user allowed shows few or no trackers and reads
    // exactly like a clean one. The absence of blocking has to be stated, or
    // the card silently takes credit for a page it is not protecting.
    if (blocking_off_) {
      auto* notice = hero->AddChildView(MakeLabel(
          l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_ALLOWED_NOTICE),
          palette_.on_hero, zephyrus::m3::Type::kBodyMedium));
      notice->SetMultiLine(true);
      notice->SetMaximumWidth(hero_width);
    }

    AddSpacer(view.get(), 16);

    // ---- What was seen, in sentences --------------------------------------
    if (analysis_.blocked > 0) {
      view->AddChildView(
          MakeLabel(Plural(IDS_ZEPHYRUS_PRIVACY_TRACKERS_BLOCKED,
                           analysis_.blocked),
                    palette_.muted, zephyrus::m3::Type::kBodyMedium));
    }
    if (signals_.fingerprint_surface_count() > 0) {
      // DETECTED, not randomized. §6.5's randomization is Phase 4 work, and
      // saying "randomized" before it exists would promise a protection the
      // browser is not performing.
      view->AddChildView(
          MakeLabel(Plural(IDS_ZEPHYRUS_PRIVACY_FINGERPRINT_DETECTED,
                           signals_.fingerprint_surface_count()),
                    palette_.muted, zephyrus::m3::Type::kBodyMedium));
    }
    if (signals_.webrtc_address_requests > 0) {
      // The reassuring half of this sentence appears only when the policy in
      // force actually withheld local addresses (§9.2.1).
      auto* webrtc = view->AddChildView(MakeLabel(
          l10n_util::GetStringUTF16(
              signals_.local_addresses_withheld
                  ? IDS_ZEPHYRUS_PRIVACY_WEBRTC_WITHHELD
                  : IDS_ZEPHYRUS_PRIVACY_WEBRTC_EXPOSED),
          palette_.muted, zephyrus::m3::Type::kBodyMedium));
      webrtc->SetMultiLine(true);
      webrtc->SetMaximumWidth(content_width_);
    }

    AddSpacer(view.get(), 16);
    view->AddChildView(BuildArithmetic());
    view->AddChildView(BuildWhatWasProtected());

    // No site means nothing to allow: an about:blank page has no host for the
    // allowlist to match, and a switch that silently did nothing is worse than
    // no switch. The same goes for a profile with no blocker to tell.
    if (!site_.empty() && can_allow_) {
      AddSpacer(view.get(), 16);
      view->AddChildView(BuildQuickControls());
    }

    // Nothing here opens the tracker list: the card's segmented button does,
    // and two controls for one destination is one too many.
    return view;
  }

  // §6.3: "no weights the user cannot inspect. Show the arithmetic on tap."
  // Rendered straight from the breakdown the score function returned, so there
  // is no second copy of the weights to drift out of step.
  std::unique_ptr<views::View> BuildArithmetic() {
    auto view = std::make_unique<views::View>();
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
    col->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    struct TermLabel {
      TermIndex index;
      int message_id;
    };
    static constexpr TermLabel kTerms[] = {
        {TermIndex::kEntities, IDS_ZEPHYRUS_PRIVACY_TERM_ENTITIES},
        {TermIndex::kThirdPartyDomains, IDS_ZEPHYRUS_PRIVACY_TERM_DOMAINS},
        {TermIndex::kFingerprinting, IDS_ZEPHYRUS_PRIVACY_TERM_FINGERPRINTING},
        {TermIndex::kTrackingCookies, IDS_ZEPHYRUS_PRIVACY_TERM_COOKIES},
    };
    for (const TermLabel& term : kTerms) {
      const IntensityTerm& t = analysis_.intensity.term(term.index);
      auto* row = view->AddChildView(std::make_unique<views::View>());
      auto* rl = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
      rl->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      const std::u16string name = l10n_util::GetStringUTF16(term.message_id);
      const std::u16string points = l10n_util::GetStringFUTF16(
          IDS_ZEPHYRUS_PRIVACY_TERM_POINTS, base::FormatNumber(t.points),
          base::FormatNumber(t.max_points));

      auto* label =
          row->AddChildView(MakeLabel(name, palette_.muted,
                                      zephyrus::m3::Type::kBodyMedium));
      rl->SetFlexForView(label, 1);
      row->AddChildView(MakeLabel(base::FormatNumber(t.observed), palette_.fg,
                                  zephyrus::m3::Type::kLabelLarge));
      // The bar carries the SCORE, not the count: the count has no ceiling to
      // draw it against, and the point of §6.3 is showing how the score was
      // reached.
      row->AddChildView(std::make_unique<ScoreBar>(
          t.max_points > 0
              ? static_cast<float>(t.points) / static_cast<float>(t.max_points)
              : 0.0f,
          palette_.accent, palette_.track));
      auto* points_label = row->AddChildView(
          MakeLabel(points, palette_.muted, zephyrus::m3::Type::kBodySmall));
      // Trailing edge. gfx has no ALIGN_TO_TAIL, so this is flipped
      // explicitly for RTL rather than left as a hard right.
      points_label->SetHorizontalAlignment(
          base::i18n::IsRTL() ? gfx::ALIGN_LEFT : gfx::ALIGN_RIGHT);

      // One row, one sentence: a reader that walked the four labels
      // separately would say "Companies, 3, 0 of 4" with a silent bar in the
      // middle of it.
      row->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
      row->GetViewAccessibility().SetName(base::JoinString(
          {name, base::FormatNumber(t.observed), points}, u", "));
    }
    return view;
  }

  // §6.6. Translates what the browser DID into what the user KEEPS.
  //
  // Returns an empty, zero-height view when nothing was prevented — §6.6:
  // "Never list a category with zero events 'for completeness.' An empty list
  // is an honest list." The heading goes with it, because a heading over
  // nothing still implies something.
  std::unique_ptr<views::View> BuildWhatWasProtected() {
    auto view = std::make_unique<views::View>();
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    col->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    ProtectionInputs inputs;
    // Only outcomes where something was actually prevented. Advertising rows
    // are a subset of the cross-site total; §6.6 lists both because they are
    // two true statements about overlapping events, not a double count of one.
    for (const auto& row : analysis_.rows) {
      if (row.category == Category::kAdvertising) {
        inputs.advertising_requests_blocked += row.blocked;
      }
    }
    inputs.cross_site_requests_blocked = analysis_.blocked;
    inputs.third_party_cookies_blocked = analysis_.cookies_blocked;
    // referrers_stripped still has no signal.
    //
    // fingerprint_surfaces_randomized comes from the RANDOMIZED mask, never
    // from the reported-surface mask. A surface is reported whether or not it
    // was perturbed — deliberately, so that turning randomization off does not
    // also blind detection (see BaseRenderingContext2D::getImageData) — so
    // counting reports here would put "Device fingerprint" under "What was
    // protected" for surfaces that were only DETECTED. The browser derives the
    // randomized bit itself in ZephyrusPrivacyReporterHost, so a compromised
    // renderer cannot claim a protection that never happened.
    //
    // With Phase 4 off this is 0 and the row does not appear, which is the same
    // behaviour as the hardcoded zero it replaces — but now for the right
    // reason, and it becomes true on its own when randomization is enabled.
    //
    // The claim stays scoped to SURFACES. Shared workers are an open evasion
    // (a page perturbed in the document can read true pixels from a
    // SharedWorker) and fonts are not instrumented at all, so "these surfaces
    // were randomized" is honest and anything implying the device cannot be
    // identified is not.
    inputs.fingerprint_surfaces_randomized =
        signals_.fingerprint_randomized_count();

    const std::vector<ProtectedItem> items = ComputeWhatWasProtected(inputs);
    if (items.empty()) {
      return view;
    }

    AddSpacer(view.get(), 20);
    view->AddChildView(MakeLabel(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_PROTECTED_HEADING),
        palette_.fg, zephyrus::m3::Type::kTitleMedium));
    AddSpacer(view.get(), 8);

    auto* list = view->AddChildView(std::make_unique<views::View>());
    list->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
    auto* ll = list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
    ll->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    for (const ProtectedItem& item : items) {
      auto* row = list->AddChildView(std::make_unique<views::View>());
      auto* rl = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
      const std::u16string name = ProtectedCategoryString(item.category);
      const std::u16string count =
          base::FormatNumber(static_cast<int>(item.count));
      auto* label = row->AddChildView(
          MakeLabel(name, palette_.muted, zephyrus::m3::Type::kBodyMedium));
      rl->SetFlexForView(label, 1);
      row->AddChildView(
          MakeLabel(count, palette_.accent, zephyrus::m3::Type::kLabelLarge));
      row->GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
      row->GetViewAccessibility().SetName(
          base::JoinString({name, count}, u", "));
    }
    return view;
  }

  // §6 quick controls. One control for now: the breakage escape hatch. An M3
  // list item on a filled card: a bodyLarge label with an M3 switch, and the
  // supporting line under it.
  std::unique_ptr<views::View> BuildQuickControls() {
    auto view = std::make_unique<views::View>();
    MakeCard(view.get());
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(8, kInnerPadding, kInnerPadding, 8), 0));
    col->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* row = view->AddChildView(std::make_unique<views::View>());
    auto* rl = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
    rl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* label = row->AddChildView(
        MakeLabel(l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_ALLOW_SITE),
                  palette_.fg, zephyrus::m3::Type::kBodyLarge));
    rl->SetFlexForView(label, 1);

    auto* toggle = row->AddChildView(std::make_unique<zephyrus::m3::Switch>(
        base::BindRepeating(&PrivacyPanel::OnAllowSiteToggled,
                            base::Unretained(this))));
    toggle->SetIsOn(blocking_off_);
    toggle->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    toggle->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_ALLOW_SITE));
    allow_toggle_ = toggle;

    auto* hint = view->AddChildView(MakeLabel(
        l10n_util::GetStringFUTF16(IDS_ZEPHYRUS_PRIVACY_ALLOW_SITE_HINT, site_),
        palette_.muted, zephyrus::m3::Type::kBodyMedium));
    hint->SetMultiLine(true);
    hint->SetMaximumWidth(content_width_ - 2 * kInnerPadding);
    return view;
  }

  void OnAllowSiteToggled() {
    if (!browser_ || !allow_toggle_ || site_.empty()) {
      return;
    }
    const bool allow = allow_toggle_->GetIsOn();
    // The ACTIVE TAB's profile, not the window's.
    //
    // This panel already reads the site from the active tab, and used to write
    // the result to the window -- so in a window holding two workspaces you
    // could allow a site while looking at one profile's page and have the
    // allowlist entry land in the other. Half-correct is harder to spot than
    // wholly wrong: the panel showed the right site name the whole time.
    zephyrus_adblock::ZephyrusAdblockService* adblock =
        zephyrus_adblock::ZephyrusAdblockServiceFactory::GetForBrowserContext(
            zephyrus::ActiveProfile(browser_.get()));
    if (!adblock) {
      return;
    }
    const std::string domain = base::UTF16ToUTF8(site_);
    if (allow) {
      adblock->AddAllowlistDomain(domain);
    } else {
      adblock->RemoveAllowlistDomain(domain);
    }

    content::WebContents* contents =
        browser_->tab_strip_model()->GetActiveWebContents();
    if (!contents) {
      return;
    }
    // §9's breakage signal, recorded only when blocking is turned OFF. Turning
    // it back on says nothing about a broken page. Recorded before the reload
    // so it lands against the page the user was actually looking at.
    if (allow) {
      if (PrivacyIntelligenceService* service =
              PrivacyIntelligenceServiceFactory::GetForBrowserContext(
                  zephyrus::ActiveProfile(browser_.get()))) {
        service->RecordUserAllowedSite(contents->GetLastCommittedURL());
      }
    }
    // The change only takes effect on the next load, and a user who flips this
    // is looking at a broken page right now.
    contents->GetController().Reload(content::ReloadType::NORMAL,
                                     /*check_for_repost=*/false);
    // The analysis on screen describes the page before the reload, so the sheet
    // goes. Through the sheet, never GetWidget()->Close(): that widget is the
    // browser window.
    if (on_close_) {
      on_close_.Run();
    }
  }

  // §6.2, per-tracker rows.
  std::unique_ptr<views::View> BuildDetails() {
    auto view = std::make_unique<views::View>();
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    col->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    // No "Back" and no site name here: the segmented button above switches
    // layers, and the card's own header already names the site.
    view->AddChildView(MakeLabel(
        l10n_util::GetStringFUTF16(
            IDS_ZEPHYRUS_PRIVACY_SUMMARY_COUNTS,
            base::FormatNumber(analysis_.detected),
            base::FormatNumber(analysis_.blocked),
            base::FormatNumber(analysis_.allowed)),
        palette_.muted, zephyrus::m3::Type::kBodyMedium));

    AddSpacer(view.get(), 12);

    if (analysis_.rows.empty()) {
      view->AddChildView(
          MakeLabel(l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_NO_REQUESTS),
                    palette_.muted, zephyrus::m3::Type::kBodyMedium));
      return view;
    }

    // §14.2: "the tracker list is a table with headers, not a stack of divs."
    // Built from Views rather than a views::TableView — a TableView cannot be
    // styled to match this panel — but carrying the table ROLES, which is what
    // a screen reader navigates by. Without them the reader gets four
    // unlabelled strings per row and no way to ask which column it is in.
    //
    // Not wrapped in its own ScrollView any more: the whole sheet scrolls, and
    // a capped list inside a scrolling sheet is a scroll box in a scroll box.
    auto* list = view->AddChildView(std::make_unique<views::View>());
    list->GetViewAccessibility().SetRole(ax::mojom::Role::kTable);
    list->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_TITLE));
    auto* ll = list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
    ll->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    list->AddChildView(BuildTrackerHeaderRow());
    for (const auto& row : analysis_.rows) {
      list->AddChildView(BuildTrackerRow(row));
    }
    return view;
  }

  // The header row. Visible as well as exposed: sighted users need to know
  // what "12 req" is counting just as much as screen-reader users do.
  std::unique_ptr<views::View> BuildTrackerHeaderRow() {
    auto view = std::make_unique<views::View>();
    auto* rl = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        // Past the monogram, so "Tracker" sits over the names rather than over
        // the circles.
        gfx::Insets::TLBR(4, kAvatarSize + 8, 4, 0), 8));
    view->GetViewAccessibility().SetRole(ax::mojom::Role::kRow);

    auto add_header = [&](int message_id, int flex) {
      auto* label = view->AddChildView(
          MakeLabel(l10n_util::GetStringUTF16(message_id), palette_.muted,
                    zephyrus::m3::Type::kLabelMedium));
      label->GetViewAccessibility().SetRole(ax::mojom::Role::kColumnHeader);
      if (flex) {
        rl->SetFlexForView(label, flex);
      }
      return label;
    };
    // THREE columns, not four. The category moved under the tracker's name
    // where it reads as part of it, so a "Category" header would stand over an
    // empty column -- and a screen reader would announce every request count
    // against the wrong heading.
    add_header(IDS_ZEPHYRUS_PRIVACY_COLUMN_TRACKER, 1);
    add_header(IDS_ZEPHYRUS_PRIVACY_COLUMN_REQUESTS, 0);
    add_header(IDS_ZEPHYRUS_PRIVACY_COLUMN_STATUS, 0);
    return view;
  }

  // §6.10. One template per (event_type, status) pair, chosen by lookup — no
  // model, no inference, no network call. These rows are all one event type
  // (a tracker request), so the status is what selects; the fingerprint,
  // WebRTC and user-allowed templates belong to the timeline, which carries
  // those event types.
  //
  // The partial case is the one that matters. A row where six of eight
  // requests were blocked reads "Blocked" nowhere: it says what got through,
  // by count, because §2 forbids a status the evidence does not support and
  // rounding two leaked requests away would do exactly that.
  static std::u16string ExplainRow(
      const PrivacyIntelligenceService::TrackerRow& row) {
    if (row.blocked && row.blocked < row.requests) {
      return l10n_util::GetStringFUTF16(
          IDS_ZEPHYRUS_PRIVACY_EXPLAIN_PARTIAL, RowName(row),
          base::FormatNumber(row.requests), base::FormatNumber(row.blocked),
          base::FormatNumber(row.requests - row.blocked));
    }
    int message_id = IDS_ZEPHYRUS_PRIVACY_EXPLAIN_DETECTED;
    switch (row.status) {
      case TrackerStatus::kBlocked:
        message_id = IDS_ZEPHYRUS_PRIVACY_EXPLAIN_BLOCKED;
        break;
      case TrackerStatus::kAllowed:
        message_id = IDS_ZEPHYRUS_PRIVACY_EXPLAIN_ALLOWED;
        break;
      case TrackerStatus::kDetected:
      case TrackerStatus::kRandomized:
      case TrackerStatus::kPotential:
        // Same reasoning as StatusString: rows never carry the last two, and
        // claiming randomization here would be a protection we did not
        // perform. Detected says only what was seen.
        break;
    }
    return PluralNamed(message_id, RowName(row), row.requests);
  }

  // §4.2 in one place: an unattributed domain is named by itself, never by a
  // guessed owner.
  static std::u16string RowName(
      const PrivacyIntelligenceService::TrackerRow& row) {
    return row.entity_name.empty() ? base::UTF8ToUTF16(row.domain)
                                   : base::UTF8ToUTF16(row.entity_name);
  }

  std::unique_ptr<views::View> BuildTrackerRow(
      const PrivacyIntelligenceService::TrackerRow& row) {
    auto view = std::make_unique<views::View>();
    auto* rl = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(6, 0), 8));
    rl->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    // §4.2: an unknown domain renders as itself, with no invented owner and no
    // invented category. `entity_name` empty is the signal for that, and the
    // category column is simply "Unknown" rather than a guess.
    const std::u16string row_name = RowName(row);
    // A monogram, not a favicon: fetching one would be a request to the
    // tracker's own domain, made because the user opened a privacy report.
    auto* avatar = view->AddChildView(std::make_unique<Monogram>(
        row_name, palette_.card, palette_.fg));
    avatar->GetViewAccessibility().SetIsIgnored(true);

    auto* names = view->AddChildView(std::make_unique<views::View>());
    names->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    const std::u16string& primary = row_name;
    names->AddChildView(
        MakeLabel(primary, palette_.fg, zephyrus::m3::Type::kBodyLarge));
    names->AddChildView(MakeLabel(CategoryString(row.category), palette_.muted,
                                  zephyrus::m3::Type::kBodySmall));
    rl->SetFlexForView(names, 1);

    view->AddChildView(MakeLabel(
        Plural(IDS_ZEPHYRUS_PRIVACY_REQUESTS, row.requests), palette_.muted,
        zephyrus::m3::Type::kBodySmall));
    // Blocked rows are the reassuring ones, so only they get the accent; a
    // row that let something through must not be coloured like a success.
    view->AddChildView(std::make_unique<StatusPill>(
        StatusString(row.status),
        row.status == TrackerStatus::kBlocked ? palette_.accent
                                              : palette_.muted,
        palette_.card));

    // kRow with kCell children, matching the kTable above (three columns: the
    // category rides along inside the name cell), so the reader can navigate by
    // column and hear each cell against its header. The row also
    // carries the whole line as its name, for readers that announce the row
    // before descending into it.
    view->GetViewAccessibility().SetRole(ax::mojom::Role::kRow);
    view->GetViewAccessibility().SetName(base::JoinString(
        {primary, CategoryString(row.category),
         Plural(IDS_ZEPHYRUS_PRIVACY_REQUESTS, row.requests),
         StatusString(row.status)},
        u", "));
    for (views::View* cell : view->children()) {
      // The monogram is decoration and stays out of the table; everything else
      // is a cell under one of the four headers.
      if (cell != avatar) {
        cell->GetViewAccessibility().SetRole(ax::mojom::Role::kCell);
      }
    }

    // The explanation sits OUTSIDE the table row. Inside, it would be a fifth
    // cell with no column header, and a screen reader would read it against
    // whichever header happened to be fourth. Wrapping instead keeps the table
    // four columns wide and gives the sentence its own line.
    auto wrap = std::make_unique<views::View>();
    auto* wl = wrap->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    wl->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    wrap->AddChildView(std::move(view));
    auto* why = wrap->AddChildView(MakeLabel(ExplainRow(row), palette_.muted,
                                             zephyrus::m3::Type::kBodySmall));
    why->SetMultiLine(true);
    why->SetMaximumWidth(content_width_);
    why->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
    // The row above already announces the same facts as name, category, count
    // and status. Reading the sentence too would say everything twice, so it
    // is presentational to a screen reader and visible to everyone else.
    why->GetViewAccessibility().SetIsIgnored(true);
    return wrap;
  }

  // Weak: the hop from a click to a reload passes through the sheet's own
  // teardown, and the window can be closed under it.
  base::WeakPtr<Browser> browser_;
  PrivacyIntelligenceService::PageAnalysis analysis_;
  PrivacyIntelligenceService::PageSignals signals_;
  std::u16string site_;
  bool blocking_off_ = false;
  bool can_allow_ = false;
  Palette palette_;
  int content_width_;
  base::RepeatingClosure on_close_;
  raw_ptr<zephyrus::m3::Switch> allow_toggle_ = nullptr;
  raw_ptr<views::View> summary_ = nullptr;
  raw_ptr<views::View> details_ = nullptr;
};

BEGIN_METADATA(PrivacyPanel)
END_METADATA

void PrivacyPanel::ShowLayer(bool details) {
  ShowLayerImpl(details);
}

// One segment of an M3 SINGLE-SELECT SEGMENTED BUTTON: 40dp, a 1dp `outline`
// all round, full-round on the group's outer edge and square where the two
// segments meet. Selected takes secondaryContainer and a leading checkmark.
class Segment : public views::Button {
  METADATA_HEADER(Segment, views::Button)

 public:
  Segment(PressedCallback callback,
          const std::u16string& label,
          bool first,
          bool selected)
      : views::Button(std::move(callback)),
        label_(label),
        font_(zephyrus::m3::Font(zephyrus::m3::Type::kLabelLarge)),
        first_(first),
        selected_(selected) {
    GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
    GetViewAccessibility().SetName(label);
    GetViewAccessibility().SetIsSelected(selected);
    SetInstallFocusRingOnFocus(true);
  }
  Segment(const Segment&) = delete;
  Segment& operator=(const Segment&) = delete;
  ~Segment() override = default;

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    GetViewAccessibility().SetIsSelected(selected);
    SchedulePaint();
  }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(gfx::GetStringWidth(label_, font_) + 4 * kSegmentPad,
                     kSegmentHeight);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    // Every colour is a role, and a role needs a Widget.
    if (!GetWidget()) {
      return;
    }
    const SkColor content = zephyrus::m3::Role(
        *this, selected_ ? kColorZephyrusOnSecondaryContainer
                         : kColorZephyrusOnSurface);
    // Bounds are mirrored for RTL but PAINTING is not, so the group's outer
    // edge is on the left here exactly when this segment is drawn on the left.
    const bool round_left = first_ != base::i18n::IsRTL();
    const float r = kSegmentHeight / 2.0f;
    const SkVector radii[4] = {
        {round_left ? r : 0, round_left ? r : 0},
        {round_left ? 0 : r, round_left ? 0 : r},
        {round_left ? 0 : r, round_left ? 0 : r},
        {round_left ? r : 0, round_left ? r : 0},
    };
    SkRRect shape;
    shape.setRectRadii(gfx::RectFToSkRect(gfx::RectF(GetLocalBounds())), radii);

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (selected_) {
      flags.setColor(
          zephyrus::m3::Role(*this, kColorZephyrusSecondaryContainer));
      canvas->sk_canvas()->drawRRect(shape, flags);
    }
    if (GetEnabled()) {
      const bool pressed = GetState() == STATE_PRESSED;
      const bool hovered = GetState() == STATE_HOVERED;
      if (pressed || hovered || HasFocus()) {
        flags.setColor(zephyrus::m3::StateLayer(
            content, pressed   ? zephyrus::m3::kPressed
                     : hovered ? zephyrus::m3::kHover
                               : zephyrus::m3::kFocus));
        canvas->sk_canvas()->drawRRect(shape, flags);
      }
    }
    SkRRect outline = shape;
    outline.inset(0.5f, 0.5f);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.0f);
    flags.setColor(zephyrus::m3::Role(*this, kColorZephyrusOutline));
    canvas->sk_canvas()->drawRRect(outline, flags);

    // Laid out in LTR and mirrored by hand: flipping the canvas would mirror
    // the glyphs too.
    int text_x = kSegmentPad;
    if (selected_) {
      const gfx::ImageSkia check =
          gfx::CreateVectorIcon(kCheckIcon, kSegmentIcon, content);
      canvas->DrawImageInt(check,
                           GetMirroredXWithWidthInView(kSegmentPad,
                                                       kSegmentIcon),
                           (height() - kSegmentIcon) / 2);
      text_x += kSegmentIcon + kSegmentPad;
    }
    canvas->DrawStringRectWithFlags(
        label_, font_, content,
        GetMirroredRect(gfx::Rect(text_x, 0,
                                  std::max(0, width() - text_x - kSegmentPad),
                                  height())),
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
  static constexpr int kSegmentPad = 8;
  static constexpr int kSegmentIcon = 18;

  std::u16string label_;
  gfx::FontList font_;
  bool first_;
  bool selected_;
};

BEGIN_METADATA(Segment)
END_METADATA

// An M3 MODAL SIDE SHEET was what this used to be. It is now a FLOATING CARD
// inside the browser window: the report is about the page, so the page stays
// visible and usable behind it.
//
// A view in the window rather than a widget, the same as the search overlay and
// the tab switcher. It is created on open and removed on close, so nothing in
// BrowserView had to change to host it.
class PrivacySideSheet : public views::View,
                         public views::ViewObserver,
                         public TabStripModelObserver {
  METADATA_HEADER(PrivacySideSheet, views::View)

 public:
  PrivacySideSheet(BrowserView* browser_view,
                   TabStripModel* tab_strip_model,
                   std::unique_ptr<PrivacyPanel> panel,
                   const std::u16string& site,
                   size_t tracker_count)
      : browser_view_(browser_view) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kCardShape));
    layer()->SetIsFastRoundedCorner(true);
    shadow_ = std::make_unique<views::ViewShadow>(this, kCardElevation);
    shadow_->SetRoundedCornerRadius(kCardShape);

    GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_TITLE));
    // The card takes focus when it opens, so the keyboard is inside it and
    // Escape closes it.
    SetFocusBehavior(views::View::FocusBehavior::ALWAYS);

    auto* column = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    column->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    // ---- Header: shield, title over site, close --------------------------
    auto* header = AddChildView(std::make_unique<views::View>());
    auto* hl = header->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(kCardInset, kCardInset, 0, 8), 12));
    hl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    shield_ = header->AddChildView(std::make_unique<views::ImageView>());
    auto* titles = header->AddChildView(std::make_unique<views::View>());
    titles->SetLayoutManager(std::make_unique<views::BoxLayout>(
                                 views::BoxLayout::Orientation::kVertical))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);
    title_ = titles->AddChildView(
        MakeLabel(l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_TITLE),
                  SK_ColorTRANSPARENT, zephyrus::m3::Type::kTitleMedium));
    if (!site.empty()) {
      site_ = titles->AddChildView(MakeLabel(site, SK_ColorTRANSPARENT,
                                             zephyrus::m3::Type::kBodySmall));
      site_->SetElideBehavior(gfx::ELIDE_HEAD);
    }
    hl->SetFlexForView(titles, 1);
    close_button_ = header->AddChildView(std::make_unique<views::ImageButton>(
        base::BindRepeating(&PrivacySideSheet::Dismiss,
                            base::Unretained(this))));
    close_button_->SetPreferredSize(
        gfx::Size(kIconButtonSize, kIconButtonSize));
    close_button_->SetImageHorizontalAlignment(
        views::ImageButton::ALIGN_CENTER);
    close_button_->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    close_button_->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_APP_CLOSE));
    close_button_->SetTooltipText(l10n_util::GetStringUTF16(IDS_APP_CLOSE));
    views::InstallCircleHighlightPathGenerator(close_button_);
    views::InkDropHost* const ink = views::InkDrop::Get(close_button_);
    ink->SetMode(views::InkDropHost::InkDropMode::ON);
    ink->SetHighlightOpacity(zephyrus::m3::kHover / 255.0f);
    ink->SetVisibleOpacity(zephyrus::m3::kPressed / 255.0f);

    // ---- The two layers, as a segmented button ---------------------------
    auto* segments = AddChildView(std::make_unique<views::View>());
    segments->GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
    auto* sl = segments->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(12, kCardInset, 4, kCardInset), 0));
    PrivacyPanel* const panel_raw = panel.get();
    summary_segment_ = segments->AddChildView(std::make_unique<Segment>(
        base::BindRepeating(&PrivacySideSheet::SelectLayer,
                            base::Unretained(this), false),
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_SEGMENT_SUMMARY),
        /*first=*/true, /*selected=*/true));
    trackers_segment_ = segments->AddChildView(std::make_unique<Segment>(
        base::BindRepeating(&PrivacySideSheet::SelectLayer,
                            base::Unretained(this), true),
        l10n_util::GetStringFUTF16(
            IDS_ZEPHYRUS_PRIVACY_SEGMENT_TRACKERS,
            base::FormatNumber(static_cast<int>(tracker_count))),
        /*first=*/false, /*selected=*/false));
    // Equal halves: M3 segmented buttons share the width.
    sl->SetFlexForView(summary_segment_, 1);
    sl->SetFlexForView(trackers_segment_, 1);

    // ---- The panel, scrolling --------------------------------------------
    //
    // Set up as the sidebar's tab list is, which learned both halves the hard
    // way: ClipHeightTo is required, and the scrollbar must be an overlay.
    auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
    scroll->ClipHeightTo(0, kUnboundedScrollHeight);
    scroll->SetBackgroundColor(std::nullopt);
    scroll->SetDrawOverflowIndicator(false);
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll->SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(
        views::ScrollBar::Orientation::kVertical));
    scroll->SetContents(std::move(panel));
    panel_ = panel_raw;
    column->SetFlexForView(scroll, 1);

    bounds_observations_.AddObservation(browser_view);
    if (views::View* top = browser_view->top_container()) {
      bounds_observations_.AddObservation(top);
    }
    // Not a ScopedObservation: a TabStripModel can die before this view, and
    // TabStripModelObserver already unhooks itself from a dying model and
    // from every model it still watches on destruction. A ScopedObservation
    // would then remove itself again from the freed model.
    if (tab_strip_model) {
      tab_strip_model->AddObserver(this);
    }
  }
  PrivacySideSheet(const PrivacySideSheet&) = delete;
  PrivacySideSheet& operator=(const PrivacySideSheet&) = delete;
  ~PrivacySideSheet() override = default;

  PrivacyPanel* panel() { return panel_; }
  bool dismissing() const { return dismissing_; }
  base::WeakPtr<PrivacySideSheet> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Below the title bar, against the trailing edge, as tall as its content
  // needs up to the space available.
  void UpdateBounds() {
    const gfx::Rect window = browser_view_->GetLocalBounds();
    int top = 0;
    views::View* const top_container = browser_view_->top_container();
    if (top_container && top_container->GetVisible() &&
        top_container->GetWidget() == browser_view_->GetWidget()) {
      top = views::View::ConvertRectToTarget(top_container, browser_view_,
                                             top_container->GetLocalBounds())
                .bottom();
    }
    top = std::clamp(top, 0, window.height()) + kCardMargin;
    const int width =
        std::max(0, std::min(kCardWidth, window.width() - 2 * kCardMargin));
    const int available = std::max(0, window.height() - top - kCardMargin);
    const int height = std::min(GetHeightForWidth(width), available);
    // LTR coordinates; Views mirrors this to the leading edge in RTL.
    SetBounds(window.width() - kCardMargin - width, top, width, height);
  }

  // Fades and slides in from the trailing edge.
  void Open() {
    DeprecatedLayoutImmediately();
    layer()->SetOpacity(0.0f);
    layer()->SetTransform(SlideTransform());
    {
      ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
      settings.SetTransitionDuration(kCardDuration);
      settings.SetTweenType(
          zephyrus::m3::TweenFor(zephyrus::m3::Spring::kDefaultSpatial));
      layer()->SetTransform(gfx::Transform());
      layer()->SetOpacity(1.0f);
    }
    RequestFocus();
  }

  // Slides out, then removes itself.
  void Dismiss() {
    if (dismissing_) {
      return;
    }
    dismissing_ = true;
    TabStripModelObserver::StopObservingAll(this);
    {
      ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
      settings.SetTransitionDuration(kCardDuration);
      settings.SetTweenType(gfx::Tween::FAST_OUT_LINEAR_IN);
      layer()->SetTransform(SlideTransform());
      layer()->SetOpacity(0.0f);
    }
    // Removed AFTER the exit animation, and never inside the call that asked.
    // Dismiss is reached from the close button's own callback and from the
    // switch's; removing synchronously would free that button mid-dispatch.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&PrivacySideSheet::RemoveFromWindow,
                       weak_factory_.GetWeakPtr()),
        kCardDuration);
  }

  // Immediate, for tests and for replacing an open card. Not for use from any
  // of this card's own event handlers.
  void RemoveFromWindow() {
    // Give the page its focus back before the focused view disappears -- but
    // only if the focus is still in the card. Anywhere else, the user put it
    // there.
    views::FocusManager* const focus_manager = GetFocusManager();
    if (focus_manager && Contains(focus_manager->GetFocusedView())) {
      if (views::View* contents = browser_view_->contents_web_view()) {
        contents->RequestFocus();
      }
    }
    if (views::View* parent_view = parent()) {
      parent_view->RemoveChildViewT(this);  // Deletes `this`.
    }
  }

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override {
    // The card is opaque to the page under it: a click on its background is
    // not a click on the page.
    return true;
  }

  bool OnKeyPressed(const ui::KeyEvent& event) override {
    // Escape, but NOT as an accelerator: an accelerator is window-wide, and
    // this card is not modal -- it would swallow the Escape key from the page
    // behind it. RootView walks up from the focused view, so this is reached
    // only when the focus is inside the card.
    if (event.key_code() == ui::VKEY_ESCAPE) {
      Dismiss();
      return true;
    }
    return false;
  }

  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    SetBackground(views::CreateRoundedRectBackground(
        zephyrus::m3::Role(*this, PrefersMoreContrast()
                                      ? kColorZephyrusSurface
                                      : kColorZephyrusSurfaceContainer),
        kCardShape));
    const SkColor on_surface =
        zephyrus::m3::Role(*this, kColorZephyrusOnSurface);
    const SkColor variant =
        zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant);
    title_->SetEnabledColor(on_surface);
    if (site_) {
      site_->SetEnabledColor(variant);
    }
    shield_->SetImage(ui::ImageModel::FromVectorIcon(
        kZephyrusShieldIcon, zephyrus::m3::Role(*this, kColorZephyrusPrimary),
        kIconSize));
    close_button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(vector_icons::kCloseIcon, variant,
                                       kIconSize));
    views::InkDrop::Get(close_button_)->SetBaseColor(variant);
  }

  void ChildPreferredSizeChanged(views::View* child) override {
    // Switching layers changes how tall the card wants to be.
    UpdateBounds();
  }

  // views::ViewObserver:
  void OnViewBoundsChanged(views::View* observed_view) override {
    UpdateBounds();
  }
  void OnViewIsDeleting(views::View* observed_view) override {
    bounds_observations_.RemoveObservation(observed_view);
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override {
    // The analysis describes ONE page. Once another tab is in front, every
    // number on this card is about something the user is not looking at.
    if (selection.active_tab_changed()) {
      Dismiss();
    }
  }

 private:
  void SelectLayer(bool details) {
    summary_segment_->SetSelected(!details);
    trackers_segment_->SetSelected(details);
    panel_->ShowLayer(details);
  }

  // Just past the trailing edge, whichever side that is.
  gfx::Transform SlideTransform() const {
    gfx::Transform transform;
    transform.Translate(base::i18n::IsRTL() ? -kCardSlide : kCardSlide, 0);
    return transform;
  }

  const raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::ViewShadow> shadow_;
  raw_ptr<views::ImageView> shield_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> site_ = nullptr;
  raw_ptr<views::ImageButton> close_button_ = nullptr;
  raw_ptr<Segment> summary_segment_ = nullptr;
  raw_ptr<Segment> trackers_segment_ = nullptr;
  raw_ptr<PrivacyPanel> panel_ = nullptr;
  bool dismissing_ = false;
  base::ScopedMultiSourceObservation<views::View, views::ViewObserver>
      bounds_observations_{this};
  base::WeakPtrFactory<PrivacySideSheet> weak_factory_{this};
};

BEGIN_METADATA(PrivacySideSheet)
END_METADATA

PrivacySideSheet* FindSheet(BrowserView* browser_view) {
  if (!browser_view) {
    return nullptr;
  }
  for (views::View* child : browser_view->children()) {
    if (auto* sheet = views::AsViewClass<PrivacySideSheet>(child)) {
      return sheet;
    }
  }
  return nullptr;
}

// Second half of ShowPrivacyPopup, once the analysis comes back.
void ShowWithAnalysis(base::WeakPtr<Browser> browser,
                      PrivacyIntelligenceService::PageSignals signals,
                      std::u16string site,
                      bool blocking_off,
                      bool can_allow,
                      PrivacyIntelligenceService::PageAnalysis analysis) {
  // The user may have closed the window during the hop to the privacy
  // sequence. The weak Browser is what makes that safe rather than a
  // use-after-free.
  if (!browser) {
    return;
  }
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser.get());
  if (!browser_view || !browser_view->GetWidget()) {
    return;
  }
  // One sheet per window. Replacing it here is safe: this is a posted reply,
  // not one of the old sheet's own event handlers.
  if (PrivacySideSheet* existing = FindSheet(browser_view)) {
    existing->RemoveFromWindow();
  }

  const Palette palette = MakePalette(*browser_view);
  const int card_width = std::max(
      0, std::min(kCardWidth, browser_view->width() - 2 * kCardMargin));
  const size_t tracker_count = analysis.rows.size();
  auto panel = std::make_unique<PrivacyPanel>(
      browser, std::move(analysis), signals, site, blocking_off, can_allow,
      palette, std::max(0, card_width - 2 * kCardInset));
  PrivacyPanel* const panel_ptr = panel.get();

  auto* sheet = browser_view->AddChildView(std::make_unique<PrivacySideSheet>(
      browser_view, browser->tab_strip_model(), std::move(panel), site,
      tracker_count));
  panel_ptr->set_on_close(base::BindRepeating(&PrivacySideSheet::Dismiss,
                                              sheet->GetWeakPtr()));
  sheet->UpdateBounds();
  sheet->Open();
  // After the card is in the window: an announcement made before that is
  // dropped by the platform, so the counts would never be spoken (§14.2).
  panel_ptr->AnnounceOnOpen();
}

}  // namespace

void ShowPrivacyPopup(Browser* browser, views::View* anchor) {
  if (!browser) {
    return;
  }
  // Null when the feature is off and for every off-the-record profile. Both
  // are legitimate states: incognito deliberately has no service at all, so
  // there is nothing to show and nothing was collected (§5.2).
  //
  // Resolved from the ACTIVE TAB, like everything else in this panel: the
  // analysis being shown belongs to the page, so the service that produced it
  // has to be the page's, not the window's.
  PrivacyIntelligenceService* service =
      PrivacyIntelligenceServiceFactory::GetForBrowserContext(
          zephyrus::ActiveProfile(browser));
  if (!service) {
    return;
  }
  // The shield toggles: pressed again while the sheet is up, it closes it.
  if (PrivacySideSheet* const open_sheet =
          FindSheet(BrowserView::GetBrowserViewForBrowser(browser))) {
    if (!open_sheet->dismissing()) {
      open_sheet->Dismiss();
      return;
    }
  }
  content::WebContents* contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return;
  }

  // Absent until the page makes its first classified request, which is the
  // normal state on a page with no third parties — not an error. An empty
  // analysis is the correct answer there.
  std::vector<PrivacyTabHelper::DomainRow> rows;
  PrivacyTabHelper::PageTotals totals;
  if (auto* helper = PrivacyTabHelper::FromWebContents(contents)) {
    rows = helper->PageRows();
    totals = helper->totals();
  }

  const GURL page_url = contents->GetLastCommittedURL();
  std::u16string site =
      base::UTF8ToUTF16(PrivacyIntelligenceService::SiteKeyFor(page_url));
  // An IP address or a single-label host has no registrable domain, so its
  // site key is EMPTY -- and every string that names the site rendered with a
  // hole in it: "Turns off all blocking on  and reloads the page." The host is
  // the honest name for such a page, and it is also exactly what the adblock
  // allowlist matches (ZephyrusAdblockService::IsAllowlisted compares hosts),
  // so the switch now works there instead of silently doing nothing.
  if (site.empty()) {
    site = base::UTF8ToUTF16(page_url.host());
  }

  // Read now rather than in the panel: it describes the page the analysis was
  // taken on, and the user could allowlist something else during the hop.
  bool blocking_off = false;
  auto* const adblock =
      zephyrus_adblock::ZephyrusAdblockServiceFactory::GetForBrowserContext(
          zephyrus::ActiveProfile(browser));
  if (adblock) {
    blocking_off = adblock->IsAllowlisted(page_url);
  }

  service->AnalyzeCurrentPage(
      page_url, std::move(rows), totals,
      base::BindOnce(&ShowWithAnalysis, browser->AsWeakPtr(),
                     service->GetPageSignals(page_url), site, blocking_off,
                     /*can_allow=*/adblock != nullptr));
}

views::View* GetPrivacySheetForTesting(Browser* browser) {
  return FindSheet(BrowserView::GetBrowserViewForBrowser(browser));
}

void ClosePrivacySheetForTesting(Browser* browser) {
  if (PrivacySideSheet* const sheet =
          FindSheet(BrowserView::GetBrowserViewForBrowser(browser))) {
    sheet->RemoveFromWindow();
  }
}

}  // namespace zephyrus_privacy

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_privacy_popup.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/i18n/message_formatter.h"
#include "base/i18n/number_formatting.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
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
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_provider.h"
#include "ui/native_theme/native_theme.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_tracker.h"
#include "ui/views/widget/widget.h"

namespace zephyrus_privacy {
namespace {

constexpr int kWidth = 340;
// Layer 2 can be long. Cap it and scroll rather than growing a bubble taller
// than the window.
constexpr int kMaxListHeight = 320;

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
// Derived from the permanent theme by the same lift() model as the Shield panel
// and the workspace dropdown, so all three cards are literally the same
// material. See zephyrus-permanent-theme.
struct Palette {
  SkColor card;
  SkColor inset;
  SkColor fg;
  SkColor muted;
  SkColor faint;
  SkColor accent;
};

Palette MakePalette() {
  const SkColor base = zephyrus::Ground();
  const SkColor overlay =
      color_utils::IsDark(base) ? SK_ColorWHITE : SK_ColorBLACK;
  auto lift = [&](SkAlpha a) {
    return color_utils::AlphaBlend(overlay, base, a);
  };
  Palette p;
  p.card = lift(0x22);
  p.inset = lift(0x2E);
  p.fg = zephyrus::InkFor(base);
  p.muted = SkColorSetA(p.fg, 0xB0);
  p.faint = SkColorSetA(p.fg, 0x8A);
  p.accent = zephyrus::Accent();

  // §14.2: respect an increased-contrast preference, including Windows High
  // Contrast. What actually fails for those users is not the layout but the
  // alpha-softened text above — `faint` at 0x8A is the whole score breakdown
  // and every category label, and it is deliberately quiet. Quiet is the wrong
  // answer for someone who asked the system to make things louder, so the
  // secondary tiers collapse to full-strength foreground and the surfaces
  // separate more.
  //
  // Done by adjusting this palette rather than by pulling ColorProvider
  // colours: the panel has no widget yet at construction, so it has no colour
  // provider to ask.
  const ui::NativeTheme* theme = ui::NativeTheme::GetInstanceForNativeUi();
  // preferred_contrast() alone: it already reports kMore under Windows High
  // Contrast, and NativeTheme::IsForcedHighContrast() is protected.
  const bool high_contrast =
      theme && theme->preferred_contrast() !=
                   ui::NativeTheme::PreferredContrast::kNoPreference;
  if (high_contrast) {
    p.muted = p.fg;
    p.faint = p.fg;
    p.card = base;
    p.inset = lift(0x40);
    p.accent = p.fg;
  }
  return p;
}

std::unique_ptr<views::Label> MakeLabel(const std::u16string& text,
                                        SkColor color,
                                        int size_delta,
                                        gfx::Font::Weight weight) {
  auto label = std::make_unique<views::Label>(text);
  label->SetEnabledColor(color);
  label->SetAutoColorReadabilityEnabled(false);
  label->SetSubpixelRenderingEnabled(false);
  // ALIGN_TO_HEAD, not ALIGN_LEFT: in an RTL locale the whole panel
  // mirrors, and a hard left alignment would strand every label on the
  // wrong edge of its own row (§14).
  label->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
  label->SetMultiLine(false);
  label->SetFontList(
      label->font_list().DeriveWithSizeDelta(size_delta).DeriveWithWeight(
          weight));
  return label;
}

// The whole panel: both layers, with only one visible at a time.
class PrivacyPanel : public views::View {
  METADATA_HEADER(PrivacyPanel, views::View)

 public:
  PrivacyPanel(base::WeakPtr<Browser> browser,
               PrivacyIntelligenceService::PageAnalysis analysis,
               PrivacyIntelligenceService::PageSignals signals,
               std::u16string site,
               bool blocking_off)
      : browser_(std::move(browser)),
        analysis_(std::move(analysis)),
        signals_(signals),
        site_(std::move(site)),
        blocking_off_(blocking_off),
        palette_(MakePalette()) {
    SetBackground(views::CreateRoundedRectBackground(palette_.card,
                                                     zephyrus::kCornerRadius));
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(18, 18), 0))
        ->set_cross_axis_alignment(
            views::BoxLayout::CrossAxisAlignment::kStretch);

    summary_ = AddChildView(BuildSummary());
    details_ = AddChildView(BuildDetails());
    details_->SetVisible(false);
  }

 private:
 public:
  // §14.2: "screen reader announcement of counts on popup open". Called once
  // the widget is visible — announcing before that is dropped by the platform.
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

  void ShowLayer(bool details) {
    summary_->SetVisible(!details);
    details_->SetVisible(details);
    // The bubble is autosize, so it follows. Announce the swap: without this a
    // screen-reader user gets a silent change of everything below the header.
    // The two branches must say DIFFERENT things — an earlier version announced
    // the same string either way, which is the same as announcing nothing.
    GetViewAccessibility().AnnounceText(
        details ? l10n_util::GetStringFUTF16(
                      IDS_ZEPHYRUS_PRIVACY_ANNOUNCE_DETAILS,
                      base::FormatNumber(
                          static_cast<int>(analysis_.rows.size())))
                : SummaryAnnouncement());
    PreferredSizeChanged();
  }

  // §6.1, four lines.
  std::unique_ptr<views::View> BuildSummary() {
    auto view = std::make_unique<views::View>();
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    col->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    // Header: shield glyph + title.
    auto* header = view->AddChildView(std::make_unique<views::View>());
    auto* hl = header->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 9));
    hl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    header->AddChildView(std::make_unique<views::ImageView>(
        ui::ImageModel::FromVectorIcon(kZephyrusShieldIcon, palette_.accent,
                                       18)));
    auto* title = header->AddChildView(
        MakeLabel(l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_TITLE),
                  palette_.fg, 2, gfx::Font::Weight::SEMIBOLD));
    hl->SetFlexForView(title, 1);

    AddSpacer(view.get(), 12);

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
    view->AddChildView(MakeLabel(l10n_util::GetStringUTF16(headline_id),
                                 palette_.fg, 3, gfx::Font::Weight::MEDIUM));

    // Without this, a site the user allowed shows few or no trackers and reads
    // exactly like a clean one. The absence of blocking has to be stated, or
    // the panel silently takes credit for a page it is not protecting.
    if (blocking_off_) {
      auto* notice = view->AddChildView(MakeLabel(
          l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_ALLOWED_NOTICE),
          palette_.fg, -1, gfx::Font::Weight::NORMAL));
      notice->SetMultiLine(true);
      notice->SetMaximumWidth(kWidth - 36);
    }

    AddSpacer(view.get(), 10);

    if (analysis_.blocked > 0) {
      view->AddChildView(
          MakeLabel(Plural(IDS_ZEPHYRUS_PRIVACY_TRACKERS_BLOCKED,
                           analysis_.blocked),
                    palette_.muted, 0, gfx::Font::Weight::NORMAL));
    }
    if (signals_.fingerprint_surface_count() > 0) {
      // DETECTED, not randomized. §6.5's randomization is Phase 4 work, and
      // saying "randomized" before it exists would promise a protection the
      // browser is not performing.
      view->AddChildView(
          MakeLabel(Plural(IDS_ZEPHYRUS_PRIVACY_FINGERPRINT_DETECTED,
                           signals_.fingerprint_surface_count()),
                    palette_.muted, 0, gfx::Font::Weight::NORMAL));
    }
    if (signals_.webrtc_address_requests > 0) {
      // The reassuring half of this sentence appears only when the policy in
      // force actually withheld local addresses (§9.2.1).
      auto* webrtc = view->AddChildView(MakeLabel(
          l10n_util::GetStringUTF16(
              signals_.local_addresses_withheld
                  ? IDS_ZEPHYRUS_PRIVACY_WEBRTC_WITHHELD
                  : IDS_ZEPHYRUS_PRIVACY_WEBRTC_EXPOSED),
          palette_.muted, 0, gfx::Font::Weight::NORMAL));
      webrtc->SetMultiLine(true);
      webrtc->SetMaximumWidth(kWidth - 36);
    }

    AddSpacer(view.get(), 14);

    // Tracking Intensity, then the arithmetic behind it.
    view->AddChildView(
        MakeLabel(l10n_util::GetStringFUTF16(
                      IDS_ZEPHYRUS_PRIVACY_INTENSITY,
                      BandString(analysis_.intensity.band)),
                  palette_.fg, 1, gfx::Font::Weight::MEDIUM));
    if (analysis_.protection.has_value()) {
      view->AddChildView(MakeLabel(
          l10n_util::GetStringFUTF16(
              IDS_ZEPHYRUS_PRIVACY_PROTECTION_APPLIED,
              base::FormatNumber(analysis_.protection->percent)),
          palette_.faint, -1, gfx::Font::Weight::NORMAL));
    }

    AddSpacer(view.get(), 10);
    view->AddChildView(BuildArithmetic());

    view->AddChildView(BuildWhatWasProtected());

    AddSpacer(view.get(), 12);
    view->AddChildView(BuildQuickControls());

    AddSpacer(view.get(), 14);

    auto* more = view->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&PrivacyPanel::ShowLayer, base::Unretained(this),
                            true),
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_SEE_WHAT_HAPPENED)));
    more->SetEnabledTextColors(palette_.accent);
    // §14.2 keyboard navigation. Explicit rather than inherited: a bubble whose
    // controls are not in the focus ring is unreachable without a mouse.
    more->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    more->SetInstallFocusRingOnFocus(true);
    // ALIGN_LEFT, not ALIGN_TO_HEAD: LabelButton DCHECKs against
    // ALIGN_TO_HEAD because it mirrors for RTL itself, unlike Label.
    more->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    return view;
  }

  // §6.3: "no weights the user cannot inspect. Show the arithmetic on tap."
  // Rendered straight from the breakdown the score function returned, so there
  // is no second copy of the weights to drift out of step.
  std::unique_ptr<views::View> BuildArithmetic() {
    auto view = std::make_unique<views::View>();
    view->SetBackground(views::CreateRoundedRectBackground(
        palette_.inset, zephyrus::kCornerRadius));
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(10, 12), 3));
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
      auto* name =
          row->AddChildView(MakeLabel(l10n_util::GetStringUTF16(term.message_id),
                                      palette_.faint, -1,
                                      gfx::Font::Weight::NORMAL));
      rl->SetFlexForView(name, 1);
      row->AddChildView(MakeLabel(base::FormatNumber(t.observed),
                                  palette_.muted, -1,
                                  gfx::Font::Weight::MEDIUM));
      auto* points = row->AddChildView(MakeLabel(
          l10n_util::GetStringFUTF16(IDS_ZEPHYRUS_PRIVACY_TERM_POINTS,
                                     base::FormatNumber(t.points),
                                     base::FormatNumber(t.max_points)),
          palette_.faint, -2, gfx::Font::Weight::NORMAL));
      // Trailing edge. gfx has no ALIGN_TO_TAIL, so this is flipped
      // explicitly for RTL rather than left as a hard right.
      points->SetHorizontalAlignment(
          base::i18n::IsRTL() ? gfx::ALIGN_LEFT : gfx::ALIGN_RIGHT);
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

    AddSpacer(view.get(), 14);
    view->AddChildView(MakeLabel(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_PROTECTED_HEADING),
        palette_.fg, 0, gfx::Font::Weight::MEDIUM));
    AddSpacer(view.get(), 4);

    auto* list = view->AddChildView(std::make_unique<views::View>());
    list->GetViewAccessibility().SetRole(ax::mojom::Role::kList);
    auto* ll = list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    ll->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    for (const ProtectedItem& item : items) {
      auto* row = list->AddChildView(std::make_unique<views::View>());
      auto* rl = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
      const std::u16string name = ProtectedCategoryString(item.category);
      const std::u16string count = base::FormatNumber(
          static_cast<int>(item.count));
      auto* label =
          row->AddChildView(MakeLabel(name, palette_.muted, -1,
                                      gfx::Font::Weight::NORMAL));
      rl->SetFlexForView(label, 1);
      row->AddChildView(
          MakeLabel(count, palette_.accent, -1, gfx::Font::Weight::MEDIUM));
      row->GetViewAccessibility().SetRole(ax::mojom::Role::kListItem);
      row->GetViewAccessibility().SetName(
          base::JoinString({name, count}, u", "));
    }
    return view;
  }

  // §6 quick controls. One control for now: the breakage escape hatch.
  std::unique_ptr<views::View> BuildQuickControls() {
    auto view = std::make_unique<views::View>();
    view->SetBackground(views::CreateRoundedRectBackground(
        palette_.inset, zephyrus::kCornerRadius));
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(10, 12), 3));
    col->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* row = view->AddChildView(std::make_unique<views::View>());
    auto* rl = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 10));
    rl->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* label = row->AddChildView(
        MakeLabel(l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_ALLOW_SITE),
                  palette_.fg, -1, gfx::Font::Weight::MEDIUM));
    rl->SetFlexForView(label, 1);

    auto* toggle = row->AddChildView(std::make_unique<views::ToggleButton>(
        base::BindRepeating(&PrivacyPanel::OnAllowSiteToggled,
                            base::Unretained(this))));
    toggle->SetIsOn(blocking_off_);
    toggle->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    toggle->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_ALLOW_SITE));
    allow_toggle_ = toggle;

    auto* hint = view->AddChildView(MakeLabel(
        l10n_util::GetStringFUTF16(IDS_ZEPHYRUS_PRIVACY_ALLOW_SITE_HINT, site_),
        palette_.faint, -2, gfx::Font::Weight::NORMAL));
    hint->SetMultiLine(true);
    hint->SetMaximumWidth(kWidth - 60);
    return view;
  }

  void OnAllowSiteToggled() {
    if (!browser_ || !allow_toggle_ || site_.empty()) {
      return;
    }
    const bool allow = allow_toggle_->GetIsOn();
    // The ACTIVE TAB's profile, not the window's.
    //
    // This popup already reads the site from the active tab, and used to write
    // the result to the window -- so in a window holding two workspaces you
    // could allow a site while looking at one profile's page and have the
    // allowlist entry land in the other. Half-correct is harder to spot than
    // wholly wrong: the popup showed the right site name the whole time.
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
    if (GetWidget()) {
      GetWidget()->Close();
    }
  }

  // §6.2, per-tracker rows.
  std::unique_ptr<views::View> BuildDetails() {
    auto view = std::make_unique<views::View>();
    auto* col = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    col->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto* back = view->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&PrivacyPanel::ShowLayer, base::Unretained(this),
                            false),
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_BACK)));
    back->SetEnabledTextColors(palette_.accent);
    back->SetFocusBehavior(views::View::FocusBehavior::ALWAYS);
    back->SetInstallFocusRingOnFocus(true);
    // ALIGN_LEFT, not ALIGN_TO_HEAD: LabelButton DCHECKs against
    // ALIGN_TO_HEAD because it mirrors for RTL itself, unlike Label.
    back->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    AddSpacer(view.get(), 6);
    view->AddChildView(
        MakeLabel(site_, palette_.fg, 2, gfx::Font::Weight::SEMIBOLD));
    view->AddChildView(MakeLabel(
        l10n_util::GetStringFUTF16(
            IDS_ZEPHYRUS_PRIVACY_SUMMARY_COUNTS,
            base::FormatNumber(analysis_.detected),
            base::FormatNumber(analysis_.blocked),
            base::FormatNumber(analysis_.allowed)),
        palette_.faint, -1, gfx::Font::Weight::NORMAL));

    AddSpacer(view.get(), 10);

    if (analysis_.rows.empty()) {
      view->AddChildView(
          MakeLabel(l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_NO_REQUESTS),
                    palette_.muted, 0, gfx::Font::Weight::NORMAL));
      return view;
    }

    // §14.2: "the tracker list is a table with headers, not a stack of divs."
    // Built from Views rather than a views::TableView — a TableView cannot be
    // styled to match this panel — but carrying the table ROLES, which is what
    // a screen reader navigates by. Without them the reader gets four
    // unlabelled strings per row and no way to ask which column it is in.
    auto list = std::make_unique<views::View>();
    list->GetViewAccessibility().SetRole(ax::mojom::Role::kTable);
    list->GetViewAccessibility().SetName(
        l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_TITLE));
    auto* ll = list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    ll->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    list->AddChildView(BuildTrackerHeaderRow());
    for (const auto& row : analysis_.rows) {
      list->AddChildView(BuildTrackerRow(row));
    }

    auto* scroll = view->AddChildView(std::make_unique<views::ScrollView>());
    scroll->SetContents(std::move(list));
    scroll->ClipHeightTo(0, kMaxListHeight);
    scroll->SetDrawOverflowIndicator(false);
    scroll->SetBackgroundColor(std::nullopt);
    return view;
  }

  // The header row. Visible as well as exposed: sighted users need to know
  // what "12 req" is counting just as much as screen-reader users do.
  std::unique_ptr<views::View> BuildTrackerHeaderRow() {
    auto view = std::make_unique<views::View>();
    auto* rl = view->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(2, 0), 8));
    view->GetViewAccessibility().SetRole(ax::mojom::Role::kRow);

    auto add_header = [&](int message_id, int flex) {
      auto* label = view->AddChildView(
          MakeLabel(l10n_util::GetStringUTF16(message_id), palette_.faint, -2,
                    gfx::Font::Weight::MEDIUM));
      label->GetViewAccessibility().SetRole(ax::mojom::Role::kColumnHeader);
      if (flex) {
        rl->SetFlexForView(label, flex);
      }
      return label;
    };
    add_header(IDS_ZEPHYRUS_PRIVACY_COLUMN_TRACKER, 1);
    add_header(IDS_ZEPHYRUS_PRIVACY_COLUMN_CATEGORY, 0);
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
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(5, 0), 8));
    rl->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    // §4.2: an unknown domain renders as itself, with no invented owner and no
    // invented category. `entity_name` empty is the signal for that, and the
    // category column is simply "Unknown" rather than a guess.
    auto* names = view->AddChildView(std::make_unique<views::View>());
    names->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));
    const std::u16string primary = RowName(row);
    names->AddChildView(
        MakeLabel(primary, palette_.fg, -1, gfx::Font::Weight::MEDIUM));
    names->AddChildView(MakeLabel(CategoryString(row.category), palette_.faint,
                                  -2, gfx::Font::Weight::NORMAL));
    rl->SetFlexForView(names, 1);

    view->AddChildView(MakeLabel(Plural(IDS_ZEPHYRUS_PRIVACY_REQUESTS,
                                        row.requests),
                                 palette_.faint, -2,
                                 gfx::Font::Weight::NORMAL));
    // Blocked rows are the reassuring ones, so only they get the accent; a
    // row that let something through must not be coloured like a success.
    view->AddChildView(MakeLabel(StatusString(row.status),
                                 row.status == TrackerStatus::kBlocked
                                     ? palette_.accent
                                     : palette_.muted,
                                 -2, gfx::Font::Weight::MEDIUM));

    // kRow with kCell children, matching the kTable above, so the reader can
    // navigate by column and hear each cell against its header. The row also
    // carries the whole line as its name, for readers that announce the row
    // before descending into it.
    view->GetViewAccessibility().SetRole(ax::mojom::Role::kRow);
    view->GetViewAccessibility().SetName(base::JoinString(
        {primary, CategoryString(row.category),
         Plural(IDS_ZEPHYRUS_PRIVACY_REQUESTS, row.requests),
         StatusString(row.status)},
        u", "));
    for (views::View* cell : view->children()) {
      cell->GetViewAccessibility().SetRole(ax::mojom::Role::kCell);
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
    auto* why = wrap->AddChildView(MakeLabel(ExplainRow(row), palette_.faint,
                                             -2, gfx::Font::Weight::NORMAL));
    why->SetMultiLine(true);
    why->SetHorizontalAlignment(gfx::ALIGN_TO_HEAD);
    // The row above already announces the same facts as name, category, count
    // and status. Reading the sentence too would say everything twice, so it
    // is presentational to a screen reader and visible to everyone else.
    why->GetViewAccessibility().SetIsIgnored(true);
    return wrap;
  }

  // Weak: the panel outlives nothing, but the hop from a click to a reload
  // passes through the widget's own teardown, and the window can be closed
  // under it.
  base::WeakPtr<Browser> browser_;
  PrivacyIntelligenceService::PageAnalysis analysis_;
  PrivacyIntelligenceService::PageSignals signals_;
  std::u16string site_;
  bool blocking_off_ = false;
  Palette palette_;
  raw_ptr<views::ToggleButton> allow_toggle_ = nullptr;
  raw_ptr<views::View> summary_ = nullptr;
  raw_ptr<views::View> details_ = nullptr;
};

BEGIN_METADATA(PrivacyPanel)
END_METADATA

// Second half of ShowPrivacyPopup, once the analysis comes back.
void ShowWithAnalysis(std::unique_ptr<views::ViewTracker> anchor,
                      base::WeakPtr<Browser> browser,
                      PrivacyIntelligenceService::PageSignals signals,
                      std::u16string site,
                      bool blocking_off,
                      PrivacyIntelligenceService::PageAnalysis analysis) {
  // The user may have closed the window, switched tabs, or clicked elsewhere
  // during the hop to the privacy sequence. ViewTracker is what makes that
  // safe rather than a use-after-free.
  views::View* view = anchor->view();
  if (!view || !view->GetWidget() || !browser) {
    return;
  }

  auto panel = std::make_unique<PrivacyPanel>(std::move(browser),
                                              std::move(analysis), signals,
                                              std::move(site), blocking_off);
  PrivacyPanel* panel_ptr = panel.get();
  panel->SetPreferredSize(
      gfx::Size(kWidth, panel->GetHeightForWidth(kWidth)));

  auto bubble = std::make_unique<views::BubbleDialogDelegate>(
      view, views::BubbleBorder::TOP_RIGHT,
      views::BubbleBorder::STANDARD_SHADOW, /*autosize=*/true);
  bubble->SetShowCloseButton(false);
  bubble->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  bubble->set_margins(gfx::Insets());
  bubble->SetTitle(l10n_util::GetStringUTF16(IDS_ZEPHYRUS_PRIVACY_TITLE));
  bubble->SetShowTitle(false);
  zephyrus::ConfigureBubble(bubble.get());
  bubble->SetBackgroundColor(MakePalette().card);
  bubble->SetContentsView(std::move(panel));
  views::BubbleDialogDelegate* bubble_ptr = bubble.get();
  views::Widget* widget = views::BubbleDialogDelegate::CreateBubbleDeprecated(
      std::move(bubble), views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  zephyrus::ApplyBubbleFrame(bubble_ptr);
  widget->Show();
  // After Show(): an announcement made while the widget is still hidden is
  // dropped by the platform, so the counts would never be spoken (§14.2).
  panel_ptr->AnnounceOnOpen();
}

}  // namespace

void ShowPrivacyPopup(Browser* browser, views::View* anchor) {
  if (!browser || !anchor) {
    return;
  }
  // Null when the feature is off and for every off-the-record profile. Both
  // are legitimate states: incognito deliberately has no service at all, so
  // there is nothing to show and nothing was collected (§5.2).
  //
  // Resolved from the ACTIVE TAB, like everything else in this popup: the
  // analysis being shown belongs to the page, so the service that produced it
  // has to be the page's, not the window's.
  PrivacyIntelligenceService* service =
      PrivacyIntelligenceServiceFactory::GetForBrowserContext(
          zephyrus::ActiveProfile(browser));
  if (!service) {
    return;
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
  const std::u16string site = base::UTF8ToUTF16(
      PrivacyIntelligenceService::SiteKeyFor(page_url));

  // Read now rather than in the panel: it describes the page the analysis was
  // taken on, and the user could allowlist something else during the hop.
  bool blocking_off = false;
  if (auto* adblock =
          zephyrus_adblock::ZephyrusAdblockServiceFactory::GetForBrowserContext(
              zephyrus::ActiveProfile(browser))) {
    blocking_off = adblock->IsAllowlisted(page_url);
  }

  service->AnalyzeCurrentPage(
      page_url, std::move(rows), totals,
      base::BindOnce(&ShowWithAnalysis,
                     std::make_unique<views::ViewTracker>(anchor),
                     browser->AsWeakPtr(), service->GetPageSignals(page_url),
                     site, blocking_off));
}

}  // namespace zephyrus_privacy

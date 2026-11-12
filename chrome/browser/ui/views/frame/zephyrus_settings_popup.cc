// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_browser_compat.h"
#include "chrome/browser/ui/views/frame/zephyrus_settings_popup.h"

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_bubble_style.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "build/build_config.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/field_trial_params.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/autofill/autofill_client_provider.h"
#include "chrome/browser/ui/autofill/autofill_client_provider_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/keyboard_event_processing_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_flags.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/base/page_transition_types.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_element.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/cubic_bezier.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/geometry/transform_util.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/skbitmap_operations.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/window/non_client_view.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/box_layout_view.h"
#include "ui/views/vector_icons.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

// The single active popup (at most one across all windows).
ZephyrusSettingsPopup* g_active_popup = nullptr;

// POPUP radius, not card. This sheet floats over the window, so it takes M3's
// dialog/sheet step (28) and now matches every upstream dialog, which
// LayoutProvider maps to the same value.
//
// It asked for kRadiusCard before, which was simply the wrong constant for what
// this is -- an 8px sheet reads as a clipped rectangle rather than something
// cast on top of the window.
//
// The DPI constraint that shaped the old value still holds and 28 satisfies it:
// this radius reaches the WebView's layer via holder()->SetCornerRadii(), and a
// layer-rounded corner must be a whole number of device pixels or the curve
// renders blurry at fractional scaling. 28 is a multiple of 4, so it is whole
// at 1.25x (35), 1.5x (42) and 2x (56).
constexpr int kPopupCornerRadius = zephyrus::kRadiusPopup;
constexpr int kRailWidth = 248;
constexpr int kRailInset = 12;

// The rail is an M3 EXPRESSIVE SEGMENTED LIST. A list is one rounded container
// made of separate tonal rows, 2dp apart: the first and last visible row of a
// list take the large outer radius and every row between takes the small inner
// one, so expanding a group re-rounds the list around what appeared. A row is a
// label with supporting text, and a group row ends in a chevron.
constexpr int kListGap = 8;
constexpr int kRowGap = 2;
constexpr int kOuterRadius = 16;
constexpr int kInnerRadius = 4;
constexpr int kRowMinHeight = 52;
constexpr int kChildRowMinHeight = 38;
constexpr int kRowPaddingH = 14;
constexpr int kChildIndent = 10;
constexpr int kChevronSize = 12;

// The close button: a 32dp circle around a 24dp icon. 32 with the rail's 12dp
// inset is not arbitrary: the popup's corner is 28, so a circle of radius 16
// 12dp from both edges is concentric with it (inner radius = outer radius -
// padding = 28 - 12 = 16), 12dp clear of the corner's arc all the way round.
// (At the M3 default 40 it sat 2dp off the arc.)
constexpr int kCloseButtonSize = 32;
constexpr int kCloseIconSize = 24;

// Effectively unbounded: ScrollView::ClipHeightTo needs a real maximum.
constexpr int kUnboundedScrollHeight = 100000;

// The pre-warm (see ZephyrusSettingsPopup::SchedulePrewarm). A feature so it can
// be switched off, and A/B-measured, from the command line:
//   --disable-features=ZephyrusSettingsPopupPrewarm
//   --enable-features=ZephyrusSettingsPopupPrewarm:delay_ms/1500/webview/true
BASE_FEATURE(kSettingsPopupPrewarm,
             "ZephyrusSettingsPopupPrewarm",
             base::FEATURE_ENABLED_BY_DEFAULT);
// How long after a window first activates. Startup is busy; the popup is not
// wanted for the first seconds anyway.
const base::FeatureParam<int> kPrewarmDelayMs{&kSettingsPopupPrewarm,
                                              "delay_ms", 5000};
// Host a real (about:blank) WebView in the stand-in rather than a plain layer.
const base::FeatureParam<bool> kPrewarmWebView{&kSettingsPopupPrewarm,
                                               "webview", false};
// ~1/255: at 8 bits per channel it moves a pixel by at most one level, which no
// one can see, but it is not zero, and the compositor skips a layer whose
// opacity is zero (so zero would compile nothing).
constexpr float kPrewarmOpacity = 0.004f;
constexpr base::TimeDelta kPrewarmLifetime = base::Milliseconds(800);
// The rail's width plus a sliver of page; tall enough for a few rail rows.
constexpr gfx::Size kPrewarmSize(kRailWidth + 200, 480);

bool g_prewarm_scheduled = false;
bool g_prewarm_done = false;

// The popup's entrance: fade in while the card scales up from 96%, overshoots to
// ~101.5%, and settles -- an under-damped spring about the centre (modals are
// not anchored to a trigger, so a centred origin is correct). Runs on the
// compositor (opacity/transform only). |target_opacity| is 1 for the real popup.
void StartEntranceAnimation(ui::Layer* layer, float target_opacity) {
  const gfx::Transform settled = layer->transform();
  const gfx::Point center = gfx::Rect(layer->bounds().size()).CenterPoint();
  layer->SetOpacity(0.0f);
  layer->SetTransform(gfx::GetScaleTransform(center, 0.96f));
  ui::LayerAnimator* animator = layer->GetAnimator();
  auto fade = ui::LayerAnimationElement::CreateOpacityElement(
      target_opacity, base::Milliseconds(150));
  fade->set_tween_type(
      zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastEffects));
  animator->StartAnimation(new ui::LayerAnimationSequence(std::move(fade)));
  auto grow = ui::LayerAnimationElement::CreateTransformElement(
      gfx::GetScaleTransform(center, 1.015f), base::Milliseconds(170));
  grow->set_tween_type(
      zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
  auto settle = ui::LayerAnimationElement::CreateTransformElement(
      settled, base::Milliseconds(140));
  settle->set_tween_type(
      zephyrus::m3::TweenFor(zephyrus::m3::Spring::kFastSpatial));
  auto* spring = new ui::LayerAnimationSequence(std::move(grow));
  spring->AddElement(std::move(settle));
  animator->StartAnimation(spring);
}

// Colours are ROLES, and a role can only be read from a view that is in a
// Widget.
//
// The rail and the page base are the SAME role, and that is measured, not a
// shortcut. The base was `surface` for a tonal split against the rail, and it
// produced a 1px dark line instead: at 1.5x the web contents lands a device
// pixel inside the WebView, so the WebView's own background shows at the edge
// — 19,19,20 between a rail at 31,32,32 and a page at 32,33,36. The page
// paints its OWN background, which is Chromium's WebUI colour and not ours,
// so a split here cannot show until WebUI is aligned (overhaul Phase 6).
// Until then the honest result is one dialog surface, with the base matching
// it so neither the sliver nor the pre-paint flash stands out.
SkColor RailColor(const views::View& view) {
  return zephyrus::m3::Role(view, kColorZephyrusSurfaceContainer);
}
SkColor ContentBase(const views::View& view) {
  return RailColor(view);
}

// What the rail lists. Every Chromium settings section is a row, and the
// sections that have subpages are groups that expand to show them. A row
// navigates the embedded page to its URL; "settings" rows are WebUI routes.
// Routes are the ones route.ts defines -- a route a build hides simply lands on
// the settings root, which is why the rail is verified against a real window
// rather than trusted.
struct ChildSpec {
  const char16_t* title;
  const char* url;
};

struct EntrySpec {
  const char16_t* title;
  const char16_t* supporting;
  const char* url;
  // Only ever points at the constant arrays below, which live for the whole
  // program, so there is nothing for a raw_span to protect.
  RAW_PTR_EXCLUSION base::span<const ChildSpec> children;
};

constexpr ChildSpec kAutofillChildren[] = {
    {u"Payment methods", "chrome://settings/payments"},
    {u"Addresses and more", "chrome://settings/contactInfo"},
    {u"Passkeys", "chrome://settings/passkeys"},
};
constexpr ChildSpec kPrivacyChildren[] = {
    {u"Clear browsing data", "chrome://settings/clearBrowserData"},
    {u"Cookies and site data", "chrome://settings/cookies"},
    {u"Security", "chrome://settings/security"},
    {u"Site settings", "chrome://settings/content"},
    {u"Safety check", "chrome://settings/safetyCheck"},
};
constexpr ChildSpec kAppearanceChildren[] = {
    {u"Fonts and customization", "chrome://settings/fonts"},
};
constexpr ChildSpec kSearchChildren[] = {
    {u"Manage search engines", "chrome://settings/searchEngines"},
};

constexpr EntrySpec kSettingsList[] = {
    {u"All settings", u"Everything on one page", "chrome://settings", {}},
    {u"Autofill and passwords", u"Cards and addresses",
     "chrome://settings/autofill", kAutofillChildren},
    {u"Privacy and security", u"Data and permissions",
     "chrome://settings/privacy", kPrivacyChildren},
    {u"Performance", u"Memory and energy", "chrome://settings/performance", {}},
    {u"Appearance", u"Theme, layout and fonts", "chrome://settings/appearance",
     kAppearanceChildren},
    {u"Shield", u"Ads, trackers and Shield DNS", "chrome://settings/adBlocker",
     {}},
    {u"Search engine", u"Default and site search", "chrome://settings/search",
     kSearchChildren},
    {u"Default browser", u"Make Zephyrus your default",
     "chrome://settings/defaultBrowser", {}},
    {u"On startup", u"What opens at launch", "chrome://settings/onStartup", {}},
    {u"Languages", u"Display and spelling", "chrome://settings/languages", {}},
    {u"Download settings", u"Location and prompts",
     "chrome://settings/downloads", {}},
    {u"Accessibility", u"Zoom, captions and readers",
     "chrome://settings/accessibility", {}},
    {u"System", u"Hardware and proxy", "chrome://settings/system", {}},
    {u"Reset settings", u"Restore the defaults", "chrome://settings/reset", {}},
};

constexpr EntrySpec kContentList[] = {
    {u"History", u"Pages you visited", "chrome://history", {}},
    {u"Downloads", u"Files you downloaded", "chrome://downloads", {}},
    {u"Bookmarks", u"Your saved pages", "chrome://bookmarks", {}},
    {u"Passwords", u"Saved sign-ins", "chrome://password-manager", {}},
    {u"Extensions", u"Add-ons and their access", "chrome://extensions", {}},
};

constexpr EntrySpec kAboutList[] = {
    {u"About Zephyrus", u"Version and updates", "chrome://settings/help", {}},
};

// A row of the segmented list. It paints its own tonal fill with a different
// radius on each corner (the list decides which), and its content colours are
// roles, so they are applied in OnThemeChanged once the view is in a Widget.
class RailRow : public views::Button {
  METADATA_HEADER(RailRow, views::Button)

 public:
  RailRow(PressedCallback callback,
          const std::u16string& title,
          const std::u16string& supporting,
          bool is_child,
          bool is_group)
      : views::Button(std::move(callback)),
        is_group_(is_group),
        title_text_(title) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(is_child ? 6 : 8,
                          kRowPaddingH + (is_child ? kChildIndent : 0),
                          is_child ? 6 : 8, kRowPaddingH),
        kRowPaddingH));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    layout->set_minimum_cross_axis_size(is_child ? kChildRowMinHeight
                                                 : kRowMinHeight);

    auto* text = AddChildView(std::make_unique<views::BoxLayoutView>());
    text->SetOrientation(views::BoxLayout::Orientation::kVertical);
    text->SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    // The row is the click target; its labels must not swallow the press.
    text->SetCanProcessEventsWithinSubtree(false);
    layout->SetFlexForView(text, 1);
    title_ = text->AddChildView(std::make_unique<views::Label>(title));
    StyleLabel(title_, is_child ? zephyrus::m3::Type::kBodyMedium
                                : zephyrus::m3::Type::kLabelLarge);
    if (!supporting.empty()) {
      supporting_ =
          text->AddChildView(std::make_unique<views::Label>(supporting));
      StyleLabel(supporting_, zephyrus::m3::Type::kBodySmall);
    }
    if (is_group_) {
      chevron_ = AddChildView(std::make_unique<views::ImageView>());
      chevron_->SetCanProcessEventsWithinSubtree(false);
    }

    views::FocusRing::Install(this);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(), 12);
    UpdateAccessibleName();
  }

  void SetCorners(int top_radius, int bottom_radius) {
    if (top_radius_ == top_radius && bottom_radius_ == bottom_radius) {
      return;
    }
    top_radius_ = top_radius;
    bottom_radius_ = bottom_radius;
    SchedulePaint();
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    UpdateColors();
    SchedulePaint();
  }

  void SetExpanded(bool expanded) {
    if (expanded_ == expanded) {
      return;
    }
    expanded_ = expanded;
    UpdateColors();
    UpdateAccessibleName();
  }

  // views::Button:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
  }

  void OnThemeChanged() override {
    views::Button::OnThemeChanged();
    UpdateColors();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    if (!GetWidget()) {
      return;
    }
    const SkColor content = Ink();
    SkColor fill =
        zephyrus::m3::Role(*this, selected_
                                      ? kColorZephyrusSecondaryContainer
                                      : kColorZephyrusSurfaceContainerHigh);
    const bool hovered = GetState() == views::Button::STATE_HOVERED;
    const bool pressed = GetState() == views::Button::STATE_PRESSED;
    if (hovered || pressed) {
      fill = zephyrus::m3::WithStateLayer(
          fill, content, pressed ? zephyrus::m3::kPressed : zephyrus::m3::kHover);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill);
    const gfx::RectF bounds(GetLocalBounds());
    const float top = static_cast<float>(top_radius_);
    const float bottom = static_cast<float>(bottom_radius_);
    const SkVector radii[4] = {{top, top}, {top, top}, {bottom, bottom},
                               {bottom, bottom}};
    SkRRect rrect;
    rrect.setRectRadii(gfx::RectFToSkRect(bounds), radii);
    canvas->sk_canvas()->drawRRect(rrect, flags);
  }

 private:
  static void StyleLabel(views::Label* label, zephyrus::m3::Type type) {
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetSubpixelRenderingEnabled(false);
    label->SetFontList(zephyrus::m3::Font(type));
  }

  SkColor Ink() const {
    return zephyrus::m3::Role(*this, selected_
                                         ? kColorZephyrusOnSecondaryContainer
                                         : kColorZephyrusOnSurface);
  }

  SkColor SupportingInk() const {
    return zephyrus::m3::Role(*this, selected_
                                         ? kColorZephyrusOnSecondaryContainer
                                         : kColorZephyrusOnSurfaceVariant);
  }

  void UpdateColors() {
    if (!GetWidget()) {
      return;
    }
    title_->SetEnabledColor(Ink());
    if (supporting_) {
      supporting_->SetEnabledColor(SupportingInk());
    }
    if (chevron_) {
      const gfx::ImageSkia icon = gfx::CreateVectorIcon(
          kZephyrusDropdownIcon, kChevronSize, SupportingInk());
      chevron_->SetImage(ui::ImageModel::FromImageSkia(
          expanded_ ? gfx::ImageSkiaOperations::CreateRotatedImage(
                          icon, SkBitmapOperations::ROTATION_180_CW)
                    : icon));
    }
  }

  void UpdateAccessibleName() {
    std::u16string name = title_text_;
    if (is_group_) {
      name += expanded_ ? u", expanded" : u", collapsed";
    }
    GetViewAccessibility().SetName(name);
  }

  const bool is_group_;
  const std::u16string title_text_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> supporting_ = nullptr;
  raw_ptr<views::ImageView> chevron_ = nullptr;
  int top_radius_ = kInnerRadius;
  int bottom_radius_ = kInnerRadius;
  bool selected_ = false;
  bool expanded_ = false;
};

BEGIN_METADATA(RailRow)
END_METADATA

// How well |item| (a rail row's URL) stands for the page at |url|; -1 means
// not at all. The longest matching path wins, so a subpage selects its own row
// rather than its section's, and a row with no path stands for its whole host.
int MatchScore(const GURL& item, const GURL& url) {
  if (!url.SchemeIs("chrome") || item.host() != url.host()) {
    return -1;
  }
  auto trim = [](std::string_view path) {
    while (!path.empty() && path.back() == '/') {
      path.remove_suffix(1);
    }
    return path;
  };
  const std::string_view item_path = trim(item.path());
  const std::string_view url_path = trim(url.path());
  if (item_path.empty()) {
    if (item.host() != "settings") {
      return 1;
    }
    return url_path.empty() ? 1000 : -1;
  }
  if (item_path == url_path) {
    return 1000 + static_cast<int>(item_path.size());
  }
  if (url_path.size() > item_path.size() &&
      url_path.substr(0, item_path.size()) == item_path &&
      url_path[item_path.size()] == '/') {
    return static_cast<int>(item_path.size());
  }
  return -1;
}

// The whole rail: three segmented lists (settings, the user's content, about).
// It owns the expansion state and the selection, and re-rounds each list
// whenever either changes.
class SettingsRail : public views::BoxLayoutView {
  METADATA_HEADER(SettingsRail, views::BoxLayoutView)

 public:
  using NavigateCallback = base::RepeatingCallback<void(const GURL&)>;

  explicit SettingsRail(NavigateCallback navigate)
      : navigate_(std::move(navigate)) {
    SetOrientation(views::BoxLayout::Orientation::kVertical);
    SetCrossAxisAlignment(views::BoxLayout::CrossAxisAlignment::kStretch);
    SetBetweenChildSpacing(kListGap);
    AddList(kSettingsList);
    AddList(kContentList);
    AddList(kAboutList);
    ApplyLayout();
  }
  SettingsRail(const SettingsRail&) = delete;
  SettingsRail& operator=(const SettingsRail&) = delete;
  ~SettingsRail() override = default;

  // Selects the row that stands for the page at |url|, and opens its group.
  void SelectURL(const GURL& url) {
    size_t best = kNone;
    int best_score = -1;
    for (size_t i = 0; i < items_.size(); ++i) {
      const int score = MatchScore(items_[i].url, url);
      if (score > best_score) {
        best = i;
        best_score = score;
      }
    }
    // A settings page no row stands for (sync, say) still belongs to the
    // settings, so it selects the first row rather than leaving none lit.
    if (best == kNone && url.SchemeIs("chrome") && url.host() == "settings") {
      best = 0;
    }
    selected_ = best;
    if (best != kNone) {
      if (items_[best].parent != kNone) {
        items_[items_[best].parent].expanded = true;
      } else if (items_[best].is_group) {
        items_[best].expanded = true;
      }
    }
    ApplyLayout();
  }

 private:
  static constexpr size_t kNone = static_cast<size_t>(-1);

  struct Item {
    raw_ptr<RailRow> row = nullptr;
    GURL url;
    size_t parent = kNone;
    size_t list = 0;
    bool is_group = false;
    bool expanded = false;
  };

  void AddList(base::span<const EntrySpec> entries) {
    const size_t list_index = lists_++;
    auto* list = AddChildView(std::make_unique<views::BoxLayoutView>());
    list->SetOrientation(views::BoxLayout::Orientation::kVertical);
    list->SetCrossAxisAlignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    list->SetBetweenChildSpacing(kRowGap);
    for (const EntrySpec& entry : entries) {
      const size_t parent_index = AddRow(
          list, list_index, entry.title, entry.supporting, GURL(entry.url),
          /*parent=*/kNone, /*is_group=*/!entry.children.empty());
      for (const ChildSpec& child : entry.children) {
        AddRow(list, list_index, child.title, u"", GURL(child.url),
               parent_index, /*is_group=*/false);
      }
    }
  }

  size_t AddRow(views::View* list,
                size_t list_index,
                const std::u16string& title,
                const std::u16string& supporting,
                const GURL& url,
                size_t parent,
                bool is_group) {
    const size_t index = items_.size();
    auto* row = list->AddChildView(std::make_unique<RailRow>(
        base::BindRepeating(&SettingsRail::Activate, base::Unretained(this),
                            index),
        title, supporting, /*is_child=*/parent != kNone, is_group));
    Item item;
    item.row = row;
    item.url = url;
    item.parent = parent;
    item.list = list_index;
    item.is_group = is_group;
    items_.push_back(std::move(item));
    return index;
  }

  void Activate(size_t index, const ui::Event&) {
    Item& item = items_[index];
    if (item.is_group) {
      if (selected_ == index) {
        // The group's own page is already showing: the click only folds or
        // unfolds it.
        item.expanded = !item.expanded;
        ApplyLayout();
        return;
      }
      item.expanded = true;
    }
    selected_ = index;
    ApplyLayout();
    navigate_.Run(item.url);
  }

  bool ChildSelectedUnder(size_t group) const {
    return selected_ != kNone && items_[selected_].parent == group;
  }

  // Visibility, selection and corner radii, derived from the expansion state.
  void ApplyLayout() {
    std::vector<std::vector<size_t>> visible(lists_);
    for (size_t i = 0; i < items_.size(); ++i) {
      Item& item = items_[i];
      const bool shown = item.parent == kNone || items_[item.parent].expanded;
      item.row->SetVisible(shown);
      item.row->SetExpanded(item.expanded);
      // A folded group stands in for its selected subpage.
      item.row->SetSelected(selected_ == i ||
                            (item.is_group && !item.expanded &&
                             ChildSelectedUnder(i)));
      if (shown) {
        visible[item.list].push_back(i);
      }
    }
    for (const std::vector<size_t>& rows : visible) {
      for (size_t k = 0; k < rows.size(); ++k) {
        items_[rows[k]].row->SetCorners(
            k == 0 ? kOuterRadius : kInnerRadius,
            k + 1 == rows.size() ? kOuterRadius : kInnerRadius);
      }
    }
    InvalidateLayout();
  }

  NavigateCallback navigate_;
  std::vector<Item> items_;
  size_t lists_ = 0;
  size_t selected_ = kNone;
};

BEGIN_METADATA(SettingsRail)
END_METADATA

GURL SectionURL(ZephyrusSettingsPopup::Section section) {
  using Section = ZephyrusSettingsPopup::Section;
  switch (section) {
    case Section::kSettings:
      return GURL("chrome://settings");
    case Section::kShield:
      return GURL("chrome://settings/adBlocker");
    case Section::kExtensions:
      return GURL("chrome://extensions");
    case Section::kDownloads:
      return GURL("chrome://downloads");
    case Section::kHistory:
      return GURL("chrome://history");
    case Section::kBookmarks:
      return GURL("chrome://bookmarks");
    case Section::kPasswords:
      return GURL("chrome://password-manager");
    case Section::kAbout:
      return GURL("chrome://settings/help");
  }
}

// The close button: the standard Material close glyph (smooth at any scale --
// the old one was a dotted pixel-art X) in a circle, with three motions.
//   Hover: the X turns a quarter turn on M3's fast-spatial spring (it overshoots
//          a hair and settles; a quarter turn of an X looks the same as where
//          it began, so it reads as a spin, not a flip), with the state layer.
//   Press: it shrinks to 82% while the button is held.
//   Click: it spins another quarter turn while shrinking away, ~160ms, and then
//          closes the popup. The close is posted, never run from the animation
//          callback, because closing destroys this button and its animations.
// With reduced motion there is none of it and the click closes at once.
class CloseButton : public views::Button {
  METADATA_HEADER(CloseButton, views::Button)

 public:
  explicit CloseButton(base::RepeatingClosure on_close)
      : views::Button(
            base::BindRepeating(&CloseButton::OnClicked,
                                base::Unretained(this))),
        on_close_(std::move(on_close)),
        hover_(&animation_delegate_),
        press_(&animation_delegate_),
        click_(&animation_delegate_) {
    hover_.SetTweenType(gfx::Tween::LINEAR);
    hover_.SetSlideDuration(base::Milliseconds(260));
    press_.SetTweenType(gfx::Tween::LINEAR);
    press_.SetSlideDuration(base::Milliseconds(100));
    click_.SetTweenType(gfx::Tween::LINEAR);
    click_.SetSlideDuration(base::Milliseconds(160));
    SetFocusBehavior(FocusBehavior::ALWAYS);
    views::FocusRing::Install(this);
  }
  CloseButton(const CloseButton&) = delete;
  CloseButton& operator=(const CloseButton&) = delete;
  ~CloseButton() override = default;

  void SetIconColor(SkColor color) {
    if (color_ == color) {
      return;
    }
    color_ = color;
    SchedulePaint();
  }

  // views::Button:
  void StateChanged(views::Button::ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    if (closing_) {
      return;
    }
    const views::Button::ButtonState state = GetState();
    Retarget(hover_, state == STATE_HOVERED || state == STATE_PRESSED);
    Retarget(press_, state == STATE_PRESSED);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF bounds(GetLocalBounds());
    const float hover = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastSpatial)
                            .Solve(hover_.GetCurrentValue());
    const float press = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastEffects)
                            .Solve(press_.GetCurrentValue());
    const float click = zephyrus::m3::Curve(zephyrus::m3::Spring::kFastEffects)
                            .Solve(click_.GetCurrentValue());
    const float degrees = 90.0f * hover + 90.0f * click;
    const float scale = (1.0f - 0.18f * press) * (1.0f - 0.9f * click);
    // Turned about the centre. The glyph is a path, so it stays sharp under the
    // rotation and the scale.
    canvas->Save();
    cc::PaintCanvas* const paint = canvas->sk_canvas();
    paint->translate(bounds.width() / 2.0f, bounds.height() / 2.0f);
    paint->rotate(degrees);
    paint->scale(scale, scale);
    paint->translate(-kCloseIconSize / 2.0f, -kCloseIconSize / 2.0f);
    gfx::PaintVectorIcon(canvas, vector_icons::kCloseIcon, kCloseIconSize,
                         color_);
    canvas->Restore();
  }

 private:
  // views::Button is itself a gfx::AnimationDelegate (for its own hover
  // animation), so ours cannot be the button: it is this.
  class AnimationForwarder final : public gfx::AnimationDelegate {
   public:
    explicit AnimationForwarder(CloseButton* owner) : owner_(owner) {}
    void AnimationProgressed(const gfx::Animation*) override {
      owner_->SchedulePaint();
    }
    void AnimationEnded(const gfx::Animation* animation) override {
      owner_->OnAnimationEnded(animation);
    }

   private:
    raw_ptr<CloseButton> owner_;
  };

  void OnAnimationEnded(const gfx::Animation* animation) {
    SchedulePaint();
    if (animation != &click_) {
      return;
    }
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&CloseButton::RunClose,
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
    if (closing_) {
      return;
    }
    closing_ = true;
    if (!gfx::Animation::ShouldRenderRichAnimation()) {
      on_close_.Run();
      return;
    }
    click_.Show();
  }

  void RunClose() { on_close_.Run(); }

  base::RepeatingClosure on_close_;
  AnimationForwarder animation_delegate_{this};
  gfx::SlideAnimation hover_;
  gfx::SlideAnimation press_;
  gfx::SlideAnimation click_;
  SkColor color_ = SK_ColorGRAY;
  bool closing_ = false;
  base::WeakPtrFactory<CloseButton> weak_factory_{this};
};

BEGIN_METADATA(CloseButton)
END_METADATA

// The popup's contents. It exists to apply the theme to the parts that do
// not colour themselves: this whole tree is built inside the dialog's
// constructor, before any Widget or ColorProvider exists, so nothing built
// there may ask for a role. OnThemeChanged runs once the popup is in its
// Widget and before the first paint, and again on every theme change.
class SettingsPopupContents : public views::View {
  METADATA_HEADER(SettingsPopupContents, views::View)

 public:
  SettingsPopupContents() = default;
  SettingsPopupContents(const SettingsPopupContents&) = delete;
  SettingsPopupContents& operator=(const SettingsPopupContents&) = delete;
  ~SettingsPopupContents() override = default;

  void set_rail(views::View* rail) { rail_ = rail; }
  void set_close_button(CloseButton* button) { close_button_ = button; }
  void set_version(views::Label* version) { version_ = version; }
  void set_web_view(views::WebView* web_view) { web_view_ = web_view; }

  // views::View:
  void OnThemeChanged() override {
    views::View::OnThemeChanged();
    if (rail_) {
      rail_->SetBackground(views::CreateSolidBackground(RailColor(*this)));
    }
    const SkColor variant =
        zephyrus::m3::Role(*this, kColorZephyrusOnSurfaceVariant);
    if (close_button_) {
      close_button_->SetIconColor(variant);
      // An M3 icon button shows hover and press as a state layer in the
      // content colour, not by recolouring the icon.
      views::InkDrop::Get(close_button_)->SetBaseColor(variant);
    }
    if (version_) {
      version_->SetEnabledColor(variant);
    }
    if (web_view_) {
      const SkColor base = ContentBase(*this);
      web_view_->SetBackground(views::CreateSolidBackground(base));
      if (content::WebContents* contents = web_view_->GetWebContents()) {
        contents->SetPageBaseBackgroundColor(base);
      }
    }
  }

 private:
  raw_ptr<views::View> rail_ = nullptr;
  raw_ptr<CloseButton> close_button_ = nullptr;
  raw_ptr<views::Label> version_ = nullptr;
  raw_ptr<views::WebView> web_view_ = nullptr;
};

BEGIN_METADATA(SettingsPopupContents)
END_METADATA

// The pre-warm's window. It is built like the popup -- the same dialog frame
// (translucent, compositor-rounded, with its shadow), the same entrance
// animation, a rail-like tile beside a rounded page area -- so the GPU compiles
// the same pipelines, but it is shown inert: never activated, ignoring mouse
// input (WS_EX_TRANSPARENT), not modal, at ~0.4% opacity, for under a second.
// It deletes itself when its widget closes.
class PrewarmDialog : public views::DialogDelegate {
 public:
  PrewarmDialog(const PrewarmDialog&) = delete;
  PrewarmDialog& operator=(const PrewarmDialog&) = delete;
  ~PrewarmDialog() override = default;

  // Creates and shows one over |host|. Returns false, having shown nothing,
  // when it cannot be made invisible.
  static bool Start(Browser* browser, views::Widget* host) {
    const gfx::Rect host_bounds = host->GetWindowBoundsInScreen();
    gfx::Size size = kPrewarmSize;
    size.SetToMin(host_bounds.size());
    // Owned by its widget from here: WindowClosing() deletes it.
    PrewarmDialog* dialog = new PrewarmDialog(browser->GetProfile(), size);

    views::Widget::InitParams params =
        views::DialogDelegate::GetDialogWidgetInitParams(
            dialog, gfx::NativeWindow(), host->GetNativeView(), gfx::Rect());
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.accept_events = false;
    auto* widget = new views::Widget;
    widget->Init(std::move(params));
    dialog->widget_ = widget;

    // Without a compositor layer there is no way to make the window
    // invisible, and without a bubble frame no way to make it click-through,
    // so in either case show nothing at all. (accept_events=false only sets
    // WS_EX_TRANSPARENT, which does not pass clicks through a window that is
    // not layered -- measured: a click at its centre landed on it.)
    // HTTRANSPARENT from the frame's hit test is what passes them through.
    ui::Layer* layer = widget->GetLayer();
    views::NonClientView* const non_client = widget->non_client_view();
    auto* frame = non_client ? views::AsViewClass<views::BubbleFrameView>(
                                   non_client->frame_view())
                             : nullptr;
    if (!layer || !frame) {
      widget->CloseNow();
      return false;
    }
    frame->set_hit_test_transparent(true);
    // Hidden before the first frame can be produced, and centred before it is
    // shown, so no frame is ever drawn visible or in the wrong place.
    layer->SetOpacity(0.0f);
    gfx::Rect bounds = widget->GetWindowBoundsInScreen();
    bounds.set_origin(gfx::Point(
        host_bounds.x() + (host_bounds.width() - bounds.width()) / 2,
        host_bounds.y() + (host_bounds.height() - bounds.height()) / 2));
    widget->SetBounds(bounds);
    widget->ShowInactive();
    // ShowInactive() can recreate the layer; use whatever is current.
    layer = widget->GetLayer();
    if (!layer) {
      widget->CloseNow();
      return false;
    }
    layer->SetOpacity(0.0f);
    if (gfx::Animation::ShouldRenderRichAnimation()) {
      StartEntranceAnimation(layer, kPrewarmOpacity);
    } else {
      layer->SetOpacity(kPrewarmOpacity);
    }
    dialog->close_timer_.Start(
        FROM_HERE, kPrewarmLifetime,
        base::BindOnce(&PrewarmDialog::Finish, base::Unretained(dialog)));
    return true;
  }

  // views::DialogDelegate:
  void WindowClosing() override {
    views::DialogDelegate::WindowClosing();
    close_timer_.Stop();
    widget_ = nullptr;
    // Not owned by the widget (see ZephyrusSettingsPopup::WindowClosing), so it
    // deletes itself, deferred so the widget teardown on the stack never
    // touches a freed object.
    if (!delete_scheduled_) {
      delete_scheduled_ = true;
      base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                    this);
    }
  }

 private:
  PrewarmDialog(Profile* profile, const gfx::Size& size) {
    SetModalType(ui::mojom::ModalType::kNone);
    SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
    SetShowTitle(false);
    SetShowCloseButton(false);
    set_use_round_corners(true);
    set_corner_radius(kPopupCornerRadius);
    set_margins(gfx::Insets());

    auto root = std::make_unique<views::View>();
    root->SetPreferredSize(size);
    auto* layout = root->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal));
    // The real rail's content, so the GPU also sees what only the rail draws:
    // rows with four different corner radii (a path, not a simple rounded
    // rectangle), the vector icons, the state-layer ink drop, the overlay
    // scroll bar. Built as BuildContentsView builds it, with callbacks that do
    // nothing; keep the two in step.
    auto* rail = root->AddChildView(std::make_unique<views::View>());
    rail->SetPreferredSize(gfx::Size(kRailWidth, size.height()));
    rail->SetBackground(
        views::CreateSolidBackground(SkColorSetRGB(0x30, 0x30, 0x30)));
    auto* rail_layout = rail->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(kRailInset),
            0));
    rail_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    auto* header = rail->AddChildView(std::make_unique<views::View>());
    header->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, 0, 4, 0), 0));
    auto* close_button = header->AddChildView(
        std::make_unique<CloseButton>(base::DoNothing()));
    // Never shown or reached, but a focusable view with no name fails the
    // accessibility paint check.
    close_button->GetViewAccessibility().SetName(u"Close settings");
    close_button->SetPreferredSize(
        gfx::Size(kCloseButtonSize, kCloseButtonSize));
    views::InstallCircleHighlightPathGenerator(close_button);
    views::InkDrop::Get(close_button)
        ->SetMode(views::InkDropHost::InkDropMode::ON);
    auto* scroll = rail->AddChildView(std::make_unique<views::ScrollView>());
    scroll->ClipHeightTo(0, kUnboundedScrollHeight);
    scroll->SetBackgroundColor(std::nullopt);
    scroll->SetDrawOverflowIndicator(false);
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll->SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(
        views::ScrollBar::Orientation::kVertical));
    scroll->SetContents(std::make_unique<SettingsRail>(
        base::BindRepeating([](const GURL&) {})));
    rail_layout->SetFlexForView(scroll, 1);
    views::View* page = nullptr;
    if (kPrewarmWebView.Get()) {
      auto* web_view =
          root->AddChildView(std::make_unique<views::WebView>(profile));
      web_view->holder()->SetNativeViewCornerRadii(
          gfx::RoundedCornersF(0, kPopupCornerRadius, kPopupCornerRadius, 0));
      web_view->LoadInitialURL(GURL("about:blank"));
      page = web_view;
    } else {
      page = root->AddChildView(std::make_unique<views::View>());
      page->SetBackground(
          views::CreateSolidBackground(SkColorSetRGB(0x20, 0x20, 0x20)));
      page->SetPaintToLayer();
      page->layer()->SetFillsBoundsOpaquely(true);
      page->layer()->SetRoundedCornerRadius(
          gfx::RoundedCornersF(0, kPopupCornerRadius, kPopupCornerRadius, 0));
    }
    layout->SetFlexForView(page, 1);
    SetContentsView(std::move(root));
  }

  void Finish() {
    if (widget_ && !widget_->IsClosed()) {
      widget_->Close();
    }
  }

  raw_ptr<views::Widget> widget_ = nullptr;
  base::OneShotTimer close_timer_;
  bool delete_scheduled_ = false;
};

// Runs kPrewarmDelayMs after a window's first activation. Everything is
// re-checked here, because seconds have passed.
void RunPrewarm(base::WeakPtr<BrowserView> view) {
  g_prewarm_scheduled = false;
  if (!view || g_prewarm_done) {
    return;
  }
  BrowserWindowInterface* const window = view->browser();
  views::Widget* const host = view->GetWidget();
  if (!window || !host || !zephyrus::IsTypeNormal(window)) {
    return;
  }
  Browser* const browser = zephyrus::AsBrowser(window);
  if (browser->GetProfile()->IsOffTheRecord()) {
    return;
  }
  // Only over the window the user is looking at, and never alongside a real
  // popup. If the window is not the foreground one, the next activation tries
  // again.
  if (g_active_popup || !host->IsActive() || !host->IsVisible() ||
      host->IsMinimized() || host->IsFullscreen()) {
    return;
  }
  g_prewarm_done = true;
  PrewarmDialog::Start(browser, host);
}

}  // namespace

// static
bool ZephyrusSettingsPopup::MaybeShow(Browser* browser, Section section) {
  if (!browser || !zephyrus::IsTypeNormal(browser) ||
      browser->GetProfile()->IsOffTheRecord()) {
    return false;
  }
  Show(browser, section);
  return true;
}

// static
void ZephyrusSettingsPopup::SchedulePrewarm(base::WeakPtr<BrowserView> view) {
  if (g_prewarm_scheduled || g_prewarm_done ||
      !base::FeatureList::IsEnabled(kSettingsPopupPrewarm)) {
    return;
  }
  g_prewarm_scheduled = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&RunPrewarm, std::move(view)),
      base::Milliseconds(kPrewarmDelayMs.Get()));
}

// static
void ZephyrusSettingsPopup::Show(Browser* browser, Section section) {
  // Reuse the open popup only when its widget is demonstrably alive; a
  // closing/closed widget means the pointer is about to go stale.
  if (g_active_popup && g_active_popup->browser_ == browser &&
      g_active_popup->widget_ && !g_active_popup->widget_->IsClosed()) {
    g_active_popup->SwitchTo(section);
    g_active_popup->widget_->Activate();
    if (g_active_popup->web_view_) {
      g_active_popup->web_view_->RequestFocus();
    }
    return;
  }
  if (g_active_popup) {
    // Stale, closing, or belongs to another window: shut it down and start
    // fresh. Its WindowClosing() only clears g_active_popup when it still
    // points at itself, so overwriting it below is safe.
    g_active_popup->ClosePopup();
    g_active_popup = nullptr;
  }

  auto popup = base::WrapUnique(new ZephyrusSettingsPopup(browser, section));
  ZephyrusSettingsPopup* popup_ptr = popup.get();
  views::WebView* web_view = popup->web_view_;
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(popup), browser->GetWindow()->GetNativeWindow());
  popup_ptr->widget_ = widget;
  // No DWM corner preference here any more: the compositor rounds the frame at
  // the designed 20px again (see set_use_round_corners in the constructor), and
  // asking DWM to also round the window at its ~8px system radius would clip the
  // corners twice and shave them.
  widget->Show();
  // Center over the browser window; the default browser-modal placement pins
  // the dialog to the top edge, which looks misaligned in the Zephyrus
  // frameless UI.
  const gfx::Rect browser_bounds = browser->GetWindow()->GetBounds();
  gfx::Rect bounds = widget->GetWindowBoundsInScreen();
  bounds.set_origin(gfx::Point(
      browser_bounds.x() + (browser_bounds.width() - bounds.width()) / 2,
      browser_bounds.y() + (browser_bounds.height() - bounds.height()) / 2));
  widget->SetBounds(bounds);
  // Springy entrance (see StartEntranceAnimation), skipped when the OS asks for
  // reduced motion.
  if (ui::Layer* layer = widget->GetLayer();
      layer && gfx::Animation::ShouldRenderRichAnimation()) {
    StartEntranceAnimation(layer, 1.0f);
  }
  if (web_view) {
    // Re-apply the rounded clip now that the web contents' native view is
    // attached to the widget: NativeViewHostAura::ApplyRoundedCorners() is a
    // silent no-op without an attached native view, and the layer can be
    // recreated during widget initialization, dropping radii applied earlier.
    web_view->holder()->SetNativeViewCornerRadii(
        gfx::RoundedCornersF(0, kPopupCornerRadius, kPopupCornerRadius, 0));
    web_view->RequestFocus();
  }
}

ZephyrusSettingsPopup::ZephyrusSettingsPopup(Browser* browser,
                                             Section initial_section)
    : browser_(browser), current_(initial_section) {
  g_active_popup = this;

  SetModalType(ui::mojom::ModalType::kWindow);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetTitle(u"Zephyrus Settings");
  SetShowTitle(false);
  SetShowCloseButton(false);
  // Translucent frameless window with compositor-rounded corners — the design's
  // intent, restored 2026-08-10.
  //
  // This was forced opaque (set_opaque_custom_frame(true) +
  // set_use_round_corners(false) + a DWM corner preference on the HWND) back
  // when DirectComposition was disabled: a translucent window could not deliver
  // per-pixel alpha, so its rounded-corner cutouts rendered as an opaque black
  // border, and the usual software-compositing escape hatch blanks a WebView.
  // DComp is on again — the disable was a workaround for a vsync bug that lived
  // somewhere else entirely — so none of that applies. The cost of the
  // workaround was the designed 20px radius (DWM rounds at ~8) and the soft
  // compositor shadow.
  set_use_round_corners(true);
  set_corner_radius(kPopupCornerRadius);
  set_margins(gfx::Insets());

  // Size to the browser window, capped so the popup keeps a floating,
  // palette-like feel.
  gfx::Size size(980, 680);
  if (browser_->GetWindow()) {
    const gfx::Rect bounds = browser_->GetWindow()->GetBounds();
    // Room to breathe: the rail takes a fixed slice, so the page needs the
    // rest. Capped so the popup still floats over the window.
    size = gfx::Size(std::min(1200, bounds.width() - 80),
                     std::min(800, bounds.height() - 80));
    size.SetToMax(gfx::Size(760, 480));
  }
  SetContentsView(BuildContentsView(size, initial_section));
}

ZephyrusSettingsPopup::~ZephyrusSettingsPopup() {
  if (g_active_popup == this) {
    g_active_popup = nullptr;
  }
}

std::unique_ptr<views::View> ZephyrusSettingsPopup::BuildContentsView(
    const gfx::Size& size,
    Section initial_section) {
  auto root = std::make_unique<SettingsPopupContents>();
  SettingsPopupContents* const contents = root.get();
  root->SetPreferredSize(size);
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  auto* root_layout =
      static_cast<views::BoxLayout*>(root->GetLayoutManager());

  // ---- Left: M3 navigation drawer items -----------------------------------
  // No hairline against the page: the rail's tone does that now. Colours are
  // applied by SettingsPopupContents::OnThemeChanged.
  auto* rail = root->AddChildView(std::make_unique<views::View>());
  contents->set_rail(rail);
  rail->SetPreferredSize(gfx::Size(kRailWidth, size.height()));
  auto* rail_layout = rail->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kRailInset), 0));
  rail_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Close, a 32dp icon button at the top of the rail (see CloseButton). The band
  // it sits in is 12 + 32 + 4dp, down from 12 + 40 + 8.
  auto* header = rail->AddChildView(std::make_unique<views::View>());
  header->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, 0, 4, 0), 0));
  auto* close_button = header->AddChildView(std::make_unique<CloseButton>(
      base::BindRepeating(&ZephyrusSettingsPopup::ClosePopup,
                          base::Unretained(this))));
  contents->set_close_button(close_button);
  close_button->SetPreferredSize(gfx::Size(kCloseButtonSize, kCloseButtonSize));
  close_button->GetViewAccessibility().SetName(u"Close settings");
  close_button->SetTooltipText(u"Close");
  views::InstallCircleHighlightPathGenerator(close_button);
  views::InkDrop::Get(close_button)
      ->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::Get(close_button)
      ->SetHighlightOpacity(zephyrus::m3::kHover / 255.0f);
  views::InkDrop::Get(close_button)
      ->SetVisibleOpacity(zephyrus::m3::kPressed / 255.0f);

  // The items SCROLL. At M3's 56dp the eight of them, the dividers and the
  // header come to roughly 584dp, and the popup is allowed down to 480 on a
  // small window — without this the last items and the version would simply
  // be cut off. Set up exactly as the sidebar's tab list, which learned both
  // halves the hard way: ClipHeightTo is required, and the scrollbar must be
  // an overlay.
  auto* scroll = rail->AddChildView(std::make_unique<views::ScrollView>());
  scroll->ClipHeightTo(0, kUnboundedScrollHeight);
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetDrawOverflowIndicator(false);
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetVerticalScrollBar(std::make_unique<views::OverlayScrollBar>(
      views::ScrollBar::Orientation::kVertical));
  auto* rail_list = scroll->SetContents(std::make_unique<SettingsRail>(
      base::BindRepeating(&ZephyrusSettingsPopup::SwitchToURL,
                          base::Unretained(this))));
  rail_list_ = rail_list;
  rail_layout->SetFlexForView(scroll, 1);

  // Zephyrus version at the foot of the rail, below the scrolling items.
  auto* version = rail->AddChildView(std::make_unique<views::Label>(
      u"Zephyrus " + std::u16string(zephyrus::kVersion)));
  contents->set_version(version);
  version->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  version->SetAutoColorReadabilityEnabled(false);
  version->SetSubpixelRenderingEnabled(false);
  version->SetFontList(zephyrus::m3::Font(zephyrus::m3::Type::kLabelSmall));
  version->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::TLBR(8, kRowPaddingH, 2, 0)));

  // ---- Right: embedded WebUI ----------------------------------------------
  auto* web_view =
      root->AddChildView(std::make_unique<views::WebView>(browser_->GetProfile()));
  web_view_ = web_view;
  root_layout->SetFlexForView(web_view, 1);
  // Clip the native web contents to the popup's rounded right corners.
  web_view->holder()->SetNativeViewCornerRadii(
      gfx::RoundedCornersF(0, kPopupCornerRadius, kPopupCornerRadius, 0));
  contents->set_web_view(web_view);
  // A base under the page, so opening the popup and switching sections never
  // flashes white before the WebUI paints. This tree has no ColorProvider yet,
  // so the first value is read from the BROWSER WINDOW, which does; the
  // contents view re-applies it in OnThemeChanged.
  BrowserView* const browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  // Register the embedding context BEFORE the first navigation: hosted WebUIs
  // (history clusters, settings subpages, ...) resolve their browser through
  // webui::GetBrowserWindowInterface() since they are not in a tab here.
  content::WebContents* web_contents = web_view->GetWebContents();
  webui::SetBrowserWindowInterface(web_contents, browser_);
  // The Autofill settings page talks to autofillPrivate, whose functions take
  // the sender's ContentAutofillClient for granted because every other host of
  // that page is a tab. This WebContents is not one, so there was none, and
  // opening Autofill and passwords dereferenced null in the browser process
  // (AutofillPrivateLoadEntityInstancesFunction::Run) and killed the whole
  // browser. Give it the client a tab would get.
  autofill::AutofillClientProviderFactory::GetForProfile(browser_->GetProfile())
      .CreateClientForWebContents(web_contents);
  if (browser_view) {
    web_view->SetBackground(
        views::CreateSolidBackground(ContentBase(*browser_view)));
    web_contents->SetPageBaseBackgroundColor(ContentBase(*browser_view));
  }
  web_contents->SetDelegate(this);
  // Observe navigations so the rail highlight follows pages the popup did not
  // navigate to itself (e.g. history's "Delete browsing data" subpage).
  Observe(web_contents);
  web_view->LoadInitialURL(SectionURL(initial_section));

  views::AsViewClass<SettingsRail>(rail_list_.get())
      ->SelectURL(SectionURL(initial_section));
  return root;
}

void ZephyrusSettingsPopup::SwitchTo(Section section) {
  current_ = section;
  SwitchToURL(SectionURL(section));
}

void ZephyrusSettingsPopup::SwitchToURL(const GURL& url) {
  if (!web_view_ || !web_view_->GetWebContents()) {
    return;
  }
  content::WebContents* web_contents = web_view_->GetWebContents();
  if (std::optional<Section> section = SectionForURL(url)) {
    current_ = *section;
  }
  if (rail_list_) {
    views::AsViewClass<SettingsRail>(rail_list_.get())->SelectURL(url);
  }
  // Re-selecting the page that is already showing would reload it and throw
  // away its state (scroll position, open subsections, search text).
  if (web_contents->GetLastCommittedURL() == url) {
    web_view_->RequestFocus();
    return;
  }
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  web_contents->GetController().LoadURLWithParams(params);
  web_view_->RequestFocus();
}

// static
std::optional<ZephyrusSettingsPopup::Section>
ZephyrusSettingsPopup::SectionForURL(const GURL& url) {
  if (!url.SchemeIs("chrome")) {
    return std::nullopt;
  }
  const std::string_view host = url.host();
  const std::string_view path = url.path();
  if (host == "settings") {
    // More specific settings subpages first.
    if (path == "/adBlocker") {
      return Section::kShield;
    }
    if (path == "/help") {
      return Section::kAbout;
    }
    // Everything else under settings (incl. clearBrowserData) is Settings.
    return Section::kSettings;
  }
  if (host == "extensions") {
    return Section::kExtensions;
  }
  if (host == "downloads") {
    return Section::kDownloads;
  }
  if (host == "history") {
    return Section::kHistory;
  }
  if (host == "bookmarks") {
    return Section::kBookmarks;
  }
  if (host == "password-manager") {
    return Section::kPasswords;
  }
  return std::nullopt;
}

void ZephyrusSettingsPopup::PrimaryPageChanged(content::Page& page) {
  // Keep the rail selection in sync with whatever page the webview is showing,
  // including navigations the popup didn't initiate via SwitchTo().
  const GURL& url = page.GetMainDocument().GetLastCommittedURL();
  if (std::optional<Section> section = SectionForURL(url)) {
    current_ = *section;
  }
  if (rail_list_) {
    views::AsViewClass<SettingsRail>(rail_list_.get())->SelectURL(url);
  }
}

void ZephyrusSettingsPopup::ClosePopup() {
  if (widget_ && !widget_->IsClosed()) {
    widget_->Close();
  }
}

void ZephyrusSettingsPopup::WindowClosing() {
  views::DialogDelegate::WindowClosing();
  // The widget is going away: sever every pointer that outside code (the
  // singleton in Show()) or the hosted WebContents could still reach us
  // through, while the view tree is still alive.
  if (g_active_popup == this) {
    g_active_popup = nullptr;
  }
  if (web_view_ && web_view_->GetWebContents()) {
    web_view_->GetWebContents()->SetDelegate(nullptr);
  }
  Observe(nullptr);
  web_view_ = nullptr;
  widget_ = nullptr;
  rail_list_ = nullptr;
  // The widget does not own this delegate (WidgetDelegate defaults to
  // not-owned and SetOwnedByWidget is pass-key gated), so it must delete
  // itself. Deferred so the widget teardown currently on the stack never
  // touches a freed object.
  if (!delete_scheduled_) {
    delete_scheduled_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                  this);
  }
}

content::WebContents* ZephyrusSettingsPopup::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  if (params.disposition == WindowOpenDisposition::CURRENT_TAB) {
    // Navigation within the popup (links inside the hosted WebUI page).
    content::NavigationController::LoadURLParams load_params(params);
    base::WeakPtr<content::NavigationHandle> handle =
        source->GetController().LoadURLWithParams(load_params);
    if (navigation_handle_callback && handle) {
      std::move(navigation_handle_callback).Run(*handle);
    }
    return source;
  }
  // Links that target a new tab open in the real browser; the modal popup
  // closes so the user actually sees the page.
  NavigateParams nav(browser_, params.url, params.transition);
  nav.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&nav);
  ClosePopup();
  return nullptr;
}

content::WebContents* ZephyrusSettingsPopup::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  // window.open()/target=_blank from the hosted page: hand the new contents
  // to the browser, preserving the requested disposition (sign-in flows from
  // settings open NEW_POPUP windows and expect a real popup), then close the
  // modal so the user can interact with what opened.
  if (disposition == WindowOpenDisposition::CURRENT_TAB ||
      disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB) {
    disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  }
  content::WebContents* added =
      chrome::AddWebContents(browser_, nullptr, std::move(new_contents),
                             target_url, disposition, window_features);
  ClosePopup();
  return added;
}

content::KeyboardEventProcessingResult
ZephyrusSettingsPopup::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() != blink::WebInputEvent::Type::kRawKeyDown) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  // Esc closes the popup even while the web contents has focus.
  if (event.windows_key_code == ui::VKEY_ESCAPE) {
    ClosePopup();
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  // The popup is its own widget, so browser accelerators don't reach it while
  // the hosted page has focus. Handle the section shortcuts locally so
  // Ctrl+H / Ctrl+J / Ctrl+Shift+O keep working inside the popup.
  const int modifiers =
      event.GetModifiers() & blink::WebInputEvent::kKeyModifiers;
  if (modifiers == blink::WebInputEvent::kControlKey) {
    if (event.windows_key_code == ui::VKEY_H) {
      SwitchTo(Section::kHistory);
      return content::KeyboardEventProcessingResult::HANDLED;
    }
    if (event.windows_key_code == ui::VKEY_J) {
      SwitchTo(Section::kDownloads);
      return content::KeyboardEventProcessingResult::HANDLED;
    }
  } else if (modifiers == (blink::WebInputEvent::kControlKey |
                           blink::WebInputEvent::kShiftKey) &&
             event.windows_key_code == ui::VKEY_O) {
    SwitchTo(Section::kBookmarks);
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

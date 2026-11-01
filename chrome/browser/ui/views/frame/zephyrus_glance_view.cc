// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/zephyrus_glance_view.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/color/zephyrus_color_mixer.h"
#include "chrome/browser/ui/tab_helpers.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/zephyrus_m3.h"
#include "chrome/browser/ui/views/frame/zephyrus_workspace_partition.h"
#include "chrome/browser/zephyrus/glance/glance_policy.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/split_tabs/split_tab_visual_data.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/vector_icons.h"
#include "ui/views/widget/widget.h"

namespace {

namespace glance = zephyrus::glance;

// A multiple of 4 so the layer's rounded corners land on whole device pixels.
constexpr float kCardRadius = 20;
constexpr int kElevation = 6;
constexpr int kButtonSize = 40;
constexpr int kButtonGap = 12;
constexpr int kButtonSlide = 16;
constexpr int kButtonIcon = 20;

base::TimeDelta RichDuration(base::TimeDelta duration) {
  return gfx::Animation::ShouldRenderRichAnimation() ? duration
                                                     : base::TimeDelta();
}

// One of the three round buttons beside the card.
class GlanceButton : public views::ImageButton {
  METADATA_HEADER(GlanceButton, views::ImageButton)

 public:
  GlanceButton(PressedCallback callback,
               const gfx::VectorIcon& icon,
               const std::u16string& name)
      : views::ImageButton(std::move(callback)), icon_(icon) {
    SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    GetViewAccessibility().SetName(name);
    SetTooltipText(name);
  }

  void OnThemeChanged() override {
    views::ImageButton::OnThemeChanged();
    SetImageModel(views::Button::STATE_NORMAL,
                  ui::ImageModel::FromVectorIcon(
                      *icon_,
                      zephyrus::m3::Role(*this, kColorZephyrusOnSurface),
                      kButtonIcon));
    SchedulePaint();
  }

  void StateChanged(views::Button::ButtonState old_state) override {
    views::ImageButton::StateChanged(old_state);
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    const SkColor container =
        zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHigh);
    const SkColor ink = zephyrus::m3::Role(*this, kColorZephyrusOnSurface);
    SkColor fill = container;
    if (GetState() == views::Button::STATE_PRESSED) {
      fill = zephyrus::m3::WithStateLayer(container, ink,
                                          zephyrus::m3::kPressed);
    } else if (GetState() == views::Button::STATE_HOVERED) {
      fill = zephyrus::m3::WithStateLayer(container, ink, zephyrus::m3::kHover);
    }
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(fill);
    flags.setLooper(gfx::CreateShadowDrawLooper(
        gfx::ShadowValue::MakeMdShadowValues(2)));
    gfx::RectF bounds(GetLocalBounds());
    bounds.Inset(2);
    canvas->DrawRoundRect(bounds, bounds.height() / 2, flags);
  }

 private:
  const raw_ref<const gfx::VectorIcon> icon_;
};

BEGIN_METADATA(GlanceButton)
END_METADATA

}  // namespace

ZephyrusGlanceView::ZephyrusGlanceView(BrowserView* browser_view)
    : browser_view_(browser_view) {
  // Its own layer, to stack above the page, and not opaque: the scrim and the
  // card are painted over what is behind.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);
  animation_.SetTweenType(gfx::Tween::Type::LINEAR);

  web_view_ = AddChildView(
      std::make_unique<views::WebView>(browser_view->browser()->profile()));
  web_view_->SetPaintToLayer();
  web_view_->layer()->SetFillsBoundsOpaquely(false);
  web_view_->SetVisible(false);

  buttons_ = AddChildView(std::make_unique<views::View>());
  buttons_->SetPaintToLayer();
  buttons_->layer()->SetFillsBoundsOpaquely(false);
  // What each button does, by name: Close, Expand (a tab), Expand + split.
  auto action = [](ZephyrusGlanceView* self, int what, const ui::Event&) {
    if (what == 0) {
      self->Close();
    } else {
      self->Expand(/*split=*/what == 2);
    }
  };
  buttons_->AddChildView(std::make_unique<GlanceButton>(
      base::BindRepeating(action, base::Unretained(this), 0), views::kCloseIcon,
      u"Close"));
  buttons_->AddChildView(std::make_unique<GlanceButton>(
      base::BindRepeating(action, base::Unretained(this), 1),
      kOpenInFullIcon, u"Open as a tab"));
  buttons_->AddChildView(std::make_unique<GlanceButton>(
      base::BindRepeating(action, base::Unretained(this), 2), kSplitSceneIcon,
      u"Open beside this page"));

  // For when focus is not in the page (a click on the dimmed area moved it).
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

ZephyrusGlanceView::~ZephyrusGlanceView() {
  // The page goes before the WebView that shows it.
  if (web_view_) {
    web_view_->SetWebContents(nullptr);
  }
  contents_.reset();
}

void ZephyrusGlanceView::Open(const GURL& url,
                              content::WebContents* parent,
                              const gfx::Point& origin) {
  if (state_ != State::kHidden || !parent || !glance::IsGlanceableUrl(url)) {
    return;
  }
  Profile* profile = Profile::FromBrowserContext(parent->GetBrowserContext());
  if (!profile) {
    return;
  }

  // The parent's cookie jar: a glance that was logged out of everything would
  // be no use for "look at this link". Null means the default partition, which
  // a plain new-tab SiteInstance already lands in.
  scoped_refptr<content::SiteInstance> site_instance =
      zephyrus::SiteInstanceForWorkspace(
          profile, zephyrus::PartitionNameOfContents(parent), url);
  content::WebContents::CreateParams create_params(
      profile, site_instance ? site_instance
                             : tab_util::GetSiteInstanceForNewTab(profile, url));
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(create_params);
  if (!contents) {
    return;
  }
  // The adblocker and the privacy features hang off these, so a glance is
  // protected like any other page.
  TabHelpers::AttachTabHelpers(contents.get());
  contents->SetDelegate(this);
  // No referrer: the user asked to LOOK at a link, not to tell the site which
  // page they were on.
  content::NavigationController::LoadURLParams load(url);
  load.transition_type = ui::PAGE_TRANSITION_LINK;
  contents->GetController().LoadURLWithParams(load);
  contents_ = std::move(contents);

  state_ = State::kOpening;
  page_revealed_ = false;
  SetVisible(true);
  FitToContents();
  origin_rect_ = glance::OriginRect(origin, size());
  card_rect_ = glance::CardBounds(size());
  UpdateButtons();
  animation_.Reset(0);
  animation_.SetSlideDuration(RichDuration(glance::kOpenDuration));
  animation_.Show();
}

void ZephyrusGlanceView::Close() {
  if (state_ != State::kOpening && state_ != State::kOpen) {
    return;
  }
  state_ = State::kClosing;
  // The page leaves first, so the card shrinks back as a card and not as a
  // window onto a page that is no longer sized to it.
  web_view_->SetVisible(false);
  web_view_->SetWebContents(nullptr);
  page_revealed_ = false;
  if (browser_view_->browser()->tab_strip_model()->GetActiveWebContents()) {
    browser_view_->browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  }
  animation_.SetSlideDuration(RichDuration(glance::kCloseDuration));
  animation_.Hide();
}

void ZephyrusGlanceView::CloseImmediately() {
  if (state_ == State::kHidden) {
    return;
  }
  animation_.Reset(0);
  Finish();
}

void ZephyrusGlanceView::Finish() {
  state_ = State::kHidden;
  page_revealed_ = false;
  web_view_->SetVisible(false);
  web_view_->SetWebContents(nullptr);
  SetVisible(false);
  // Deleting the page re-enters nothing of ours: the delegate is this view and
  // it is going nowhere.
  contents_.reset();
}

void ZephyrusGlanceView::Expand(bool split) {
  if (state_ != State::kOpen || !contents_) {
    return;
  }
  TabStripModel* model = browser_view_->browser()->tab_strip_model();
  const int parent_index = model->active_index();
  if (parent_index == TabStripModel::kNoTab) {
    return;
  }

  // Hand the page over: detached from the card, the card gone this instant,
  // THEN into the strip, so the strip's "active tab changed" finds nothing
  // showing to dismiss.
  web_view_->SetVisible(false);
  web_view_->SetWebContents(nullptr);
  std::unique_ptr<content::WebContents> page = std::move(contents_);
  content::WebContents* raw_page = page.get();
  animation_.Reset(0);
  state_ = State::kHidden;
  page_revealed_ = false;
  SetVisible(false);

  model->AddWebContents(std::move(page), parent_index + 1,
                        ui::PAGE_TRANSITION_LINK, AddTabTypes::ADD_ACTIVE);
  if (split) {
    // AddToNewSplit always takes the ACTIVE tab (the page we just added) as one
    // half, so only the parent is named.
    model->AddToNewSplit(
        {parent_index},
        split_tabs::SplitTabVisualData(split_tabs::SplitTabLayout::kSideBySide),
        split_tabs::SplitTabCreatedSource::kLinkClick);
  }
  raw_page->Focus();
}

void ZephyrusGlanceView::FitToContents() {
  views::View* container = browser_view_->contents_container();
  if (!container || !parent()) {
    return;
  }
  gfx::Point origin;
  views::View::ConvertPointToTarget(container, parent(), &origin);
  SetBoundsRect(gfx::Rect(origin, container->size()));
}

void ZephyrusGlanceView::Layout(PassKey) {
  card_rect_ = glance::CardBounds(size());
  web_view_->SetBoundsRect(card_rect_);
  const gfx::Rect column = glance::ButtonColumnBounds(card_rect_, size());
  buttons_->SetBoundsRect(column);
  int y = 0;
  for (views::View* button : buttons_->children()) {
    button->SetBounds((column.width() - kButtonSize) / 2, y, kButtonSize,
                      kButtonSize);
    y += kButtonSize + kButtonGap;
  }
}

void ZephyrusGlanceView::OnPaintBackground(gfx::Canvas* canvas) {
  if (state_ == State::kHidden) {
    return;
  }
  const double eased = glance::Ease(animation_.GetCurrentValue());
  canvas->FillRect(
      GetLocalBounds(),
      SkColorSetA(SK_ColorBLACK,
                  base::ClampRound<uint8_t>(255 * glance::kScrimOpacity *
                                            eased)));

  const gfx::Rect rect = glance::RectAt(eased, origin_rect_, card_rect_);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(zephyrus::m3::Role(*this, kColorZephyrusSurfaceContainerHigh));
  flags.setLooper(gfx::CreateShadowDrawLooper(
      gfx::ShadowValue::MakeMdShadowValues(kElevation)));
  const float radius =
      std::min(kCardRadius, std::min(rect.width(), rect.height()) / 2.f);
  canvas->DrawRoundRect(gfx::RectF(rect), radius, flags);
}

bool ZephyrusGlanceView::OnMousePressed(const ui::MouseEvent& event) {
  // Anything that reaches here is outside the card and the buttons (they are
  // children and took their own clicks): the dimmed page. Closing is the
  // answer, and the press is ours either way, so nothing leaks to the page.
  if (state_ == State::kOpen || state_ == State::kOpening) {
    Close();
  }
  return true;
}

void ZephyrusGlanceView::OnMouseReleased(const ui::MouseEvent& event) {}

bool ZephyrusGlanceView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (state_ != State::kOpen && state_ != State::kOpening) {
    return false;
  }
  Close();
  return true;
}

void ZephyrusGlanceView::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
}

void ZephyrusGlanceView::UpdateButtons() {
  const double progress = glance::ButtonsProgress(
      glance::kOpenDuration * animation_.GetCurrentValue());
  buttons_->SetVisible(progress > 0 && state_ != State::kHidden);
  if (ui::Layer* layer = buttons_->layer()) {
    layer->SetOpacity(static_cast<float>(progress));
    gfx::Transform slide;
    // They arrive from behind the card, sliding out to where they rest.
    slide.Translate(static_cast<float>(-(1 - progress) * kButtonSlide), 0);
    layer->SetTransform(slide);
  }
}

void ZephyrusGlanceView::AnimationProgressed(const gfx::Animation* animation) {
  UpdateButtons();
  SchedulePaint();
}

void ZephyrusGlanceView::AnimationEnded(const gfx::Animation* animation) {
  UpdateButtons();
  SchedulePaint();
  if (state_ == State::kOpening) {
    state_ = State::kOpen;
    RevealPage();
  } else if (state_ == State::kClosing) {
    Finish();
  }
}

void ZephyrusGlanceView::RevealPage() {
  if (!contents_ || page_revealed_) {
    return;
  }
  page_revealed_ = true;
  web_view_->SetVisible(true);
  web_view_->SetWebContents(contents_.get());
  // The same two layers the customize panel rounds, and for the same reason:
  // the native view host's return value says nothing about whether a pixel was
  // rounded, and the renderer's surface is a descendant that clips only if its
  // parent layer does.
  const gfx::RoundedCornersF radii(kCardRadius);
  web_view_->holder()->SetCornerRadii(radii);
  if (ui::Layer* layer = web_view_->layer()) {
    layer->SetRoundedCornerRadius(radii);
    layer->SetIsFastRoundedCorner(true);
  }
  DeprecatedLayoutImmediately();
  contents_->Focus();
}

// ---- content::WebContentsDelegate ----------------------------------------

content::WebContents* ZephyrusGlanceView::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  if (source != contents_.get()) {
    return nullptr;
  }
  if (params.disposition == WindowOpenDisposition::CURRENT_TAB) {
    // The default would drop this, and a glance you cannot click through is
    // not much of a look at a site.
    source->GetController().LoadURLWithParams(
        content::NavigationController::LoadURLParams(params));
    return source;
  }
  // A link that asks for a tab or a window gets one in the browser, not a
  // second layer on top of this one.
  content::WebContents* opened = browser_view_->browser()->OpenURL(
      params, base::OnceCallback<void(content::NavigationHandle&)>());
  if (params.disposition != WindowOpenDisposition::NEW_BACKGROUND_TAB) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ZephyrusGlanceView::Close,
                                  weak_factory_.GetWeakPtr()));
  }
  return opened;
}

content::WebContents* ZephyrusGlanceView::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  // window.open and target=_blank. The browser decides (popup blocker, tab or
  // window) exactly as it would for any page, and the glance steps aside.
  content::WebContents* added =
      static_cast<content::WebContentsDelegate*>(browser_view_->browser())
          ->AddNewContents(source, std::move(new_contents), target_url,
                           disposition, window_features, user_gesture,
                           was_blocked);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusGlanceView::Close,
                                weak_factory_.GetWeakPtr()));
  return added;
}

void ZephyrusGlanceView::CloseContents(content::WebContents* source) {
  // window.close(). Posted: this is called from inside the page.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ZephyrusGlanceView::Close,
                                weak_factory_.GetWeakPtr()));
}

bool ZephyrusGlanceView::HandleContextMenu(
    content::RenderFrameHost& render_frame_host,
    const content::ContextMenuParams& params) {
  // The browser's context menu acts on a tab in a window, and this page is
  // neither yet. Handled means no menu, rather than one whose commands reach
  // for a browser that does not know about this page.
  return true;
}

content::KeyboardEventProcessingResult
ZephyrusGlanceView::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.windows_key_code == ui::VKEY_ESCAPE &&
      event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      (state_ == State::kOpen || state_ == State::kOpening)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&ZephyrusGlanceView::Close,
                                  weak_factory_.GetWeakPtr()));
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  return content::KeyboardEventProcessingResult::NOT_HANDLED;
}

BEGIN_METADATA(ZephyrusGlanceView)
END_METADATA

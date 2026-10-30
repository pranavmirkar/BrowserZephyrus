// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ONBOARDING_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ONBOARDING_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/zephyrus/agent/onboarding_flow.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class BrowserView;

namespace gfx {
class Canvas;
}

namespace views {
class Label;
class MdTextButton;
class ScrollView;
}  // namespace views

namespace zephyrus {

class OnboardingDots;
class OnboardingFlyer;
class OnboardingSky;

// The first-run screens: a full-window, one-step-at-a-time flow (welcome, look,
// workspaces, agent and voice, privacy and import) over a daytime sky with
// clouds drifting across it, until the person finishes or skips it. Native, like
// the rest of the chrome, so the real mascot can react to each step.
//
// Whether it shows is decided once, when the window is built (ShowIfDue): the
// pref says it has not been seen, the feature is switched on, or the dev switch
// forces it. The flow's own rules (which screen, what Back and Next do) are
// zephyrus::agent::OnboardingFlow, pure and tested without a window.
class ZephyrusOnboarding : public views::View {
  METADATA_HEADER(ZephyrusOnboarding, views::View)

 public:
  explicit ZephyrusOnboarding(BrowserView* browser_view);
  ZephyrusOnboarding(const ZephyrusOnboarding&) = delete;
  ZephyrusOnboarding& operator=(const ZephyrusOnboarding&) = delete;
  ~ZephyrusOnboarding() override;

  // Shows the flow from the first screen if it is due. Called once the window
  // has its browser and profile.
  void ShowIfDue();
  // Shows it from the first screen whatever the pref says ("Run setup again").
  void Start();
  bool showing() const { return GetVisible(); }

  // Covers the whole window, above the web contents and below the mascot.
  void FitToParent();

  // views::View:
  void OnThemeChanged() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseWheel(const ui::MouseWheelEvent& event) override;

 private:
  void BuildChrome();
  void ShowStep();
  void AnimateIn();
  void OnNext();
  void OnBack();
  void OnSkip();
  void Finish();
  void OnSkyTick();
  // Fits the screen to the window: on a short window the headline gets smaller
  // and the card's controls scroll, so the footer is always on screen.
  void FitContent();
  // Builds the current screen again after this event is over: a button that
  // rebuilds its own screen from its click would free itself mid-click.
  void RebuildStepSoon();
  // The Workspaces screen's "Not now": on to the next screen without creating.
  void ContinueWithoutWorkspace();

  const raw_ptr<BrowserView> browser_view_;
  zephyrus::agent::OnboardingFlow flow_;

  // The backdrop, and the clock that moves its clouds while it is showing.
  std::unique_ptr<OnboardingSky> sky_;
  // Zep, flying through that sky between the cloud layers.
  std::unique_ptr<OnboardingFlyer> flyer_;
  base::RepeatingTimer sky_timer_;
  base::TimeTicks sky_last_;

  raw_ptr<views::View> column_ = nullptr;
  raw_ptr<OnboardingDots> dots_ = nullptr;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> body_ = nullptr;
  raw_ptr<views::View> step_content_ = nullptr;
  raw_ptr<views::ScrollView> content_scroll_ = nullptr;
  raw_ptr<views::MdTextButton> back_ = nullptr;
  raw_ptr<views::MdTextButton> next_ = nullptr;
  raw_ptr<views::MdTextButton> skip_ = nullptr;
  // The inline workspace editor while the Workspaces screen shows: Next creates
  // what it describes.
  raw_ptr<views::View> workspace_editor_ = nullptr;

  base::WeakPtrFactory<ZephyrusOnboarding> weak_factory_{this};
};

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_ONBOARDING_H_

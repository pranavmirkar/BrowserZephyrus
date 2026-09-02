// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_LAYOUT_BROWSER_VIEW_LAYOUT_DELEGATE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_LAYOUT_BROWSER_VIEW_LAYOUT_DELEGATE_H_

#include "chrome/browser/ui/views/frame/layout/browser_view_layout_params.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/layout/layout_types.h"

class ExclusiveAccessBubbleViews;
class ImmersiveModeController;

namespace gfx {
class Rect;
}

// Delegate class to allow BrowserViewLayout to be decoupled from BrowserView
// for testing.
class BrowserViewLayoutDelegate {
 public:
  virtual ~BrowserViewLayoutDelegate() = default;

  // The state the window is in. We do not lay out when minimized/hidden, so
  // that isn't included here.
  enum class WindowState {
    kNormal,
    kMaximized,
    kFullscreen,
    kFullscreenWithToolbar
  };

  // Zephyrus: width the attached sidebar is currently taking out of the
  // leading edge of the client area, or 0 when it is hidden. Comes through the
  // delegate because the layout is deliberately decoupled from BrowserView —
  // `views().browser_view` is type-erased to views::View* precisely so the
  // layout cannot reach into it.
  //
  // Not pure: every other implementation (tests, mocks) has no sidebar and the
  // default is the correct answer for them.
  // Pure rather than defaulted: chromium-style forbids inline virtual bodies,
  // and BrowserViewLayoutDelegateImpl is the only implementation, so there is
  // nothing else to keep compiling.
  //
  // Target width is the FULL column and stays constant while the sidebar is on
  // screen or animating; the fraction below is what moves. Split this way so
  // the layout can follow the side panel's pattern exactly — reserve
  // target*amount, and treat amount < 1 as "animating".
  virtual int GetZephyrusSidebarTargetWidth() const = 0;
  virtual int GetZephyrusAgentPanelWidth() const = 0;
  virtual double GetZephyrusSidebarRevealAmount() const = 0;

  virtual bool ShouldDrawTabStrip() const = 0;
  virtual bool ShouldDrawVerticalTabStrip() const = 0;
  virtual bool IsVerticalTabStripCollapsed() const = 0;
  virtual bool ShouldDrawWebAppFrameToolbar() const = 0;
  virtual bool GetUnframedModeEnabled() const = 0;
  virtual BrowserLayoutParams GetBrowserLayoutParams(
      bool use_browser_bounds) const = 0;
  virtual WindowState GetBrowserWindowState() const = 0;
  virtual views::LayoutAlignment GetWindowTitleAlignment() const = 0;
  virtual bool IsToolbarVisible() const = 0;
  virtual bool IsBookmarkBarVisible() const = 0;
  virtual bool IsInfobarVisible() const = 0;
  virtual bool IsContentsSeparatorEnabled() const = 0;
  virtual bool IsActiveTabSplit() const = 0;
  virtual bool IsActiveTabAtLeadingWindowEdge() const = 0;
  virtual const ImmersiveModeController* GetImmersiveModeController() const = 0;
  virtual ExclusiveAccessBubbleViews* GetExclusiveAccessBubble() const = 0;
  virtual bool IsTopControlsSlideBehaviorEnabled() const = 0;
  virtual float GetTopControlsSlideBehaviorShownRatio() const = 0;
  virtual gfx::NativeView GetHostViewForAnchoring() const = 0;
  virtual bool HasFindBarController() const = 0;
  virtual void MoveWindowForFindBarIfNecessary() const = 0;
  virtual bool IsWindowControlsOverlayEnabled() const = 0;
  virtual void UpdateWindowControlsOverlay(
      const gfx::Rect& available_titlebar_area) = 0;
  virtual bool ShouldLayoutTabStrip() const = 0;
  virtual int GetExtraInfobarOffset() const = 0;
  virtual bool IsProjectsPanelVisible() const = 0;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_LAYOUT_BROWSER_VIEW_LAYOUT_DELEGATE_H_

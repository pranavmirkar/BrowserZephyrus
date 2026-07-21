// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SETTINGS_POPUP_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SETTINGS_POPUP_H_

#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;
class GURL;

namespace gfx {
struct VectorIcon;
}  // namespace gfx

namespace views {
class LabelButton;
class View;
class WebView;
class Widget;
}  // namespace views

// Zephyrus centralized settings popup: a rounded, Apple-styled browser-modal
// dialog with a pill navigation rail on the left and an embedded WebView on
// the right hosting the browser's own WebUI surfaces (settings, extensions,
// downloads, history, bookmarks, passwords, ...). One popup for everything a
// user can change, instead of Chrome's scattered full-tab pages.
class ZephyrusSettingsPopup : public views::DialogDelegate,
                              public content::WebContentsDelegate,
                              public content::WebContentsObserver {
 public:
  enum class Section {
    kSettings,
    kShield,
    kExtensions,
    kDownloads,
    kHistory,
    kBookmarks,
    kPasswords,
    kAbout,
  };

  ZephyrusSettingsPopup(const ZephyrusSettingsPopup&) = delete;
  ZephyrusSettingsPopup& operator=(const ZephyrusSettingsPopup&) = delete;
  ~ZephyrusSettingsPopup() override;

  // Shows the popup for |browser|, or switches section + refocuses when it is
  // already open. Returns false (without showing) for window types the popup
  // does not support (popups/apps, incognito) so callers can fall back to the
  // regular full-tab pages.
  static bool MaybeShow(Browser* browser, Section section);

  // views::DialogDelegate:
  void WindowClosing() override;

 private:
  ZephyrusSettingsPopup(Browser* browser, Section initial_section);

  static void Show(Browser* browser, Section section);

  std::unique_ptr<views::View> BuildContentsView(const gfx::Size& size,
                                                 Section initial_section);
  void SwitchTo(Section section);
  void UpdatePillStates();
  void ClosePopup();

  // content::WebContentsDelegate:
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;
  content::KeyboardEventProcessingResult PreHandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  // Maps a committed WebUI URL back to the rail section it belongs to, so the
  // rail highlight follows navigations the popup didn't initiate (e.g. the
  // history page's "Delete browsing data" opening the settings subpage).
  static std::optional<Section> SectionForURL(const GURL& url);

  raw_ptr<Browser> browser_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::Widget> widget_ = nullptr;
  // (section, pill button) per nav entry. std::pair keeps the chromium-rawptr
  // plugin happy for the non-owning pointer.
  std::vector<std::pair<Section, views::LabelButton*>> pills_;
  Section current_;
  bool delete_scheduled_ = false;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_SETTINGS_POPUP_H_

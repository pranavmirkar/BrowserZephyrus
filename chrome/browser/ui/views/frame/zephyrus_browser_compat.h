// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BROWSER_COMPAT_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BROWSER_COMPAT_H_

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"

namespace zephyrus {

// Chromium 156 hands BrowserView and BrowserCommandController a
// BrowserWindowInterface, and removed GetBrowserForMigrationOnly(). The Zephyrus
// windows were written against the concrete Browser. On desktop every
// BrowserWindowInterface IS a Browser (Browser is its only implementation outside
// unit-test fakes), so the downcast is exact; use it only where a Zephyrus API
// genuinely needs the class, and prefer BrowserWindowInterface where it does not.
inline Browser* AsBrowser(BrowserWindowInterface* window) {
  return static_cast<Browser*>(window);
}
inline const Browser* AsBrowser(const BrowserWindowInterface* window) {
  return static_cast<const Browser*>(window);
}

// Browser::is_type_normal() is gone; the type is read through the interface.
inline bool IsTypeNormal(const BrowserWindowInterface* window) {
  return window->GetType() == BrowserWindowInterface::TYPE_NORMAL;
}

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_BROWSER_COMPAT_H_

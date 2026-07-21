// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_THROTTLE_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_THROTTLE_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace content {
class BrowserContext;
class WebContents;
}

namespace zephyrus_adblock {

class ZephyrusAdblockService;

// A per-request throttle that cancels requests matching the ad-block engine.
// Runs on the UI thread (created from ChromeContentBrowserClient).
class ZephyrusAdblockThrottle : public blink::URLLoaderThrottle {
 public:
  using WebContentsGetter =
      base::RepeatingCallback<content::WebContents*()>;

  // Returns a throttle for `browser_context`, or nullptr if ad-blocking is
  // unavailable/disabled for it. `wc_getter` attributes blocks to a tab.
  static std::unique_ptr<ZephyrusAdblockThrottle> MaybeCreate(
      content::BrowserContext* browser_context,
      const WebContentsGetter& wc_getter);

  ZephyrusAdblockThrottle(base::WeakPtr<ZephyrusAdblockService> service,
                          WebContentsGetter wc_getter);
  ~ZephyrusAdblockThrottle() override;

  // blink::URLLoaderThrottle:
  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;

 private:
  base::WeakPtr<ZephyrusAdblockService> service_;
  WebContentsGetter wc_getter_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_THROTTLE_H_

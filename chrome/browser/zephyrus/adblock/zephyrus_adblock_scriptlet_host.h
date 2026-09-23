// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SCRIPTLET_HOST_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SCRIPTLET_HOST_H_

#include "chrome/browser/zephyrus/adblock/mojom/zephyrus_adblock.mojom.h"
#include "content/public/browser/document_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"

namespace content {
class RenderFrameHost;
}

namespace zephyrus_adblock {

// Browser-side implementation of the ScriptletHost interface, scoped to a
// document via content::DocumentService. Answers the renderer's surveys of the
// live document with the generic element-hiding CSS they unlock.
//
// The document-start payload is no longer fetched through here: the browser
// pushes it at ReadyToCommitNavigation. See ZephyrusAdblockTabHelper.
class ZephyrusAdblockScriptletHost
    : public content::DocumentService<mojom::ScriptletHost> {
 public:
  static void Create(content::RenderFrameHost* render_frame_host,
                     mojo::PendingReceiver<mojom::ScriptletHost> receiver);

  // The most tokens one survey may hand the browser, and the longest token
  // considered. A survey's tokens are a web page's class names and ids, so
  // their number and size are the page's to choose; without a bound a page
  // with a few hundred thousand generated class names had the browser's UI
  // thread look up every one. The limits sit far above any real page -- a
  // survey sends only tokens it has not sent before -- so a page that reaches
  // them is either hostile or has nothing an ad-hiding rule could match.
  static constexpr size_t kMaxTokensPerSurvey = 4096;
  static constexpr size_t kMaxTokenLength = 256;

  // mojom::ScriptletHost:
  void GetGenericCosmeticCss(const std::vector<std::string>& tokens,
                             GetGenericCosmeticCssCallback callback) override;

 private:
  ZephyrusAdblockScriptletHost(
      content::RenderFrameHost& render_frame_host,
      mojo::PendingReceiver<mojom::ScriptletHost> receiver);
  ~ZephyrusAdblockScriptletHost() override;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SCRIPTLET_HOST_H_

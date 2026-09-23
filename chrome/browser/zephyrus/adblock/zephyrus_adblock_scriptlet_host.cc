// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_scriptlet_host.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service.h"
#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_service_factory.h"
#include "content/public/browser/render_frame_host.h"

namespace zephyrus_adblock {

// static
void ZephyrusAdblockScriptletHost::Create(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<mojom::ScriptletHost> receiver) {
  // DocumentService owns itself; it is destroyed with the document or on
  // disconnect.
  new ZephyrusAdblockScriptletHost(*render_frame_host, std::move(receiver));
}

ZephyrusAdblockScriptletHost::ZephyrusAdblockScriptletHost(
    content::RenderFrameHost& render_frame_host,
    mojo::PendingReceiver<mojom::ScriptletHost> receiver)
    : content::DocumentService<mojom::ScriptletHost>(render_frame_host,
                                                     std::move(receiver)) {}

ZephyrusAdblockScriptletHost::~ZephyrusAdblockScriptletHost() = default;

void ZephyrusAdblockScriptletHost::GetGenericCosmeticCss(
    const std::vector<std::string>& tokens,
    GetGenericCosmeticCssCallback callback) {
  std::string css;
  if (ZephyrusAdblockService* service =
          ZephyrusAdblockServiceFactory::GetForBrowserContext(
              render_frame_host().GetBrowserContext())) {
    // Bounded before any lookup: see kMaxTokensPerSurvey.
    std::vector<std::string> bounded;
    bounded.reserve(std::min(tokens.size(), kMaxTokensPerSurvey));
    for (const std::string& token : tokens) {
      if (bounded.size() == kMaxTokensPerSurvey) {
        break;
      }
      if (!token.empty() && token.size() <= kMaxTokenLength) {
        bounded.push_back(token);
      }
    }
    css = service->GetGenericCosmeticCss(
        render_frame_host().GetLastCommittedURL(), bounded);
  }
  std::move(callback).Run(std::move(css));
}

}  // namespace zephyrus_adblock

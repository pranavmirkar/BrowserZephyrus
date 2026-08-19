// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/zephyrus_adblock_scriptlet_host.h"

#include <string>
#include <utility>

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

void ZephyrusAdblockScriptletHost::GetPayload(GetPayloadCallback callback) {
  std::string script;
  std::string css;
  if (ZephyrusAdblockService* service =
          ZephyrusAdblockServiceFactory::GetForBrowserContext(
              render_frame_host().GetBrowserContext())) {
    const GURL url = render_frame_host().GetLastCommittedURL();
    script = service->GetScriptletInjection(url);
    css = service->GetCosmeticCss(url);
  }
  std::move(callback).Run(std::move(script), std::move(css));
}

void ZephyrusAdblockScriptletHost::GetGenericCosmeticCss(
    const std::vector<std::string>& tokens,
    GetGenericCosmeticCssCallback callback) {
  std::string css;
  if (ZephyrusAdblockService* service =
          ZephyrusAdblockServiceFactory::GetForBrowserContext(
              render_frame_host().GetBrowserContext())) {
    css = service->GetGenericCosmeticCss(
        render_frame_host().GetLastCommittedURL(), tokens);
  }
  std::move(callback).Run(std::move(css));
}

}  // namespace zephyrus_adblock

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
// document via content::DocumentService. Resolves the frame's committed URL and
// profile to return the scriptlet payload the renderer injects at
// document-start.
class ZephyrusAdblockScriptletHost
    : public content::DocumentService<mojom::ScriptletHost> {
 public:
  static void Create(content::RenderFrameHost* render_frame_host,
                     mojo::PendingReceiver<mojom::ScriptletHost> receiver);

  // mojom::ScriptletHost:
  void GetPayload(GetPayloadCallback callback) override;

 private:
  ZephyrusAdblockScriptletHost(
      content::RenderFrameHost& render_frame_host,
      mojo::PendingReceiver<mojom::ScriptletHost> receiver);
  ~ZephyrusAdblockScriptletHost() override;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ZEPHYRUS_ADBLOCK_SCRIPTLET_HOST_H_

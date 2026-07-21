// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_ZEPHYRUS_ADBLOCK_SCRIPTLET_AGENT_H_
#define CHROME_RENDERER_ZEPHYRUS_ADBLOCK_SCRIPTLET_AGENT_H_

#include "chrome/browser/zephyrus/adblock/mojom/zephyrus_adblock.mojom.h"
#include "content/public/renderer/render_frame_observer.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace content {
class RenderFrame;
}

namespace zephyrus_adblock {

// Renderer-side agent: at document-start it fetches the scriptlet payload for
// the frame from the browser (sync, so it runs before page scripts) and injects
// it into the page's MAIN world. Self-owned (deleted on frame destruction).
class ScriptletAgent : public content::RenderFrameObserver {
 public:
  explicit ScriptletAgent(content::RenderFrame* render_frame);
  ScriptletAgent(const ScriptletAgent&) = delete;
  ScriptletAgent& operator=(const ScriptletAgent&) = delete;

  // content::RenderFrameObserver:
  void DidClearWindowObject() override;
  void OnDestruct() override;

 private:
  ~ScriptletAgent() override;

  // Fetches this frame's scriptlet payload from the browser and injects it into
  // the page's MAIN world.
  void InjectScriptlets();

  mojo::Remote<mojom::ScriptletHost> host_;
};

}  // namespace zephyrus_adblock

#endif  // CHROME_RENDERER_ZEPHYRUS_ADBLOCK_SCRIPTLET_AGENT_H_

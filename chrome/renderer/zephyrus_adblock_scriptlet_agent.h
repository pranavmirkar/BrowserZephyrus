// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_ZEPHYRUS_ADBLOCK_SCRIPTLET_AGENT_H_
#define CHROME_RENDERER_ZEPHYRUS_ADBLOCK_SCRIPTLET_AGENT_H_

#include <set>
#include <string>
#include <vector>

#include "base/memory/weak_ptr.h"
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
  void DidDispatchDOMContentLoadedEvent() override;
  void OnDestruct() override;

 private:
  ~ScriptletAgent() override;

  // Fetches this frame's scriptlet payload from the browser and injects it into
  // the page's MAIN world.
  void InjectScriptlets();

  // Reads the ids and classes present in the document and asks the browser
  // which generic hide rules they unlock.
  //
  // Runs several times: consent overlays are usually injected by a script well
  // after the document finishes loading, and a survey taken before the banner
  // exists cannot see the hooks it hangs on. Re-surveying is cheap because only
  // tokens not previously sent go to the browser, so a settled page converges
  // on sending nothing.
  void SurveyDocument();
  void ScheduleSurvey(base::TimeDelta delay);
  void OnGenericCssReady(const std::string& css);

  // Reads the document's id/class tokens out of an isolated world, so the
  // survey neither touches nor is observable by the page's own scripts.
  std::vector<std::string> CollectNewTokens();

  // Undoes a consent platform's scroll-lock once its banner has been hidden.
  // No-op unless we actually hid an overlay and none is still visible.
  void ReleaseScrollLock();

  // Injects `css` into the page as an appended stylesheet.
  void InjectCss(const std::string& css, const char* style_element_id);

  mojo::Remote<mojom::ScriptletHost> host_;

  // Tokens already sent for the current document, so each survey carries only
  // what the last one did not.
  std::set<std::string> sent_tokens_;
  int surveys_remaining_ = 0;

  base::WeakPtrFactory<ScriptletAgent> weak_factory_{this};
};

}  // namespace zephyrus_adblock

#endif  // CHROME_RENDERER_ZEPHYRUS_ADBLOCK_SCRIPTLET_AGENT_H_

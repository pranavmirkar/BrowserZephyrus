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
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace content {
class RenderFrame;
}

namespace zephyrus_adblock {

// Renderer-side agent. At document-start it applies the payload the browser
// PUSHED for this document -- scriptlets into the MAIN world, hiding CSS as
// injected stylesheets -- and hands the fingerprint seed on to
// FingerprintSeedAgent. Then it surveys the live document for the generic
// hiding rules. Self-owned (deleted on frame destruction).
class ScriptletAgent : public content::RenderFrameObserver,
                       public mojom::DocumentStartAgent {
 public:
  explicit ScriptletAgent(content::RenderFrame* render_frame);
  ScriptletAgent(const ScriptletAgent&) = delete;
  ScriptletAgent& operator=(const ScriptletAgent&) = delete;

  // content::RenderFrameObserver:
  void DidClearWindowObject() override;
  void DidDispatchDOMContentLoadedEvent() override;
  void OnDestruct() override;

  // mojom::DocumentStartAgent:
  void SetDocumentStartPayload(mojom::DocumentStartPayloadPtr payload) override;

 private:
  ~ScriptletAgent() override;

  void BindDocumentStartAgent(
      mojo::PendingAssociatedReceiver<mojom::DocumentStartAgent> receiver);

  // Applies the pushed payload to the document that just got its window, if
  // the payload is for this document.
  void InjectScriptlets();

  // Reads the ids and classes present in the document and asks the browser
  // which generic hide rules they unlock.
  //
  // Runs several times: consent overlays are usually injected by a script well
  // after the document finishes loading, and a survey taken before the banner
  // exists cannot see the hooks it hangs on. Re-surveying is cheap because only
  // tokens not previously sent go to the browser, so a settled page converges
  // on sending nothing. After the last pass the survey's observer is
  // disconnected -- see EndSurveys.
  void SurveyDocument();
  void ScheduleSurvey(base::TimeDelta delay);
  void OnGenericCssReady(const std::string& css);

  // Stops watching the document once the last survey has run.
  void EndSurveys();

  // Reads the document's id/class tokens out of an isolated world, so the
  // survey neither touches nor is observable by the page's own scripts.
  std::vector<std::string> CollectNewTokens();

  // Undoes a consent platform's scroll-lock once its banner has been hidden.
  void ReleaseScrollLock();

  // Inserts `css` into the document as an injected style sheet at `origin`.
  void InsertCss(const std::string& css, bool user_origin);

  mojo::AssociatedReceiver<mojom::DocumentStartAgent> document_start_receiver_{
      this};
  // The payload for the document about to commit. Consumed by the document it
  // names, and dropped by any other.
  mojom::DocumentStartPayloadPtr pending_payload_;

  // Per document: bound when the first survey needs it.
  mojo::Remote<mojom::ScriptletHost> host_;

  // Tokens already sent for the current document, so each survey carries only
  // what the last one did not.
  std::set<std::string> sent_tokens_;
  int surveys_remaining_ = 0;
  // True for the current document when it is a web page being surveyed.
  bool surveying_ = false;

  base::WeakPtrFactory<ScriptletAgent> weak_factory_{this};
};

}  // namespace zephyrus_adblock

#endif  // CHROME_RENDERER_ZEPHYRUS_ADBLOCK_SCRIPTLET_AGENT_H_

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/zephyrus_adblock_scriptlet_agent.h"

#include <string>

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "url/gurl.h"

namespace zephyrus_adblock {

ScriptletAgent::ScriptletAgent(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

ScriptletAgent::~ScriptletAgent() = default;

void ScriptletAgent::InjectScriptlets() {
  content::RenderFrame* rf = render_frame();
  if (!rf) {
    return;
  }
  blink::WebLocalFrame* frame = rf->GetWebFrame();
  if (!frame) {
    return;
  }
  // Only web frames can carry scriptlet rules; the browser also re-checks the
  // committed URL and returns an empty payload for anything non-matching.
  const GURL url = frame->GetDocument().Url();
  if (url.is_valid() && !url.SchemeIsHTTPOrHTTPS()) {
    return;
  }

  if (!host_.is_bound()) {
    rf->GetBrowserInterfaceBroker().GetInterface(
        host_.BindNewPipeAndPassReceiver());
  }
  // Synchronous so the scriptlet patches the page's objects before the page's
  // own scripts run. The browser resolves the committed URL and returns the
  // per-site scriptlet JS + cosmetic CSS (either may be empty).
  std::string script;
  std::string css;
  if (!host_->GetPayload(&script, &css)) {
    return;
  }
  if (!script.empty()) {
    frame->ExecuteScript(
        blink::WebScriptSource(blink::WebString::FromUtf8(script)));
  }
  if (!css.empty()) {
    // Inject the element-hiding stylesheet in the main world. At this hook the
    // <html> element may not exist yet, so append when the document element is
    // ready (immediately, or via a one-shot MutationObserver).
    std::string css_json;
    base::EscapeJSONString(css, /*put_in_quotes=*/true, &css_json);
    const std::string css_js = base::StrCat(
        {"(function(){var css=", css_json,
         ";function add(){try{if(document.getElementById('zephyrus-cosmetic'))"
         "return;var s=document.createElement('style');"
         "s.id='zephyrus-cosmetic';s.textContent=css;"
         "(document.head||document.documentElement).appendChild(s);}catch(e){}}"
         "if(document.documentElement){add();}else{var o=new MutationObserver("
         "function(){if(document.documentElement){o.disconnect();add();}});"
         "o.observe(document,{childList:true});}})();"});
    frame->ExecuteScript(
        blink::WebScriptSource(blink::WebString::FromUtf8(css_js)));
  }
}

void ScriptletAgent::DidClearWindowObject() {
  // The reliable main-world document-start hook: the fresh window/context for
  // the committed document exists here, before the page's scripts execute.
  // (DidCreateDocumentElement runs on a context that is later replaced, so
  // injection there does not persist.)
  InjectScriptlets();
}

void ScriptletAgent::OnDestruct() {
  delete this;
}

}  // namespace zephyrus_adblock

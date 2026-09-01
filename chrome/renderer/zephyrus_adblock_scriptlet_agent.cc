// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/zephyrus_adblock_scriptlet_agent.h"

#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/string_escape.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/platform/browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/scheduler/web_agent_group_scheduler.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"
#include "url/gurl.h"
#include "v8/include/v8-context.h"
#include "v8/include/v8-isolate.h"
#include "v8/include/v8-local-handle.h"
#include "v8/include/v8-primitive.h"
#include "v8/include/v8-value.h"

namespace zephyrus_adblock {

namespace {

// Re-survey delays, each measured from the previous survey, spanning roughly
// the first 35 seconds. Banners and ad shells are script-inserted, and a cold
// load builds far more slowly than a refresh off cache — a short window caught
// the refresh and missed the first visit, which showed up as ad containers that
// only disappeared the second time round.
//
// A long tail is affordable because the survey script skips the DOM walk when
// nothing has changed since the last pass (see kSurveyScript), so the later
// entries usually cost one function call.
constexpr base::TimeDelta kResurveyDelays[] = {
    base::Milliseconds(300),  base::Milliseconds(400),
    base::Milliseconds(600),  base::Milliseconds(1000),
    base::Milliseconds(1500), base::Milliseconds(2500),
    base::Milliseconds(4000), base::Milliseconds(6000),
    base::Milliseconds(8000), base::Milliseconds(10000),
};

// Walks the document for every id and class in it and returns the ones not
// already reported, one per line, prefixed the way the browser's index is keyed
// ("#id", ".class"). Newline-joined because the return trip is one string.
//
// State lives on the isolated world's global, which persists across calls for
// the same document and is invisible to the page. A MutationObserver sets a
// dirty flag so a pass over an unchanged document returns immediately instead
// of walking the DOM again — that is what makes a long tail of passes cheap.
constexpr char kSurveyScript[] =
    "(function(){try{"
    "var g=window.__zsSurvey;"
    "if(!g){g=window.__zsSurvey={seen:Object.create(null),dirty:true};"
    "new MutationObserver(function(){g.dirty=true;}).observe("
    "document.documentElement,{childList:true,subtree:true,"
    "attributes:true,attributeFilter:['class','id']});}"
    "if(!g.dirty)return '';"
    "g.dirty=false;"
    "var out=[],seen=g.seen;"
    "var els=document.querySelectorAll('[id],[class]');"
    "for(var i=0;i<els.length;i++){var e=els[i];"
    "var id=e.id;"
    "if(id&&!seen['#'+id]){seen['#'+id]=1;out.push('#'+id);}"
    "var cl=e.classList;"
    "if(cl){for(var j=0;j<cl.length;j++){var k='.'+cl[j];"
    "if(!seen[k]){seen[k]=1;out.push(k);}}}}"
    "return out.join('\\n');}catch(e){return '';}})();";

// Consent platforms lock scrolling while their banner is up, by putting a class
// on <html>/<body> that sets overflow:hidden (Sourcepoint's `sp-message-open`,
// OneTrust's `ot-overflow-hidden`, and so on). Hiding the banner leaves the
// class behind, so the reader gets a page they cannot scroll — a worse outcome
// than the banner. This releases the lock.
//
// The condition is deliberately about the page's state, not about proving we
// caused it: scrolling is disabled and nothing is visibly covering the page.
// From the reader's side that is simply a broken page, whoever stranded it.
// Tying it to "an element WE hid was an overlay" was tried first and is too
// brittle — whether our sheets happen to have hidden a fixed element varies
// with which rules a page trips on a given load. A legitimate modal keeps its
// lock because it is visible, which is what liveOverlay() checks.
//
// Installs itself once and re-checks on mutation. A consent script can add its
// lock at any moment, including well after the last survey, so a fixed number
// of timed passes would miss the slow ones. The observer is narrow (the root's
// attributes, body's direct children) and disconnects as soon as it succeeds
// or the page has clearly settled.
constexpr char kReleaseScrollLockScript[] =
    "(function(){try{"
    "if(window.__zephyrusUnlockWatching)return;"
    "window.__zephyrusUnlockWatching=1;"
    "var d=document.documentElement;"
    "function liveOverlay(){"
    "var els=document.querySelectorAll('*');"
    // <body> and <html> are skipped: the scroll-lock itself makes body
    // position:fixed, so leaving them in means the lock is mistaken for the
    // very overlay that would justify keeping it.
    "for(var i=0;i<els.length;i++){"
    "if(els[i]===document.body||els[i]===d)continue;"
    "var cs=getComputedStyle(els[i]);"
    "if(cs.position!=='fixed')continue;"
    "if(cs.display==='none'||cs.visibility==='hidden')continue;"
    // A fully transparent overlay is not blocking anything either.
    "if(parseFloat(cs.opacity)===0)continue;"
    "var r=els[i].getBoundingClientRect();"
    "if(r.height<=innerHeight*0.25)continue;"
    // Must actually be ON SCREEN. Height and visibility alone are not enough:
    // a slide-out navigation drawer is permanently in the DOM at full viewport
    // height, display:block, visibility:visible, opacity:1 — and parked off
    // screen with a transform until it is opened. Measured on
    // theguardian.com: #header-expanded-menu at x=-922 in an 838px viewport.
    //
    // Without this test that drawer counts as a live overlay forever, the
    // unlock is vetoed on every check, and the page is left with the consent
    // platform's scroll-lock still applied — banner hidden, page frozen. That
    // is a worse outcome than not filtering at all, because it looks fine.
    "if(r.right<=0||r.left>=innerWidth)continue;"
    "if(r.bottom<=0||r.top>=innerHeight)continue;"
    "return true;}"
    "return false;}"
    // Applied inline, not as a stylesheet rule. The lock is itself !important
    // and carried by a more specific selector (`html.sp-message-open body`),
    // which beats any rule we could add to a sheet; an inline !important
    // outranks every author declaration whatever its specificity.
    "function unlock(){"
    "var b=document.body,cs=getComputedStyle(b),saved=0;"
    "d.style.setProperty('overflow-y','auto','important');"
    "b.style.setProperty('overflow-y','auto','important');"
    // The other half of the standard scroll-lock: body is pinned with
    // position:fixed and offset by the scroll position, which collapses it to
    // zero height. Releasing overflow alone leaves a blank, frozen page. The
    // saved offset lives in `top`, so it doubles as where to put the reader
    // back.
    "if(cs.position==='fixed'){"
    "var top=parseInt(cs.top,10);if(top<0)saved=-top;"
    "['top','left','right','width','height'].forEach(function(k){"
    "b.style.setProperty(k,'auto','important');});"
    "b.style.setProperty('position','static','important');}"
    // The THIRD component of the lock, and the one that produced the
    // "scrolled but cannot scroll up" report: the body is dragged upward by a
    // negative margin equal to the saved offset. Undoing position and overflow
    // without this leaves scrollY at 0 while the content still looks scrolled
    // — so down works and up is impossible, because you are already at the top.
    // Measured on theguardian.com: margin-top -547px, first headline at -465px.
    "var mt=parseFloat(cs.marginTop);"
    "if(mt<0){if(!saved)saved=-mt;"
    "b.style.setProperty('margin-top','0','important');}"
    "if(saved>0&&!d.hasAttribute('data-zephyrus-restored')){"
    "d.setAttribute('data-zephyrus-restored','1');"
    "window.scrollTo(0,saved);}"
    "d.setAttribute('data-zephyrus-unlocked','1');}"
    // A pinned body counts as a lock when EITHER it carries a negative top —
    // the saved scroll offset, which only a scroll-lock does — or the document
    // can no longer scroll at all. Requiring body.clientHeight===0 (the first
    // attempt) was too narrow and missed the refresh-while-scrolled case, which
    // then left the page pinned at the old offset: content looks scrolled,
    // scrollY is 0, so it moves down but never up.
    //
    // Apps that legitimately pin the body anchor it at top:0 and stay
    // scrollable through an inner container, so neither arm catches them.
    "function locked(){"
    "var b=document.body,cs=getComputedStyle(b);"
    "if(cs.overflowY==='hidden'||getComputedStyle(d).overflowY==='hidden')"
    "return true;"
    // A body pulled substantially upward is a saved scroll offset, not a
    // layout choice. The threshold keeps a decorative -1px off the hook.
    "if(parseFloat(cs.marginTop)<-20)return true;"
    "if(cs.position==='fixed'){"
    "return parseInt(cs.top,10)<0||d.scrollHeight<=innerHeight+1;}"
    "return false;}"
    "function check(){"
    "var b=document.body;if(!b)return false;"
    "if(d.hasAttribute('data-zephyrus-unlocked')){unlock();return true;}"
    "if(!locked()||liveOverlay())return false;"
    "unlock();return true;}"
    "check();"
    "var timer=null;"
    // Deliberately keeps observing after a success: a consent script that
    // re-applies its lock gets the inline override re-asserted.
    "var o=new MutationObserver(function(){"
    "if(timer)return;"
    "timer=setTimeout(function(){timer=null;check();},150);});"
    "o.observe(d,{attributes:true,attributeFilter:['class','style']});"
    "if(document.body){o.observe(document.body,"
    "{attributes:true,attributeFilter:['class','style'],childList:true});}"
    "setTimeout(function(){o.disconnect();},20000);"
    "}catch(e){}})();";

}  // namespace

ScriptletAgent::ScriptletAgent(content::RenderFrame* render_frame)
    : content::RenderFrameObserver(render_frame) {}

ScriptletAgent::~ScriptletAgent() = default;

void ScriptletAgent::InjectCss(const std::string& css,
                               const char* style_element_id) {
  content::RenderFrame* rf = render_frame();
  if (!rf || css.empty()) {
    return;
  }
  blink::WebLocalFrame* frame = rf->GetWebFrame();
  if (!frame) {
    return;
  }
  // Injected in the main world. At document-start the <html> element may not
  // exist yet, so append when the document element is ready (immediately, or
  // via a one-shot MutationObserver). Appending to an existing element rather
  // than replacing it lets later surveys add to the same stylesheet.
  std::string css_json;
  base::EscapeJSONString(css, /*put_in_quotes=*/true, &css_json);
  std::string id_json;
  base::EscapeJSONString(style_element_id, /*put_in_quotes=*/true, &id_json);
  const std::string css_js = base::StrCat(
      {"(function(){var css=", css_json, ";var id=", id_json,
       ";function add(){try{var s=document.getElementById(id);"
       "if(s){s.textContent+=css;return;}"
       "s=document.createElement('style');s.id=id;s.textContent=css;"
       "(document.head||document.documentElement).appendChild(s);}catch(e){}}"
       "if(document.documentElement){add();}else{var o=new MutationObserver("
       "function(){if(document.documentElement){o.disconnect();add();}});"
       "o.observe(document,{childList:true});}})();"});
  frame->ExecuteScript(
      blink::WebScriptSource(blink::WebString::FromUtf8(css_js)));
}

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

  // A fresh document: nothing sent for it yet, and it gets its own survey
  // budget.
  sent_tokens_.clear();
  surveys_remaining_ = std::size(kResurveyDelays);

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
  InjectCss(css, "zephyrus-cosmetic");
}

std::vector<std::string> ScriptletAgent::CollectNewTokens() {
  std::vector<std::string> tokens;
  content::RenderFrame* rf = render_frame();
  if (!rf) {
    return tokens;
  }
  blink::WebLocalFrame* frame = rf->GetWebFrame();
  if (!frame) {
    return tokens;
  }

  v8::Isolate* isolate = frame->GetAgentGroupScheduler()->Isolate();
  v8::HandleScope handle_scope(isolate);
  // An isolated world shares the DOM but not the page's script context, so the
  // survey cannot be seen, shadowed, or tampered with by the page.
  const v8::Local<v8::Value> result =
      frame->ExecuteScriptInIsolatedWorldAndReturnValue(
          ISOLATED_WORLD_ID_CHROME_INTERNAL,
          blink::WebScriptSource(blink::WebString::FromUtf8(kSurveyScript)),
          blink::BackForwardCacheAware::kAllow);
  if (result.IsEmpty() || !result->IsString()) {
    return tokens;
  }
  const v8::String::Utf8Value utf8(isolate, result);
  if (!*utf8) {
    return tokens;
  }

  for (std::string_view token :
       base::SplitStringPiece(std::string_view(*utf8, utf8.length()), "\n",
                              base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    // Only what the browser has not already answered for this document.
    if (sent_tokens_.insert(std::string(token)).second) {
      tokens.emplace_back(token);
    }
  }
  return tokens;
}

void ScriptletAgent::SurveyDocument() {
  if (!host_.is_bound()) {
    return;
  }
  std::vector<std::string> tokens = CollectNewTokens();
  if (!tokens.empty()) {
    host_->GetGenericCosmeticCss(
        std::move(tokens),
        base::BindOnce(&ScriptletAgent::OnGenericCssReady,
                       weak_factory_.GetWeakPtr()));
  }
  if (surveys_remaining_ > 0) {
    const size_t index = std::size(kResurveyDelays) - surveys_remaining_;
    --surveys_remaining_;
    ScheduleSurvey(base::span(kResurveyDelays)[index]);
  }
}

void ScriptletAgent::ScheduleSurvey(base::TimeDelta delay) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ScriptletAgent::SurveyDocument,
                     weak_factory_.GetWeakPtr()),
      delay);
}

void ScriptletAgent::ReleaseScrollLock() {
  content::RenderFrame* rf = render_frame();
  if (!rf) {
    return;
  }
  if (blink::WebLocalFrame* frame = rf->GetWebFrame()) {
    frame->ExecuteScript(blink::WebScriptSource(
        blink::WebString::FromUtf8(kReleaseScrollLockScript)));
  }
}

void ScriptletAgent::OnGenericCssReady(const std::string& css) {
  // A separate stylesheet from the document-start one: this arrives in
  // instalments, and keeping them apart makes it obvious in devtools which
  // rules came from the survey.
  InjectCss(css, "zephyrus-cosmetic-generic");
  // Checked after every instalment, because the instalment that hides the
  // consent overlay is the one that strands the scroll-lock.
  ReleaseScrollLock();
}

void ScriptletAgent::DidClearWindowObject() {
  // The reliable main-world document-start hook: the fresh window/context for
  // the committed document exists here, before the page's scripts execute.
  // (DidCreateDocumentElement runs on a context that is later replaced, so
  // injection there does not persist.)
  InjectScriptlets();
}

void ScriptletAgent::DidDispatchDOMContentLoadedEvent() {
  // The curated document-start rules can strand a lock too, and their banner
  // is hidden well before the first survey answers.
  ReleaseScrollLock();
  // First survey: the parser is done, so the document's own markup is all
  // present. Script-injected banners are picked up by the re-surveys.
  SurveyDocument();
}

void ScriptletAgent::OnDestruct() {
  delete this;
}

}  // namespace zephyrus_adblock

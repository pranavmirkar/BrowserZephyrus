// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/zephyrus_adblock_scriptlet_agent.h"

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/string_split.h"
#include "base/task/single_thread_task_runner.h"
#include "base/time/time.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/renderer/render_frame.h"
#include "chrome/renderer/zephyrus_fingerprint_seed_agent.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/web/web_css_origin.h"
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
//
// Bounded on both axes, because both are the page's to choose. A pass returns
// at most 2048 new tokens (and stays dirty so the next pass picks up the rest,
// half the browser's own per-survey cap), and a document reports at most 20000
// in all --
// past that the page is generating class names, not naming ad containers, and
// the `seen` map would otherwise grow for as long as the page lived.
//
// The observer is kept on `g.mo` so EndSurveys() can disconnect it. It used to
// be anonymous and was never disconnected: every DOM mutation for the whole
// life of the page -- an infinite feed, a single-page app -- paid for a survey
// that had finished long before.
constexpr char kSurveyScript[] =
    "(function(){try{"
    "var g=window.__zsSurvey;"
    "if(!g){g=window.__zsSurvey={seen:Object.create(null),n:0,dirty:true};"
    "g.mo=new MutationObserver(function(){g.dirty=true;});"
    "g.mo.observe("
    "document.documentElement,{childList:true,subtree:true,"
    "attributes:true,attributeFilter:['class','id']});}"
    "if(!g.dirty||!g.seen)return '';"
    "g.dirty=false;"
    "var out=[],seen=g.seen;"
    "var els=document.querySelectorAll('[id],[class]');"
    "for(var i=0;i<els.length&&out.length<2048&&g.n<20000;i++){var e=els[i];"
    "var id=e.id;"
    "if(id&&!seen['#'+id]){seen['#'+id]=1;g.n++;out.push('#'+id);}"
    "var cl=e.classList;"
    "if(cl){for(var j=0;j<cl.length;j++){var k='.'+cl[j];"
    "if(!seen[k]){seen[k]=1;g.n++;out.push(k);}}}}"
    "if(out.length>=2048)g.dirty=true;"
    "return out.join('\\n');}catch(e){return '';}})();";

// Run after the last survey: stops observing the document and releases the
// `seen` map. Later surveys (there are none) would return nothing.
constexpr char kEndSurveyScript[] =
    "(function(){try{var g=window.__zsSurvey;if(g){"
    "if(g.mo)g.mo.disconnect();g.mo=null;g.seen=null;g.dirty=false;}"
    "}catch(e){}})();";

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
    : content::RenderFrameObserver(render_frame) {
  // The browser pushes each document's payload here at
  // ReadyToCommitNavigation. Channel-associated, so it arrives before the
  // commit it is for. Unretained is safe: the registry belongs to the frame,
  // and this agent lives exactly as long as the frame does.
  render_frame->GetAssociatedInterfaceRegistry()
      ->AddInterface<mojom::DocumentStartAgent>(base::BindRepeating(
          &ScriptletAgent::BindDocumentStartAgent, base::Unretained(this)));
}

ScriptletAgent::~ScriptletAgent() = default;

void ScriptletAgent::BindDocumentStartAgent(
    mojo::PendingAssociatedReceiver<mojom::DocumentStartAgent> receiver) {
  // The browser opens a fresh pipe per navigation.
  document_start_receiver_.reset();
  document_start_receiver_.Bind(std::move(receiver));
}

void ScriptletAgent::SetDocumentStartPayload(
    mojom::DocumentStartPayloadPtr payload) {
  // The seed goes to its own agent now, while the commit has not yet happened,
  // so it is in place before anything in the new document can read it.
  if (payload->fingerprint_known) {
    std::optional<zephyrus_privacy::FingerprintSeed> seed;
    if (payload->fingerprint_seed.size() ==
        std::tuple_size_v<zephyrus_privacy::FingerprintSeed>) {
      zephyrus_privacy::FingerprintSeed bytes;
      base::span(bytes).copy_from(base::span(payload->fingerprint_seed));
      seed = bytes;
    }
    if (auto* fingerprint =
            zephyrus_privacy::FingerprintSeedAgent::Get(render_frame())) {
      fingerprint->SetPushedSeed(payload->url, seed,
                                 payload->fingerprint_surfaces);
    }
  }
  pending_payload_ = std::move(payload);
}

void ScriptletAgent::InsertCss(const std::string& css, bool user_origin) {
  content::RenderFrame* rf = render_frame();
  if (!rf || css.empty()) {
    return;
  }
  blink::WebLocalFrame* frame = rf->GetWebFrame();
  if (!frame) {
    return;
  }
  // An injected style sheet, not a <style> element written by a script in the
  // page's own world. That was three problems at once:
  //  - Each instalment was APPENDED to one element's text, so the whole growing
  //    sheet was re-parsed every time a survey answered.
  //  - It ran in the MAIN world, where a page can shadow document.createElement
  //    or appendChild and see, block or rewrite what we inject.
  //  - The element was in the DOM, so an anti-adblock script could find it by
  //    id and remove it.
  // Injected sheets are not in the DOM and not reachable from script. Hiding
  // rules go in at USER origin: every one is !important, and a user !important
  // declaration outranks an author !important, so a page cannot un-hide an ad
  // with a more specific rule of its own.
  frame->GetDocument().InsertStyleSheet(
      blink::WebString::FromUtf8(css), /*key=*/nullptr,
      user_origin ? blink::WebCssOrigin::kUser : blink::WebCssOrigin::kAuthor);
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
  // A new document: nothing surveyed for it, and no host pipe yet -- the old
  // one belonged to the previous document.
  surveying_ = false;
  sent_tokens_.clear();
  host_.reset();

  // Only web frames carry ad-block rules.
  const GURL url = frame->GetDocument().Url();
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  surveying_ = true;
  surveys_remaining_ = std::size(kResurveyDelays);

  // The payload the browser pushed for this commit. Used only if it names this
  // document: a payload for anything else is left for the document it names,
  // or replaced by the next push.
  //
  // No payload means nothing is injected at document-start, rather than
  // blocking the page to ask. The browser pushes for every navigation in a tab,
  // so this is a frame outside one (an extension page's own frames, for
  // instance), where the old synchronous fetch was the only thing that paid.
  if (!pending_payload_ ||
      pending_payload_->url != url.GetWithoutRef().spec()) {
    return;
  }
  mojom::DocumentStartPayloadPtr payload = std::move(pending_payload_);
  if (!payload->script.empty()) {
    frame->ExecuteScript(
        blink::WebScriptSource(blink::WebString::FromUtf8(payload->script)));
  }
  InsertCss(payload->hide_css, /*user_origin=*/true);
  InsertCss(payload->style_css, /*user_origin=*/false);
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
  content::RenderFrame* rf = render_frame();
  if (!surveying_ || !rf) {
    return;
  }
  if (!host_.is_bound()) {
    rf->GetBrowserInterfaceBroker().GetInterface(
        host_.BindNewPipeAndPassReceiver());
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
  } else {
    EndSurveys();
  }
}

void ScriptletAgent::EndSurveys() {
  content::RenderFrame* rf = render_frame();
  if (!rf) {
    return;
  }
  if (blink::WebLocalFrame* frame = rf->GetWebFrame()) {
    frame->ExecuteScriptInIsolatedWorld(
        ISOLATED_WORLD_ID_CHROME_INTERNAL,
        blink::WebScriptSource(blink::WebString::FromUtf8(kEndSurveyScript)),
        blink::BackForwardCacheAware::kAllow);
  }
  // Nothing more will be sent for this document.
  sent_tokens_.clear();
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
  // In the isolated world, where the page cannot see it or shadow the DOM
  // methods it relies on. Styles and scroll position are the DOM's, shared by
  // every world, so undoing the lock from here undoes it for the page.
  if (blink::WebLocalFrame* frame = rf->GetWebFrame()) {
    frame->ExecuteScriptInIsolatedWorld(
        ISOLATED_WORLD_ID_CHROME_INTERNAL,
        blink::WebScriptSource(
            blink::WebString::FromUtf8(kReleaseScrollLockScript)),
        blink::BackForwardCacheAware::kAllow);
  }
}

void ScriptletAgent::OnGenericCssReady(const std::string& css) {
  InsertCss(css, /*user_origin=*/true);
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
  if (!surveying_) {
    return;
  }
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

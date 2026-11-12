// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/zephyrus/adblock/adblock_list_util.h"

namespace zephyrus_adblock {

namespace {

// A small, self-contained library of uBlock-Origin-compatible scriptlets. Kept
// intentionally focused (the YouTube-critical ones + a couple of common
// defusers); more can be ported over time. Exposed to the injection payload as
// `zephyrusScriptlets[name](...args)`.
constexpr char kScriptletLibrary[] = R"JS(
var zephyrusScriptlets = (function(){
  var realJsonParse = JSON.parse;
  var realJsonStringify = JSON.stringify;
  var toValue = function(raw){
    switch (raw) {
      case 'true': return true;
      case 'false': return false;
      case 'null': return null;
      case 'undefined': return undefined;
      case "''": return '';
      case '[]': return [];
      case '{}': return {};
      case 'emptyArr': return [];
      case 'emptyObj': return {};
      case 'noopFunc': return function(){};
      case 'noopCallbackFunc': return function(){ return function(){}; };
      case 'throwFunc': return function(){ throw new Error(); };
      case 'trueFunc': return function(){return true;};
      case 'falseFunc': return function(){return false;};
    }
    if (/^-?\d+$/.test(raw)) return parseInt(raw, 10);
    return raw;
  };
  var findOwner = function(root, path){
    var parts = path.split('.');
    var owner = root;
    for (var i = 0; i < parts.length - 1; i++){
      // Traverse through objects AND functions (e.g. Node.prototype.appendChild
      // walks through the Node constructor, a function).
      if (owner == null ||
          (typeof owner !== 'object' && typeof owner !== 'function')) {
        return null;
      }
      owner = owner[parts[i]];
    }
    return [owner, parts[parts.length - 1]];
  };
  // Returns a prune(obj) function for uBO json-prune style path lists. Handles
  // dotted paths and the "[-]" array-wildcard token (apply to every element).
  var makePruner = function(rawPrune, rawNeedle){
    var control = { 'important': 1 };  // uBO control tokens, not real paths.
    var clean = function(list){
      return (list || '').split(/ +/).filter(function(p){
        return p && !control[p];
      });
    };
    var prunePaths = clean(rawPrune);
    var needlePaths = clean(rawNeedle);
    // Resolve a path (with optional [-] wildcards) to a list of [owner, key].
    var resolve = function(root, parts, i, out){
      if (root == null || typeof root !== 'object') return;
      var part = parts[i];
      if (part === '[-]'){
        if (!Array.isArray(root)) return;
        for (var k = 0; k < root.length; k++){
          if (i === parts.length - 1){ out.push([root, k]); }
          else { resolve(root[k], parts, i + 1, out); }
        }
        return;
      }
      if (i === parts.length - 1){ out.push([root, part]); return; }
      resolve(root[part], parts, i + 1, out);
    };
    var hits = function(root, path){
      var out = [];
      resolve(root, path.split('.'), 0, out);
      return out;
    };
    var mustProcess = function(root){
      for (var i = 0; i < needlePaths.length; i++){
        var h = hits(root, needlePaths[i]);
        if (!h.length) return false;
        var any = false;
        for (var j = 0; j < h.length; j++){
          if (h[j][0] != null && (h[j][1] in Object(h[j][0]))) { any = true; break; }
        }
        if (!any) return false;
      }
      return true;
    };
    return function(o){
      if (o == null || typeof o !== 'object') return o;
      if (needlePaths.length && !mustProcess(o)) return o;
      for (var i = 0; i < prunePaths.length; i++){
        var h = hits(o, prunePaths[i]);
        for (var j = 0; j < h.length; j++){
          var owner = h[j][0];
          if (owner && typeof owner === 'object'){ try { delete owner[h[j][1]]; } catch(e){} }
        }
      }
      return o;
    };
  };
  var jsonPrune = function(rawPrune, rawNeedle){
    var prune = makePruner(rawPrune, rawNeedle);
    try {
      var jsonParse = JSON.parse;
      JSON.parse = function(){ return prune(jsonParse.apply(this, arguments)); };
    } catch(e){}
    try {
      if (self.Response && Response.prototype.json){
        var realJson = Response.prototype.json;
        Response.prototype.json = function(){
          return realJson.apply(this, arguments).then(function(o){ return prune(o); });
        };
      }
    } catch(e){}
  };
  var setConstant = function(path, rawValue){
    var value = toValue(rawValue);
    var parts = path.split('.');
    var install = function(owner, i){
      if (owner == null) return;
      var prop = parts[i];
      if (i === parts.length - 1){
        try {
          Object.defineProperty(owner, prop, {
            get: function(){ return value; }, set: function(){}, configurable: false
          });
        } catch(e){}
        return;
      }
      var existing = owner[prop];
      if (existing && typeof existing === 'object'){ install(existing, i + 1); return; }
      var next;
      try {
        Object.defineProperty(owner, prop, {
          get: function(){ return next; },
          set: function(nv){ next = nv; if (nv && typeof nv === 'object') install(nv, i + 1); },
          configurable: true
        });
      } catch(e){}
    };
    try { install(self, 0); } catch(e){}
  };
  var abortOnPropertyRead = function(path){
    if (!path) return;
    whenOwner(path, function(owner, prop){
      guardProperty(owner, prop, function(op){
        if (op === 'get') throw abortError();
      });
    });
  };
  // ---- Response replacers (the modern YouTube-desktop / Shorts path) ----
  // Builds a predicate matching a request URL against a uBO propsToMatch value
  // (empty = match all; "/re/flags" = regex; otherwise substring).
  var makeUrlTester = function(pat){
    if (!pat) return function(){ return true; };
    pat = String(pat).replace(/^url:/, '');
    var m = /^\/(.+)\/([a-z]*)$/.exec(pat);
    if (m){
      try { var re = new RegExp(m[1], m[2] || '');
            return function(u){ try { return re.test(u); } catch(e){ return false; } }; }
      catch(e){}
    }
    var lit = pat.replace(/[?*]+$/, '');
    return function(u){ return u.indexOf(lit) !== -1; };
  };
  // Builds a global RegExp from a uBO pattern ("/re/flags" or a literal string).
  var toReplaceRegex = function(str){
    var m = /^\/(.*)\/([a-z]*)$/.exec(str);
    if (m){ try { return new RegExp(m[1], m[2] || 'g'); } catch(e){} }
    return new RegExp(String(str).replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'g');
  };
  // Wraps self.fetch; handler(url, response) resolves to a replacement Response
  // or a falsy value to pass the original through untouched.
  var hookFetch = function(handler){
    try {
      var realFetch = self.fetch;
      if (typeof realFetch !== 'function') return;
      self.fetch = function(input){
        var ctx = this, args = arguments, url;
        try { url = (input && typeof input === 'object' && input.url)
                      ? input.url : String(input); } catch(e){ url = ''; }
        return realFetch.apply(ctx, args).then(function(resp){
          try {
            if (!resp || typeof resp.clone !== 'function') return resp;
            return Promise.resolve(handler(url, resp)).then(
                function(r){ return r || resp; }, function(){ return resp; });
          } catch(e){ return resp; }
        });
      };
    } catch(e){}
  };
  var makeResponse = function(body, src){
    try { return new Response(body, { status: src.status,
             statusText: src.statusText, headers: src.headers }); }
    catch(e){ return null; }
  };
  var trustedReplaceFetchResponse = function(pattern, replacement, urlPat){
    if (pattern === undefined || pattern === '') return;
    var tester = makeUrlTester(urlPat);
    var re = toReplaceRegex(pattern);
    var repl = replacement === undefined ? '' : replacement;
    hookFetch(function(url, resp){
      if (!tester(url)) return null;
      return resp.clone().text().then(function(text){
        var out; try { out = text.replace(re, repl); } catch(e){ return null; }
        return out === text ? null : makeResponse(out, resp);
      }, function(){ return null; });
    });
  };
  var jsonPruneFetchResponse = function(){
    var a = Array.prototype.slice.call(arguments);
    var urlPat = '';
    for (var i = 2; i + 1 < a.length; i++){
      if (a[i] === 'propsToMatch'){ urlPat = a[i + 1]; break; }
    }
    var tester = makeUrlTester(urlPat);
    var prune = makePruner(a[0] || '', a[1] || '');
    hookFetch(function(url, resp){
      if (!tester(url)) return null;
      return resp.clone().text().then(function(text){
        var obj; try { obj = JSON.parse(text); } catch(e){ return null; }
        prune(obj);
        return makeResponse(JSON.stringify(obj), resp);
      }, function(){ return null; });
    });
  };
  // XHR response rewriting (YouTube desktop delivers the player response via
  // XMLHttpRequest, which the fetch hooks above never see). Installs ONE
  // prototype wrapper; each scriptlet registers a {tester, onText, onObj}
  // handler. Uses LAZY getters so a transformed value is returned whenever the
  // page reads responseText/response after DONE — regardless of listener order.
  var xhrHandlers = null;
  var registerXhrHandler = function(handler){
    if (xhrHandlers){ xhrHandlers.push(handler); return; }
    xhrHandlers = [handler];
    var XHR = self.XMLHttpRequest;
    if (!XHR || !XHR.prototype) return;
    var proto = XHR.prototype;
    var realOpen = proto.open;
    var realSend = proto.send;
    var textDesc = Object.getOwnPropertyDescriptor(proto, 'responseText');
    var respDesc = Object.getOwnPropertyDescriptor(proto, 'response');
    if (!textDesc || !textDesc.get) return;
    proto.open = function(method, url){
      try { this.__zUrl = (typeof url === 'string') ? url
              : (url && url.href) ? url.href : ''; } catch(e){ this.__zUrl = ''; }
      return realOpen.apply(this, arguments);
    };
    proto.send = function(){
      var xhr = this;
      var url = xhr.__zUrl || '';
      var hs = [];
      for (var i = 0; i < xhrHandlers.length; i++){
        try { if (xhrHandlers[i].tester(url)) hs.push(xhrHandlers[i]); } catch(e){}
      }
      if (!hs.length) return realSend.apply(this, arguments);
      var applyText = function(raw){
        if (typeof raw !== 'string') return raw;
        for (var i = 0; i < hs.length; i++){
          if (!hs[i].onText) continue;
          try { var o = hs[i].onText(raw); if (typeof o === 'string') raw = o; } catch(e){}
        }
        return raw;
      };
      try {
        Object.defineProperty(xhr, 'responseText', { configurable: true, get: function(){
          var raw = textDesc.get.call(xhr);
          return xhr.readyState === 4 ? applyText(raw) : raw;
        }});
      } catch(e){}
      if (respDesc && respDesc.get){
        try {
          Object.defineProperty(xhr, 'response', { configurable: true, get: function(){
            var raw = respDesc.get.call(xhr);
            if (xhr.readyState !== 4) return raw;
            if (typeof raw === 'string') return applyText(raw);
            if (raw && typeof raw === 'object'){
              for (var i = 0; i < hs.length; i++){
                if (hs[i].onObj){ try { hs[i].onObj(raw); } catch(e){} }
              }
            }
            return raw;
          }});
        } catch(e){}
      }
      return realSend.apply(this, arguments);
    };
  };
  var trustedReplaceXhrResponse = function(pattern, replacement, urlPat){
    if (pattern === undefined || pattern === '') return;
    var tester = makeUrlTester(urlPat);
    var re = toReplaceRegex(pattern);
    var repl = replacement === undefined ? '' : replacement;
    registerXhrHandler({ tester: tester, onText: function(raw){
      try { return raw.replace(re, repl); } catch(e){ return raw; }
    }});
  };
  var jsonPruneXhrResponse = function(){
    var a = Array.prototype.slice.call(arguments);
    var urlPat = '';
    for (var i = 2; i + 1 < a.length; i++){
      if (a[i] === 'propsToMatch'){ urlPat = a[i + 1]; break; }
    }
    var tester = makeUrlTester(urlPat);
    var prune = makePruner(a[0] || '', a[1] || '');
    registerXhrHandler({
      tester: tester,
      onObj: function(obj){ prune(obj); },
      onText: function(raw){
        var obj; try { obj = JSON.parse(raw); } catch(e){ return raw; }
        prune(obj);
        try { return JSON.stringify(obj); } catch(e){ return raw; }
      }
    });
  };
  // Prevents the fresh-iframe hook bypass: pages append a blank <iframe> and
  // steal its un-hooked fetch/JSON.parse to sidestep the wrappers above.
  // Wraps a DOM insertion method; whenever an inserted node exposes a
  // contentWindow, the parent's (hooked) target function is copied into it.
  var trustedPreventDomBypass = function(methodPath, targetProp){
    if (!methodPath || !targetProp) return;
    var r = findOwner(self, methodPath);
    if (!r || r[0] == null) return;
    var owner = r[0], name = r[1];
    var real = owner[name];
    if (typeof real !== 'function') return;
    owner[name] = function(){
      var res = real.apply(this, arguments);
      try {
        for (var i = 0; i < arguments.length; i++){
          var n = arguments[i];
          if (!n || !n.contentWindow) continue;
          var src = findOwner(self, targetProp);
          var dst = findOwner(n.contentWindow, targetProp);
          if (src && src[0] != null && dst && dst[0] != null){
            dst[0][dst[1]] = src[0][src[1]];
          }
        }
      } catch(e){}
      return res;
    };
  };
  // trusted-click-element(selectors, extraMatch, delay)
  // Clicks elements as they appear. This is what dismisses YouTube's
  // "ad blockers violate our Terms of Service" dialog: the filter list points
  // it at the dismiss button, so the modal is gone before it is ever seen.
  // Selectors are comma-separated and clicked in order; each is watched until
  // it exists (elements are rendered late), then clicked once.
  var trustedClickElement = function(rawSelectors, extraMatch, rawDelay){
    if (!rawSelectors) return;
    var selectors = String(rawSelectors).split(/\s*,\s*/).filter(Boolean);
    if (!selectors.length) return;
    var maxWaitMs = 10000;
    var delay = parseInt(rawDelay, 10);
    if (isNaN(delay) || delay < 0) delay = 0;
    var started = Date.now();
    var index = 0;
    var clickNext = function(){
      if (index >= selectors.length) return true;
      var sel = selectors[index];
      var el = null;
      try { el = document.querySelector(sel); } catch(e){ index++; return false; }
      if (!el) return false;
      // Only click something the user could actually click.
      try { el.click(); } catch(e){}
      index++;
      return index >= selectors.length;
    };
    var stop = function(observer, timer){
      if (observer) observer.disconnect();
      if (timer) clearInterval(timer);
    };
    var begin = function(){
      var observer = null;
      var timer = null;
      var tick = function(){
        if (clickNext() || Date.now() - started > maxWaitMs){
          stop(observer, timer);
        }
      };
      // Poll AND observe: YouTube swaps nodes without always mutating in a way
      // a narrow observer would catch, and polling alone can miss a brief node.
      timer = setInterval(tick, 250);
      try {
        observer = new MutationObserver(tick);
        observer.observe(document.documentElement || document,
                         { childList: true, subtree: true });
      } catch(e){}
      tick();
    };
    if (delay > 0) setTimeout(begin, delay); else begin();
  };

  // no-xhr-if(pattern) — make matching XHRs no-op instead of reaching the net.
  var noXhrIf = function(rawPattern){
    var pattern = rawPattern || '';
    var re = null;
    if (pattern.length > 2 && pattern.charAt(0) === '/' &&
        pattern.charAt(pattern.length - 1) === '/'){
      try { re = new RegExp(pattern.slice(1, -1)); } catch(e){ re = null; }
    }
    var matches = function(url){
      if (!pattern) return true;
      if (re) { try { return re.test(url); } catch(e){ return false; } }
      return String(url).indexOf(pattern) !== -1;
    };
    var RealXhr = window.XMLHttpRequest;
    if (!RealXhr) return;
    var Wrapped = function(){
      var xhr = new RealXhr();
      var blocked = false;
      var realOpen = xhr.open;
      var realSend = xhr.send;
      xhr.open = function(method, url){
        blocked = matches(String(url));
        return realOpen.apply(xhr, arguments);
      };
      xhr.send = function(){
        if (!blocked) return realSend.apply(xhr, arguments);
        // Report a benign empty success rather than an error, so page code
        // that checks status doesn't treat it as "blocker present".
        Object.defineProperty(xhr, 'readyState', { value: 4, configurable: true });
        Object.defineProperty(xhr, 'status', { value: 200, configurable: true });
        Object.defineProperty(xhr, 'responseText', { value: '', configurable: true });
        Object.defineProperty(xhr, 'response', { value: '', configurable: true });
        setTimeout(function(){
          try { if (typeof xhr.onreadystatechange === 'function') xhr.onreadystatechange(); } catch(e){}
          try { xhr.dispatchEvent(new Event('readystatechange')); } catch(e){}
          try { xhr.dispatchEvent(new Event('load')); } catch(e){}
          try { xhr.dispatchEvent(new Event('loadend')); } catch(e){}
        }, 1);
      };
      return xhr;
    };
    Wrapped.prototype = RealXhr.prototype;
    ['UNSENT','OPENED','HEADERS_RECEIVED','LOADING','DONE'].forEach(function(k, i){
      try { Wrapped[k] = i; } catch(e){}
    });
    window.XMLHttpRequest = Wrapped;
  };

  // rmnt / trusted-rpnt — remove or replace text inside matching nodes.
  // rmnt(selector, pattern) deletes matching text; trusted-rpnt adds a
  // replacement string.
  //
  // The rules aim at INLINE scripts, and an inline script runs as soon as the
  // parser has it, so the edit has to land first. A mutation observer's
  // callback is a microtask, and the parser performs a microtask checkpoint
  // before it runs a script it has just inserted: the callback sees the node
  // with its text and before it executes. So matching nodes are edited inside
  // the callback, as they are added. MEASURED (YouTube, 2026-11-12): the old
  // version re-scanned the document 100 ms after a burst of mutations, long
  // after the scripts had run, so its edits never took effect.
  //
  // Pages that enforce Trusted Types (YouTube: require-trusted-types-for
  // 'script') refuse plain strings as script text, and each refusal is a
  // logged CSP violation that the page's report-uri can collect. The text is
  // written through a policy created here, at document start, before any page
  // script runs.
  var makeNodeTextEditor = function(removeWhole){ return function(rawSelector, rawPattern, rawReplacement){
    // A selector is required. Defaulting to '*' would match every node the
    // page ever adds.
    if (!rawSelector) return;
    var selector = rawSelector;
    var pattern = rawPattern || '';
    var replacement = rawReplacement === undefined ? '' : rawReplacement;
    var re = null;
    if (pattern.length > 2 && pattern.charAt(0) === '/'){
      var end = pattern.lastIndexOf('/');
      if (end > 0){
        try { re = new RegExp(pattern.slice(1, end), pattern.slice(end + 1) || 'g'); }
        catch(e){ re = null; }
      }
    }
    // Named options after the replacement, in pairs:
    //   condition <text|/re/>  only nodes whose text has it
    //   excludes  <text|/re/>  never nodes whose text has it
    //   sedCount / quitAfter <n>  stop after n edits in all
    // MEASURED (YouTube): ignoring `condition` put YouTube's fix into the
    // first script that matched the pattern, an early one that runs before the
    // page's client config exists, where it threw and stalled the player.
    var toTester = function(raw){
      if (raw === undefined || raw === '') return null;
      raw = String(raw);
      var m = raw.match(/^\/([\s\S]+)\/([a-z]*)$/);
      if (m){
        try {
          var r = new RegExp(m[1], m[2].replace('g', ''));
          return function(t){ return r.test(t); };
        } catch(e){ return function(){ return false; }; }
      }
      return function(t){ return t.indexOf(raw) !== -1; };
    };
    // rmnt takes no replacement: its options start one place earlier.
    var firstOption = removeWhole ? 2 : 3;
    if (removeWhole) replacement = '';
    var condition = null, excludes = null, budget = Infinity;
    for (var o = firstOption; o + 1 < arguments.length; o += 2){
      var key = arguments[o], val = arguments[o + 1];
      if (key === 'condition') condition = toTester(val);
      else if (key === 'excludes') excludes = toTester(val);
      else if (key === 'sedCount' || key === 'quitAfter'){
        var nval = parseInt(val, 10);
        if (nval > 0) budget = Math.min(budget, nval);
      }
    }
    var observer = null;
    var policy = textPolicy();
    var edit = function(node){
      if (budget <= 0) return;
      var text = node.textContent;
      if (!text) return;
      if (condition && !condition(text)) return;
      if (excludes && excludes(text)) return;
      var next;
      if (removeWhole){
        // uBO's remove-node-text empties the whole node when the pattern is
        // in it; removing only the matched words leaves broken script that
        // throws, which the page can see.
        var hit = re ? (re.lastIndex = 0, re.test(text)) : text.indexOf(pattern) !== -1;
        next = hit ? '' : text;
      } else {
        next = re ? text.replace(re, replacement)
                  : text.split(pattern).join(replacement);
      }
      if (next === text) return;
      try {
        node.textContent = (policy && node.nodeName === 'SCRIPT')
            ? policy.createScript(next) : next;
      } catch(e){ return; }
      if (--budget <= 0 && observer){
        try { observer.disconnect(); } catch(e){}
      }
    };
    var matches = function(node){
      try { return node.nodeType === 1 && node.matches(selector); }
      catch(e){ return false; }
    };
    try {
      var existing = document.querySelectorAll(selector);
      for (var i = 0; i < existing.length; i++) edit(existing[i]);
    } catch(e){ return; }
    if (budget <= 0) return;
    try {
      observer = new RealMutationObserver(function(records){
        for (var r = 0; r < records.length; r++){
          var added = records[r].addedNodes;
          for (var a = 0; a < added.length; a++){
            var node = added[a];
            if (matches(node)){
              edit(node);
            } else if (node.nodeType === 1 && node.firstElementChild){
              var inner;
              try { inner = node.querySelectorAll(selector); } catch(e){ continue; }
              for (var n = 0; n < inner.length; n++) edit(inner[n]);
            }
          }
        }
      });
      observer.observe(document.documentElement || document,
                       { childList: true, subtree: true });
    } catch(e){}
  }; };
  var replaceNodeText = makeNodeTextEditor(false);
  var removeNodeText = makeNodeTextEditor(true);

  // One Trusted Types policy for script text, made the first time a scriptlet
  // needs it (at document start, before the page's own scripts) and only on a
  // page where Trusted Types exist. Null if the page's policy forbids it.
  var textPolicyValue;
  var textPolicy = function(){
    if (textPolicyValue !== undefined) return textPolicyValue;
    textPolicyValue = null;
    try {
      var tt = self.trustedTypes;
      if (tt && typeof tt.createPolicy === 'function'){
        textPolicyValue = tt.createPolicy(
            'p' + Math.random().toString(36).slice(2, 10),
            { createScript: function(s){ return s; } });
      }
    } catch(e){}
    return textPolicyValue;
  };


  // uBO's JSON edit language, as the live rules use it. Compiled once per rule
  // into { matches(root), apply(root) }; null when the text is outside what is
  // understood, so nothing is half-applied: a malformed player request breaks
  // playback, which is worse than an ad.
  //
  //   [?<pred>][?<pred>]...  leading guards, every one must hold
  //   <pred>:  [!].path | [!].path<op><value>     (.. = at any depth)
  //            ops ==  *= (contains)  ^= (starts)  $= (ends)  =/regex/
  //   .name / ..name         child / every descendant of that name
  //   .name[?<pred>]         ... that satisfies the predicate
  //   actions:  = <value>  assign      += {json}  shallow-merge
  //   values:   "text", number, true/false, {json}, ${now},
  //             repl({"regex": "...", "replacement": "..."})
  var compileJsonEdit = function(editSpec){
    if (!editSpec) return null;
    var hasOwn = function(o, k){
      return Object.prototype.hasOwnProperty.call(o, k);
    };

    var parseValue = function(raw){
      raw = String(raw).trim();
      if (raw.indexOf('repl(') === 0 && raw.charAt(raw.length - 1) === ')'){
        try {
          // Rule authors write REGEX escapes here (\b, \d, \s, \w), which JSON
          // would read as its own (\b is a backspace) or reject. Keep every
          // backslash for the regex unless it escapes a quote or a backslash.
          var specText = raw.slice(5, -1).replace(/\\(?![\\"])/g, '\\\\');
          var spec = JSON.parse(specText);
          var body = String(spec.regex || '');
          var flags = '';
          // "/.../flags" is a regex literal written as a string.
          var m = body.match(/^\/([\s\S]*)\/([a-z]*)$/);
          if (m){ body = m[1]; flags = m[2]; }
          return { kind: 'repl', regex: new RegExp(body, flags),
                   replacement: spec.replacement || '' };
        } catch(e){ return null; }
      }
      if (raw.charAt(0) === '{' || raw.charAt(0) === '['){
        try { return { kind: 'json', value: JSON.parse(raw) }; }
        catch(e){ return null; }
      }
      // Strip quotes BEFORE testing for templates: the rules write ="${now}"
      // with quotes, and checking first left the literal text in the request.
      var quoted = false;
      var q = raw.charAt(0);
      if ((q === '"' || q === "'") && raw.length > 1 &&
          raw.charAt(raw.length - 1) === q){
        raw = raw.slice(1, -1);
        quoted = true;
      }
      if (raw === '${now}') return { kind: 'now' };
      if (quoted) return { kind: 'literal', value: raw };
      if (/^-?\d+(\.\d+)?$/.test(raw)){
        return { kind: 'literal', value: parseFloat(raw) };
      }
      if (raw === 'true') return { kind: 'literal', value: true };
      if (raw === 'false') return { kind: 'literal', value: false };
      if (raw === 'null') return { kind: 'literal', value: null };
      return { kind: 'literal', value: raw };
    };

    // Every value reachable under `name`, at any depth.
    var descend = function(node, name, out){
      if (node === null || typeof node !== 'object') return;
      if (Array.isArray(node)){
        for (var i = 0; i < node.length; i++) descend(node[i], name, out);
        return;
      }
      for (var k in node){
        if (!hasOwn(node, k)) continue;
        if (k === name) out.push({ parent: node, key: k, value: node[k] });
        descend(node[k], name, out);
      }
    };

    // ".a.b" or "..a.b" read from `node`: every value it names.
    var resolve = function(node, recursive, names){
      var current = [];
      if (recursive){
        descend(node, names[0], current);
      } else if (node && typeof node === 'object' && hasOwn(node, names[0])){
        current.push({ parent: node, key: names[0], value: node[names[0]] });
      }
      for (var i = 1; i < names.length; i++){
        var next = [];
        for (var j = 0; j < current.length; j++){
          var v = current[j].value;
          if (v && typeof v === 'object' && hasOwn(v, names[i])){
            next.push({ parent: v, key: names[i], value: v[names[i]] });
          }
        }
        current = next;
      }
      return current;
    };

    var compare = function(actual, op, expected){
      if (op === 'exists') return actual !== undefined;
      if (actual === undefined || actual === null) return false;
      var a = String(actual);
      if (op === '==') return a === String(expected);
      if (op === '*=') return a.indexOf(String(expected)) !== -1;
      if (op === '^=') return a.indexOf(String(expected)) === 0;
      if (op === '$=') {
        var e = String(expected);
        return a.length >= e.length && a.slice(a.length - e.length) === e;
      }
      if (op === '=~') return expected.test(a);
      return false;
    };

    // "[?...]" -> function(node) -> bool, or null if not understood.
    var parsePredicate = function(text){
      var m = text.match(/^\[\?(!?)(\.{1,2})([A-Za-z0-9_$]+(?:\.[A-Za-z0-9_$]+)*)(?:(==|\*=|\^=|\$=|=)([\s\S]*))?\]$/);
      if (!m) return null;
      var negate = m[1] === '!';
      var recursive = m[2] === '..';
      var names = m[3].split('.');
      var op = m[4] || 'exists';
      var expected;
      if (op !== 'exists'){
        var rawVal = m[5].trim();
        if (op === '=' && rawVal.charAt(0) === '/'){
          var rm = rawVal.match(/^\/([\s\S]*)\/([a-z]*)$/);
          if (!rm) return null;
          try { expected = new RegExp(rm[1], rm[2]); } catch(e){ return null; }
          op = '=~';
        } else if (op === '='){
          return null;
        } else {
          var parsed = parseValue(rawVal);
          if (!parsed || parsed.kind !== 'literal') return null;
          expected = parsed.value;
        }
      }
      return function(node){
        var hits = resolve(node, recursive, names);
        var hold = false;
        for (var i = 0; i < hits.length && !hold; i++){
          hold = compare(hits[i].value, op, expected);
        }
        return negate ? !hold : hold;
      };
    };

    // The "[...]" group at the start of `text`, brackets balanced and quoted
    // text skipped; -1 if there is none.
    var groupEnd = function(text){
      if (text.charAt(0) !== '[') return -1;
      var depth = 0;
      var quote = '';
      for (var i = 0; i < text.length; i++){
        var ch = text.charAt(i);
        if (quote){
          if (ch === '\\') { i++; continue; }
          if (ch === quote) quote = '';
          continue;
        }
        if (ch === '"') { quote = ch; continue; }
        if (ch === '[') depth++;
        else if (ch === ']' && --depth === 0) return i;
      }
      return -1;
    };

    var spec = String(editSpec).trim();
    var guards = [];
    while (spec.charAt(0) === '['){
      var close = groupEnd(spec);
      if (close === -1) return null;
      var guard = parsePredicate(spec.slice(0, close + 1));
      if (!guard) return null;
      guards.push(guard);
      spec = spec.slice(close + 1);
    }

    // The action operator OUTSIDE any predicate, so "==" inside [?...] is not
    // mistaken for the assignment.
    var depth = 0;
    var opIndex = -1;
    var action = 'assign';
    for (var i = 0; i < spec.length; i++){
      var ch = spec.charAt(i);
      if (ch === '[') depth++;
      else if (ch === ']') depth--;
      else if (depth === 0 && ch === '='){
        if (spec.charAt(i - 1) === '+'){ opIndex = i - 1; action = 'merge'; }
        else { opIndex = i; action = 'assign'; }
        break;
      }
    }
    if (opIndex === -1) return null;
    var pathText = spec.slice(0, opIndex);
    var valueText = spec.slice(opIndex + (action === 'merge' ? 2 : 1));
    var parsedValue = parseValue(valueText);
    if (!parsedValue) return null;
    if (action === 'merge' && parsedValue.kind !== 'json') return null;

    var tokens = [];
    var rest = pathText;
    while (rest.length){
      var tm = rest.match(/^(\.{1,2})([A-Za-z0-9_$]+)/);
      if (!tm) return null;
      var token = { recursive: tm[1] === '..', name: tm[2], filter: null };
      rest = rest.slice(tm[0].length);
      if (rest.charAt(0) === '['){
        var end = groupEnd(rest);
        if (end === -1) return null;
        token.filter = parsePredicate(rest.slice(0, end + 1));
        if (!token.filter) return null;
        rest = rest.slice(end + 1);
      }
      tokens.push(token);
    }
    if (!tokens.length) return null;

    var matches = function(root){
      if (!root || typeof root !== 'object') return false;
      for (var g = 0; g < guards.length; g++){
        if (!guards[g](root)) return false;
      }
      return true;
    };

    var apply = function(root){
      if (!matches(root)) return false;
      var current = [{ parent: null, key: null, value: root }];
      for (var t = 0; t < tokens.length; t++){
        var tok = tokens[t];
        var next = [];
        for (var c = 0; c < current.length; c++){
          var hits = resolve(current[c].value, tok.recursive, [tok.name]);
          for (var h = 0; h < hits.length; h++){
            if (tok.filter && !tok.filter(hits[h].value)) continue;
            next.push(hits[h]);
          }
        }
        current = next;
        if (!current.length) return false;
      }

      var changed = false;
      for (var n = 0; n < current.length; n++){
        var target = current[n];
        if (action === 'merge'){
          if (!target.value || typeof target.value !== 'object') continue;
          for (var k2 in parsedValue.value){
            if (hasOwn(parsedValue.value, k2)){
              target.value[k2] = parsedValue.value[k2];
              changed = true;
            }
          }
          continue;
        }
        var newValue;
        if (parsedValue.kind === 'now'){
          newValue = String(Date.now());
        } else if (parsedValue.kind === 'repl'){
          var text = String(target.value === undefined ? '' : target.value);
          newValue = text.replace(parsedValue.regex, parsedValue.replacement);
        } else {
          newValue = parsedValue.value;
        }
        if (target.parent && target.value !== newValue){
          target.parent[target.key] = newValue;
          changed = true;
        }
      }
      return changed;
    };

    return { matches: matches, apply: apply };
  };

  // trusted-json-edit-xhr-request(edit, propsToMatch, <urlPattern>)
  // Rewrites the JSON body of an outgoing XHR. Kept for the lists that name
  // it; NOT registered for YouTube's sake (see the table).
  var jsonEditXhrRequest = function(editSpec, propName, propValue){
    var edit = compileJsonEdit(editSpec);
    if (!edit) return;
    var urlNeedle = '';
    if (propName === 'propsToMatch' && propValue) urlNeedle = String(propValue);
    var RealXhr = window.XMLHttpRequest;
    if (!RealXhr || !RealXhr.prototype) return;
    var realOpen = RealXhr.prototype.open;
    var realSend = RealXhr.prototype.send;
    RealXhr.prototype.open = function(method, url){
      try { this.__zephyrusUrl = String(url || ''); } catch(e){}
      return realOpen.apply(this, arguments);
    };
    RealXhr.prototype.send = function(body){
      try {
        var url = this.__zephyrusUrl || '';
        if (urlNeedle && url.indexOf(urlNeedle) !== -1 &&
            typeof body === 'string' && body.charAt(0) === '{'){
          var parsed = JSON.parse(body);
          if (edit.apply(parsed)) arguments[0] = JSON.stringify(parsed);
        }
      } catch(e){
        // Never let an edit failure break the request: a broken /player call is
        // far more visible - and more detectable - than an unblocked ad.
      }
      return realSend.apply(this, arguments);
    };
  };

  // trusted-edit-inbound-object(fnPath, argPosition, edit)
  // Edits the object a page passes to a function, before the function sees it.
  // YouTube's player builds its /player request with JSON.stringify(body); the
  // rules edit that body (the context it says it is playing in) so the reply
  // carries no ads. Hooking the serialiser, not the transport, works whether
  // the request then goes by fetch or by XHR -- the reason the XHR version
  // above never fired on YouTube.
  //
  // The page's own object is never changed: the guards are checked on it, and
  // only when they hold is a COPY edited and passed on instead. Any failure
  // passes the original through untouched.
  var trustedEditInboundObject = function(fnPath, argPosRaw, editSpec){
    var edit = compileJsonEdit(editSpec);
    if (!edit || !fnPath) return;
    var pos = parseInt(argPosRaw, 10);
    if (!(pos >= 0)) pos = 0;
    var found = findOwner(self, String(fnPath));
    if (!found || !found[0]) return;
    var owner = found[0];
    var name = found[1];
    patchMethod(owner, name, function(target, thisArg, args){
      try {
        var arg = args[pos];
        if (arg && typeof arg === 'object' && edit.matches(arg)){
          // A copy by value, through the real serialiser and parser: the
          // argument is about to be serialised anyway, so nothing a copy
          // would lose (functions, undefined) would have survived.
          var copy = realJsonParse(Reflect.apply(realJsonStringify, JSON, [arg]));
          if (edit.apply(copy)){
            var edited = Array.prototype.slice.call(args);
            edited[pos] = copy;
            return Reflect.apply(target, thisArg, edited);
          }
        }
      } catch(e){}
      return Reflect.apply(target, thisArg, args);
    });
  };

  // ---- Defusers: the anti-adblock and ad-script neutralisers ----
  // Everything below reaches the page's functions through patchMethod(), which
  // installs a Proxy rather than a plain wrapper: a Proxy of a native function
  // still stringifies as "[native code]", the probe the patch-detecting
  // scripts use (see the note on trusted-json-edit-xhr-request below).

  // Captured before any scriptlet runs, so a defuser aimed at setTimeout or
  // addEventListener can never catch this library's own calls.
  var realSetTimeout = self.setTimeout;
  var realAddEventListener = self.EventTarget && EventTarget.prototype.addEventListener;
  var RealMutationObserver = self.MutationObserver;
  var later = function(fn, ms){
    try { return Reflect.apply(realSetTimeout, self, [fn, ms || 0]); } catch(e){}
  };
  var listen = function(target, type, fn, opts){
    try { Reflect.apply(realAddEventListener, target, [type, fn, opts]); } catch(e){}
  };

  // Aborted scripts die with this error; the page never sees it reported.
  var abortMagic = 'zx' + Math.random().toString(36).slice(2);
  listen(self, 'error', function(ev){
    try {
      if (ev && ev.error instanceof ReferenceError &&
          String(ev.error.message).indexOf(abortMagic) !== -1){
        ev.preventDefault();
        ev.stopImmediatePropagation();
      }
    } catch(e){}
  }, true);
  var abortError = function(){ return new ReferenceError(abortMagic); };

  // A uBO needle: '' matches anything, '/re/flags' is a regex, a leading '!'
  // inverts, anything else is a literal substring.
  var makeMatcher = function(raw){
    var s = (raw === undefined || raw === null) ? '' : String(raw);
    var invert = false;
    if (s.charAt(0) === '!'){ invert = true; s = s.slice(1); }
    var test;
    var m = /^\/(.+)\/([imsu]*)$/.exec(s);
    var re = null;
    if (m){ try { re = new RegExp(m[1], m[2]); } catch(e){ re = null; } }
    if (s === '') test = function(){ return true; };
    else if (re) test = function(t){ return re.test(t); };
    else test = function(t){ return t.indexOf(s) !== -1; };
    return function(value){
      var hit;
      try { hit = test(String(value)); } catch(e){ hit = false; }
      return invert ? !hit : hit;
    };
  };
  var textOf = function(fn){
    try {
      if (typeof fn === 'function') return String(fn);
      if (fn && typeof fn.handleEvent === 'function') return String(fn.handleEvent);
      return String(fn);
    } catch(e){ return ''; }
  };
  var patchMethod = function(owner, name, handler){
    try {
      var real = owner[name];
      if (typeof real !== 'function') return;
      owner[name] = new Proxy(real, { apply: function(target, thisArg, args){
        return handler(target, thisArg, args);
      }});
    } catch(e){}
  };

  // Calls onOwner(owner, prop) for the object holding the last segment of
  // `path` -- now, or once the page creates a parent that does not exist yet.
  var whenOwner = function(path, onOwner){
    var parts = String(path).split('.');
    var walk = function(owner, i){
      if (owner == null) return;
      var prop = parts[i];
      if (i === parts.length - 1){ onOwner(owner, prop); return; }
      var existing;
      try { existing = owner[prop]; } catch(e){ return; }
      if (existing != null &&
          (typeof existing === 'object' || typeof existing === 'function')){
        walk(existing, i + 1);
        return;
      }
      var held = existing;
      try {
        Object.defineProperty(owner, prop, { configurable: true, enumerable: true,
          get: function(){ return held; },
          set: function(v){
            held = v;
            if (v != null && (typeof v === 'object' || typeof v === 'function')) walk(v, i + 1);
          } });
      } catch(e){}
    };
    walk(self, 0);
  };
  // Replaces `owner[prop]` with an accessor that runs `check` on every read
  // and write, and otherwise behaves like the property it replaced.
  var guardProperty = function(owner, prop, check){
    var desc;
    try { desc = Object.getOwnPropertyDescriptor(owner, prop); } catch(e){ return; }
    if (desc && desc.configurable === false) return;
    var getter = desc && desc.get;
    var setter = desc && desc.set;
    var value;
    if (desc && 'value' in desc) value = desc.value;
    else if (!desc) { try { value = owner[prop]; } catch(e){} }
    try {
      Object.defineProperty(owner, prop, {
        configurable: true, enumerable: desc ? !!desc.enumerable : true,
        get: function(){ check('get'); return getter ? getter.call(this) : value; },
        set: function(v){ check('set'); if (setter) setter.call(this, v); else value = v; }
      });
    } catch(e){}
  };

  // abort-current-script(property, needle): throws when a <script> whose
  // text (or src, for an external one) matches `needle` touches `property`.
  var abortCurrentScript = function(target, needle){
    if (!target) return;
    var matches = makeMatcher(needle);
    var ours = document.currentScript;
    whenOwner(target, function(owner, prop){
      guardProperty(owner, prop, function(){
        var s = document.currentScript;
        if (!s || s === ours || !('text' in s)) return;
        var text = s.src ? s.src : s.textContent;
        if (matches(text)) throw abortError();
      });
    });
  };
  // abort-on-property-write(property): the script assigning it dies.
  var abortOnPropertyWrite = function(target){
    if (!target) return;
    whenOwner(target, function(owner, prop){
      guardProperty(owner, prop, function(op){
        if (op === 'set') throw abortError();
      });
    });
  };
  // abort-on-stack-trace(property, needle): the access dies when the call
  // stack matches. 'inlineScript' and 'injectedScript' are uBO's shorthands.
  var abortOnStackTrace = function(target, needleRaw){
    if (!target || !needleRaw) return;
    var needle = String(needleRaw);
    if (needle === 'inlineScript') needle = String(location.href).split('#')[0];
    else if (needle === 'injectedScript') needle = '<anonymous>';
    var matches = makeMatcher(needle);
    whenOwner(target, function(owner, prop){
      guardProperty(owner, prop, function(){
        var stack = '';
        try { stack = String(new Error().stack || ''); } catch(e){}
        // Lines 0-2 are "Error", this check and the accessor: ours.
        stack = stack.split('\n').slice(3).join('\n');
        if (matches(stack)) throw abortError();
      });
    });
  };

  // no-setTimeout-if / no-setInterval-if / no-requestAnimationFrame-if:
  // matching callbacks are swapped for a no-op (the timer id is still real).
  // uBO treats a call with no arguments as "log only", so that is a no-op.
  var preventTimer = function(which){
    return function(needleRaw, delayRaw){
      var hasNeedle = needleRaw !== undefined && needleRaw !== '';
      var hasDelay = delayRaw !== undefined && delayRaw !== '';
      if (!hasNeedle && !hasDelay) return;
      var matches = makeMatcher(needleRaw);
      var delay = null, delayInvert = false;
      if (hasDelay){
        var d = String(delayRaw);
        if (d.charAt(0) === '!'){ delayInvert = true; d = d.slice(1); }
        delay = parseInt(d, 10);
        if (isNaN(delay)) delay = null;
      }
      patchMethod(self, which, function(target, thisArg, args){
        var hit = matches(textOf(args[0]));
        if (hit && delay !== null){
          var same = Number(args[1]) === delay;
          hit = delayInvert ? !same : same;
        }
        if (hit){
          args = Array.prototype.slice.call(args);
          args[0] = function(){};
        }
        return Reflect.apply(target, thisArg, args);
      });
    };
  };
  // nano-setTimeout-booster / nano-setInterval-booster: rescale matching
  // delays (ad countdowns). Args: needle, delay ('*' = any), boost factor.
  var nanoBooster = function(which){
    return function(needleRaw, delayRaw, boostRaw){
      var matches = makeMatcher(needleRaw);
      var delay = delayRaw === '*' ? -1 : parseInt(delayRaw, 10);
      if (isNaN(delay)) delay = 1000;
      var boost = parseFloat(boostRaw);
      if (isNaN(boost) || boost < 0.001 || boost > 50) boost = 0.05;
      patchMethod(self, which, function(target, thisArg, args){
        if ((delay === -1 || Number(args[1]) === delay) && matches(textOf(args[0]))){
          args = Array.prototype.slice.call(args);
          args[1] = Number(args[1]) * boost;
        }
        return Reflect.apply(target, thisArg, args);
      });
    };
  };

  // prevent-addEventListener(type, needle): matching listeners are dropped.
  var preventAddEventListener = function(typeRaw, needleRaw){
    if (!typeRaw && !needleRaw) return;
    var typeOk = makeMatcher(typeRaw);
    var needleOk = makeMatcher(needleRaw);
    if (!self.EventTarget) return;
    patchMethod(EventTarget.prototype, 'addEventListener', function(target, thisArg, args){
      var type;
      try { type = String(args[0]); } catch(e){ type = ''; }
      if (typeOk(type) && needleOk(textOf(args[1]))) return undefined;
      return Reflect.apply(target, thisArg, args);
    });
  };

  // prevent-window-open(needle): pop-ups whose URL matches never open. The
  // opener gets a stand-in window, since a null return is itself the signal
  // popunder scripts use to detect a blocker.
  var preventWindowOpen = function(needleRaw){
    var matches = makeMatcher(needleRaw);
    patchMethod(self, 'open', function(target, thisArg, args){
      var url = '';
      try { url = args.length ? String(args[0]) : ''; } catch(e){}
      if (!matches(url)) return Reflect.apply(target, thisArg, args);
      var noop = function(){};
      var fake = {
        closed: false, name: '', opener: null,
        location: { href: url, assign: noop, replace: noop, reload: noop },
        document: { open: noop, write: noop, writeln: noop, close: noop },
        focus: noop, blur: noop, postMessage: noop, print: noop,
        close: function(){ fake.closed = true; }
      };
      return fake;
    });
  };

  // prevent-fetch(props): matching fetches resolve to an empty 200 without
  // leaving the page. `props` is "url:x method:POST ..." or just a URL needle.
  var preventFetch = function(propsRaw, bodyKind){
    if (!propsRaw) return;
    var keys = { url: 1, method: 1, body: 1, mode: 1, credentials: 1,
                 cache: 1, redirect: 1, referrer: 1, referrerPolicy: 1 };
    var conds = [];
    String(propsRaw).split(/\s+/).forEach(function(tok){
      if (!tok) return;
      var i = tok.indexOf(':');
      var key = i > 0 ? tok.slice(0, i) : '';
      if (keys[key] === 1) conds.push([key, makeMatcher(tok.slice(i + 1))]);
      else conds.push(['url', makeMatcher(tok)]);
    });
    if (!conds.length) return;
    patchMethod(self, 'fetch', function(target, thisArg, args){
      var input = args[0];
      var init = args[1] || {};
      var details = {};
      try {
        details.url = (input && typeof input === 'object' && 'url' in input)
            ? String(input.url) : String(input);
      } catch(e){ details.url = ''; }
      try { details.method = String(init.method || (input && input.method) || 'GET'); }
      catch(e){ details.method = 'GET'; }
      var hit = conds.every(function(c){
        var v = details[c[0]];
        if (v === undefined){ try { v = init[c[0]]; } catch(e){} }
        return v !== undefined && c[1](v);
      });
      if (!hit) return Reflect.apply(target, thisArg, args);
      var body = bodyKind === 'emptyObj' ? '{}' : bodyKind === 'emptyArr' ? '[]' : '';
      try {
        var resp = new Response(body, { status: 200, statusText: 'OK' });
        // Absolute, as a real response's url always is.
        var absolute = details.url;
        try { absolute = new URL(details.url, document.baseURI).href; } catch(e){}
        try { Object.defineProperty(resp, 'url', { value: absolute }); } catch(e){}
        try { Object.defineProperty(resp, 'type', { value: 'basic' }); } catch(e){}
        return Promise.resolve(resp);
      } catch(e){ return Reflect.apply(target, thisArg, args); }
    });
  };

  // Runs `apply` now, when the DOM is parsed, and (coalesced) after mutations.
  var watchDom = function(apply, attributeFilter){
    var run = function(){ try { apply(); } catch(e){} };
    var pending = false;
    var schedule = function(){
      if (pending) return;
      pending = true;
      later(function(){ pending = false; run(); }, 50);
    };
    run();
    if (document.readyState === 'loading') listen(document, 'DOMContentLoaded', run, { once: true });
    if (!RealMutationObserver) return;
    try {
      var options = { childList: true, subtree: true };
      if (attributeFilter){ options.attributes = true; options.attributeFilter = attributeFilter; }
      new RealMutationObserver(schedule).observe(document.documentElement || document, options);
    } catch(e){}
  };
  // remove-attr(attrs, selector) / remove-class(classes, selector): strip
  // them wherever they appear, and keep stripping as the page re-adds them.
  var removeAttr = function(attrsRaw, selectorRaw){
    if (!attrsRaw) return;
    var attrs = String(attrsRaw).split(/\s*\|\s*/).filter(function(a){
      return /^[A-Za-z_:][\w:.-]*$/.test(a);
    });
    if (!attrs.length) return;
    var selector = selectorRaw ? String(selectorRaw)
        : attrs.map(function(a){ return '[' + a + ']'; }).join(',');
    watchDom(function(){
      var nodes = document.querySelectorAll(selector);
      for (var i = 0; i < nodes.length; i++){
        for (var j = 0; j < attrs.length; j++){
          if (nodes[i].hasAttribute(attrs[j])) nodes[i].removeAttribute(attrs[j]);
        }
      }
    }, attrs);
  };
  var removeClass = function(classesRaw, selectorRaw){
    if (!classesRaw) return;
    var classes = String(classesRaw).split(/\s*\|\s*/).filter(function(c){
      return /^-?[A-Za-z_][\w-]*$/.test(c);
    });
    if (!classes.length) return;
    var selector = selectorRaw ? String(selectorRaw)
        : classes.map(function(c){ return '.' + c; }).join(',');
    watchDom(function(){
      var nodes = document.querySelectorAll(selector);
      for (var i = 0; i < nodes.length; i++){
        for (var j = 0; j < classes.length; j++) nodes[i].classList.remove(classes[j]);
      }
    }, ['class']);
  };

  // noeval-if(needle): matching eval() calls return undefined unrun.
  var noEvalIf = function(needleRaw){
    var matches = makeMatcher(needleRaw);
    patchMethod(self, 'eval', function(target, thisArg, args){
      if (matches(textOf(args[0]))) return undefined;
      return Reflect.apply(target, thisArg, args);
    });
  };

  // Stand-ins for the BlockAdBlock / FuckAdBlock detector libraries: always
  // report "no blocker", and run the page's not-detected callback.
  var detectorStub = function(names, signatures){
    return function(){
      var chain = function(){ return this; };
      var Detector = function(){};
      Detector.prototype = {
        check: chain, clearEvent: chain, emitEvent: chain, onDetected: chain,
        setOption: chain, options: { set: chain, get: function(){} },
        on: function(detected, fn){
          if (!detected && typeof fn === 'function'){ try { fn(); } catch(e){} }
          return this;
        },
        onNotDetected: function(fn){
          if (typeof fn === 'function'){ try { fn(); } catch(e){} }
          return this;
        }
      };
      var instance = new Detector();
      names.forEach(function(n){
        var v = n.charAt(0) === n.charAt(0).toUpperCase() ? Detector : instance;
        try {
          Object.defineProperty(self, n, { configurable: true,
            get: function(){ return v; }, set: function(){} });
        } catch(e){}
      });
      // Inlined, obfuscated copies never touch those names; they are caught
      // by their tell-tale strings as they are scheduled or eval'd.
      if (signatures){
        var re = new RegExp(signatures, 'i');
        var hit = function(code){
          return typeof code !== 'undefined' && re.test(textOf(code));
        };
        patchMethod(self, 'setTimeout', function(target, thisArg, args){
          if (hit(args[0])){
            args = Array.prototype.slice.call(args);
            args[0] = function(){};
          }
          return Reflect.apply(target, thisArg, args);
        });
        patchMethod(self, 'eval', function(target, thisArg, args){
          if (hit(args[0])) return undefined;
          return Reflect.apply(target, thisArg, args);
        });
      }
    };
  };

  // popads-dummy: pretend the PopAds loader already ran. popads.net: make
  // the loader die on the globals it needs.
  var popadsDummy = function(){
    ['PopAds', 'popns'].forEach(function(n){
      try { delete self[n]; } catch(e){}
      try { Object.defineProperty(self, n, { configurable: true, value: {} }); } catch(e){}
    });
  };
  var popadsNet = function(){
    ['_pop', 'PopAds', 'popns'].forEach(function(n){
      try {
        Object.defineProperty(self, n, { configurable: true,
          get: function(){ throw abortError(); },
          set: function(){ throw abortError(); } });
      } catch(e){}
    });
  };

  // nowebrtc: ad networks open peer connections to fingerprint the local
  // network and to tunnel ads past network filters.
  var noWebrtc = function(){
    var never = function(){ return new Promise(function(){}); };
    var noop = function(){};
    var Stub = function(){};
    Stub.prototype = {
      close: noop, addEventListener: noop, removeEventListener: noop,
      createDataChannel: function(){ return { close: noop, send: noop, addEventListener: noop }; },
      createOffer: never, createAnswer: never, setLocalDescription: never,
      setRemoteDescription: never, addIceCandidate: never, getStats: never
    };
    ['RTCPeerConnection', 'webkitRTCPeerConnection'].forEach(function(n){
      if (typeof self[n] !== 'function') return;
      try { self[n] = new Proxy(self[n], { construct: function(){ return new Stub(); } }); } catch(e){}
    });
  };

  // set-cookie / set-local-storage-item: record a consent or dismissal flag.
  // Values are limited to flag-like ones, as in uBO -- a list must not be
  // able to plant an arbitrary identifier in a site's storage.
  var safeValue = function(raw){
    var v = raw === undefined ? '' : String(raw);
    var flags = ['', 'true', 'false', 'yes', 'y', 'no', 'n', 'ok', 'on', 'off',
      'accept', 'accepted', 'reject', 'rejected', 'allow', 'allowed', 'deny',
      'denied', 'checked', 'unchecked', 'dismiss', 'dismissed', 'hide', 'hidden',
      'done', 'necessary', 'required', 'essential', 'nonessential', 'approved',
      'disapproved', 'enable', 'enabled', 'disable', 'disabled', 'null', 'undefined'];
    if (flags.indexOf(v.toLowerCase()) !== -1) return v;
    if (/^-?\d{1,5}$/.test(v) && Math.abs(parseInt(v, 10)) <= 32767) return v;
    return null;
  };
  var setCookie = function(name, value, path){
    if (!name || !/^[\w.\-]{1,64}$/.test(name)) return;
    var v = safeValue(value);
    if (v === null) return;
    var pair = name + '=' + encodeURIComponent(v);
    try {
      if (String(document.cookie).split(/;\s*/).indexOf(pair) !== -1) return;
      document.cookie = pair + (path === 'none' ? '' : '; path=/');
    } catch(e){}
  };
  var setStorageItem = function(which){
    return function(key, value){
      if (!key) return;
      var storage;
      try { storage = self[which]; } catch(e){ return; }
      if (!storage) return;
      try {
        if (value === '$remove$'){ storage.removeItem(key); return; }
        var v = value === 'emptyArr' ? '[]' : value === 'emptyObj' ? '{}' : safeValue(value);
        if (v !== null) storage.setItem(key, v);
      } catch(e){}
    };
  };
  // trusted-set-cookie / trusted-set-local-storage-item: the consent
  // lists record "the user already answered" in the exact format each CMP
  // expects, so the value is free-form. Bounded instead of allowlisted: no
  // ';' (it would start a second attribute), a length cap, a plain name, and
  // an existing value is never overwritten -- so a list can pre-answer a
  // banner but cannot replace an identifier the site already set.
  var expandToken = function(v){
    if (v === '$now$') return String(Date.now());
    if (v === '$currentDate$') return new Date().toUTCString();
    if (v === '$currentISODate$') return new Date().toISOString();
    return v;
  };
  var trustedSetCookie = function(name, value, offsetRaw, path){
    if (!name || !/^[\w.\-%]{1,128}$/.test(name) || value === undefined) return;
    var v = expandToken(String(value));
    if (v.length > 1024 || /[;\r\n]/.test(v)) return;
    try {
      var jar = String(document.cookie).split(/;\s*/);
      for (var i = 0; i < jar.length; i++){
        if (jar[i].split('=')[0] === name) return;
      }
      var cookie = name + '=' + v + (path === 'none' ? '' : '; path=/');
      var offset = String(offsetRaw || '');
      var seconds = offset === '1day' ? 86400 : offset === '1year' ? 31536000
          : parseInt(offset, 10);
      if (seconds > 0 && seconds <= 400 * 86400){
        cookie += '; expires=' + new Date(Date.now() + seconds * 1000).toUTCString();
      }
      document.cookie = cookie;
    } catch(e){}
  };
  var trustedSetStorageItem = function(which){
    return function(key, value){
      if (!key || String(key).length > 256 || value === undefined) return;
      var storage;
      try { storage = self[which]; } catch(e){ return; }
      if (!storage) return;
      try {
        if (value === '$remove$'){ storage.removeItem(key); return; }
        var v = expandToken(String(value));
        if (v.length > 4096 || storage.getItem(key) !== null) return;
        storage.setItem(key, v);
      } catch(e){}
    };
  };
  // cookie-remover(needle): deletes matching cookies now and after load.
  var cookieRemover = function(needleRaw){
    if (!needleRaw) return;
    var matches = makeMatcher(needleRaw);
    var expire = function(){
      var names;
      try { names = String(document.cookie).split(/;\s*/); } catch(e){ return; }
      var host = location.hostname;
      names.forEach(function(pair){
        var name = pair.split('=')[0];
        if (!name || !matches(name)) return;
        var dead = name + '=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=/';
        try {
          document.cookie = dead;
          document.cookie = dead + '; domain=' + host;
          document.cookie = dead + '; domain=.' + host.split('.').slice(-2).join('.');
        } catch(e){}
      });
    };
    expire();
    listen(self, 'load', expire, { once: true });
  };

  var table = {
    'json-prune': jsonPrune,
    'jp': jsonPrune,
    'json-prune-fetch-response': jsonPruneFetchResponse,
    'json-prune-xhr-response': jsonPruneXhrResponse,
    'set-constant': setConstant,
    'set': setConstant,
    'abort-on-property-read': abortOnPropertyRead,
    'aopr': abortOnPropertyRead,
    'trusted-replace-fetch-response': trustedReplaceFetchResponse,
    'trusted-replace-xhr-response': trustedReplaceXhrResponse,
    // uBO's short names for the two above.
    'trusted-rpfr': trustedReplaceFetchResponse,
    'trusted-rpxr': trustedReplaceXhrResponse,
    'trusted-edit-inbound-object': trustedEditInboundObject,
    'trusted-prevent-dom-bypass': trustedPreventDomBypass,
    'nano-setTimeout-booster': nanoBooster('setTimeout'),
    'nano-stb': nanoBooster('setTimeout'),
    'nano-setInterval-booster': nanoBooster('setInterval'),
    'nano-sib': nanoBooster('setInterval'),
    'trusted-set-constant': setConstant,
    'trusted-set': setConstant,
    'abort-current-script': abortCurrentScript,
    'abort-current-inline-script': abortCurrentScript,
    'acs': abortCurrentScript,
    'acis': abortCurrentScript,
    'abort-on-property-write': abortOnPropertyWrite,
    'aopw': abortOnPropertyWrite,
    'abort-on-stack-trace': abortOnStackTrace,
    'aost': abortOnStackTrace,
    'no-setTimeout-if': preventTimer('setTimeout'),
    'prevent-setTimeout': preventTimer('setTimeout'),
    'nostif': preventTimer('setTimeout'),
    'no-setInterval-if': preventTimer('setInterval'),
    'prevent-setInterval': preventTimer('setInterval'),
    'nosiif': preventTimer('setInterval'),
    'no-requestAnimationFrame-if': preventTimer('requestAnimationFrame'),
    'norafif': preventTimer('requestAnimationFrame'),
    'addEventListener-defuser': preventAddEventListener,
    'prevent-addEventListener': preventAddEventListener,
    'aeld': preventAddEventListener,
    'no-window-open-if': preventWindowOpen,
    'prevent-window-open': preventWindowOpen,
    'window.open-defuser': preventWindowOpen,
    'nowoif': preventWindowOpen,
    'no-fetch-if': preventFetch,
    'prevent-fetch': preventFetch,
    'remove-attr': removeAttr,
    'ra': removeAttr,
    'remove-class': removeClass,
    'rc': removeClass,
    'noeval': function(){ noEvalIf(''); },
    'noeval-if': noEvalIf,
    'prevent-eval-if': noEvalIf,
    'bab-defuser': detectorStub(['BlockAdBlock', 'blockAdBlock'], 'blockadblock|babasbm'),
    'nobab': detectorStub(['BlockAdBlock', 'blockAdBlock'], 'blockadblock|babasbm'),
    'fuckadblock.js-3.2.0': detectorStub(['FuckAdBlock', 'fuckAdBlock', 'BlockAdBlock', 'blockAdBlock']),
    'nofab': detectorStub(['FuckAdBlock', 'fuckAdBlock', 'BlockAdBlock', 'blockAdBlock']),
    'popads-dummy': popadsDummy,
    'popads.net': popadsNet,
    'nowebrtc': noWebrtc,
    'set-cookie': setCookie,
    'set-local-storage-item': setStorageItem('localStorage'),
    'set-session-storage-item': setStorageItem('sessionStorage'),
    'cookie-remover': cookieRemover,
    'remove-cookie': cookieRemover,
    'trusted-set-cookie': trustedSetCookie,
    'trusted-set-local-storage-item': trustedSetStorageItem('localStorage'),
    'trusted-set-session-storage-item': trustedSetStorageItem('sessionStorage'),
    'trusted-click-element': trustedClickElement,
    // 'trusted-json-edit-xhr-request' is implemented above (jsonEditXhrRequest)
    // but deliberately NOT registered, for a measured reason:
    //
    // Instrumented run on a real watch page logged the hook INSTALLING 3 times
    // (once per YouTube rule) and FIRING zero times — YouTube requests /player
    // via fetch(), not XMLHttpRequest, so the edit code never runs. Meanwhile
    // playback broke: with the scriptlet registered readyState stays 0 and the
    // video element sometimes never appears; without it playback reaches
    // readyState 4 and runs normally (same video, identical timing).
    //
    // So the harm comes from PATCHING XMLHttpRequest.prototype.open/send at
    // all, not from any edit. Overwriting those makes
    // XMLHttpRequest.prototype.open.toString() stop reporting [native code],
    // which is a standard blocker-detection probe; YouTube's answer is to
    // withhold playable streams. Registering this can only cost playback and
    // cannot gain anything here. If it is ever needed, port it to the fetch
    // path (see hookFetch above) and mask the patch's toString.
    'no-xhr-if': noXhrIf,
    'rmnt': removeNodeText,
    'remove-node-text': removeNodeText,
    'trusted-rpnt': replaceNodeText,
    'rpnt': replaceNodeText,
    'trusted-replace-node-text': replaceNodeText,
    'noop': function(){},
  };
  return table;
})();
)JS";

// Strips one balanced pair of surrounding single/double quotes, matching uBO's
// argument parser (quotes are used to protect args containing commas/spaces).
std::string StripOuterQuotes(std::string_view s) {
  if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') &&
      s.back() == s.front()) {
    return std::string(s.substr(1, s.size() - 2));
  }
  return std::string(s);
}

// Splits "+js(...)" argument text on commas, honoring "\," escapes, then trims
// whitespace and strips protective outer quotes from each argument.
std::vector<std::string> SplitArgs(std::string_view text) {
  std::vector<std::string> args;
  std::string current;
  auto flush = [&args](std::string_view raw) {
    args.push_back(StripOuterQuotes(
        base::TrimWhitespaceASCII(raw, base::TRIM_ALL)));
  };
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == ',') {
      current.push_back(',');
      ++i;
      continue;
    }
    if (text[i] == ',') {
      flush(current);
      current.clear();
      continue;
    }
    current.push_back(text[i]);
  }
  flush(current);
  return args;
}

}  // namespace

AdblockScriptletEngine::AdblockScriptletEngine() = default;
AdblockScriptletEngine::~AdblockScriptletEngine() = default;

size_t AdblockScriptletEngine::AddRules(std::string_view filter_list_text,
                                        bool trust_unsectioned) {
  size_t added = 0;
  // Trust of the current section: see the header. Starts as the caller says
  // for text before any marker.
  bool section_trusted = trust_unsectioned;
  for (std::string_view raw :
       base::SplitStringPiece(filter_list_text, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (raw.starts_with("! ===== ")) {
      section_trusted = IsTrustedScriptletSectionMarker(raw);
      continue;
    }
    if (raw[0] == '!' || raw[0] == '[') {
      continue;
    }
    bool exception = false;
    size_t sep = raw.find("##+js(");
    size_t body_start = 0;
    if (sep != std::string_view::npos) {
      body_start = sep + 6;
    } else if (size_t e = raw.find("#@#+js("); e != std::string_view::npos) {
      exception = true;
      sep = e;
      body_start = e + 7;
    } else {
      continue;
    }
    if (raw.back() != ')') {
      continue;
    }
    std::string_view domains = raw.substr(0, sep);
    std::string_view body =
        raw.substr(body_start, raw.size() - body_start - 1);  // inside (...)
    std::vector<std::string> parts = SplitArgs(body);
    // "site#@#+js()" -- an exception naming no scriptlet -- switches every
    // scriptlet off on that site. Parsing it as an empty name dropped it, and
    // the scriptlets it exists to stop kept breaking the site.
    const bool disable_all = exception && (parts.empty() || parts[0].empty());
    if (!disable_all && (parts.empty() || parts[0].empty())) {
      continue;
    }
    Invocation inv;
    if (!disable_all) {
      inv.name = parts[0];
      // uBO accepts the resource file name too: "set-constant.js".
      if (inv.name.ends_with(".js") && inv.name != "fuckadblock.js-3.2.0") {
        inv.name.resize(inv.name.size() - 3);
      }
      // A trusted scriptlet from a list not allowed one. Checked after the
      // ".js" strip so "trusted-set-cookie.js" cannot slip past. Exceptions
      // are not gated: they only switch scriptlets OFF.
      if (!exception && !section_trusted && inv.name.starts_with("trusted-")) {
        ++untrusted_scriptlets_dropped_;
        continue;
      }
      inv.args.assign(parts.begin() + 1, parts.end());
    }

    std::vector<std::string> included;
    for (std::string_view d :
         base::SplitStringPiece(domains, ",", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      if (d[0] == '~') {
        if (d.size() > 1) {
          inv.excluded.push_back(base::ToLowerASCII(d.substr(1)));
        }
      } else {
        included.push_back(base::ToLowerASCII(d));
      }
    }
    if (included.empty()) {
      // Generic scriptlets (with or without carve-outs) are uncommon and
      // risky: one runs on every site on the web. Skipped.
      continue;
    }
    for (const std::string& domain : included) {
      if (disable_all) {
        domain_disable_all_.insert(domain);
      } else if (exception) {
        domain_exceptions_[domain].insert(inv.name);
      } else {
        domain_scriptlets_[domain].push_back(inv);
      }
    }
    ++rule_count_;
    ++added;
  }
  return added;
}

std::string AdblockScriptletEngine::BuildInjectionScriptForUrl(
    const GURL& url) const {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }
  const std::string host(url.host());

  // Exceptions (by scriptlet name) that apply to this host. The keys include
  // the entity forms ("example.*"), which the lists use for sites spread
  // across many country domains.
  std::unordered_set<std::string> exceptions;
  std::vector<const Invocation*> invocations;
  for (const std::string& key : DomainLookupKeys(host)) {
    if (domain_disable_all_.contains(key)) {
      return std::string();
    }
    auto ex = domain_exceptions_.find(key);
    if (ex != domain_exceptions_.end()) {
      exceptions.insert(ex->second.begin(), ex->second.end());
    }
    auto it = domain_scriptlets_.find(key);
    if (it != domain_scriptlets_.end()) {
      for (const Invocation& inv : it->second) {
        invocations.push_back(&inv);
      }
    }
  }
  if (invocations.empty()) {
    return std::string();
  }

  std::string calls;
  std::unordered_set<std::string> emitted_calls;
  size_t emitted = 0;
  for (const Invocation* inv_ptr : invocations) {
    const Invocation& inv = *inv_ptr;
    if (exceptions.contains(inv.name)) {
      continue;
    }
    // "a.com,~shop.a.com##+js(...)": not on the carved-out host.
    bool carved_out = false;
    for (const std::string& domain : inv.excluded) {
      if (HostMatchesFilterDomain(host, domain)) {
        carved_out = true;
        break;
      }
    }
    if (carved_out) {
      continue;
    }
    std::string args;
    for (size_t i = 0; i < inv.args.size(); ++i) {
      if (i) {
        args += ',';
      }
      std::string quoted;
      base::EscapeJSONString(inv.args[i], /*put_in_quotes=*/true, &quoted);
      args += quoted;
    }
    std::string name_quoted;
    base::EscapeJSONString(inv.name, /*put_in_quotes=*/true, &name_quoted);
    std::string call = base::StrCat({"try{var f=zephyrusScriptlets[",
                                     name_quoted, "];if(f)f(", args,
                                     ");}catch(e){}\n"});
    // The same rule is often filed under a domain AND its entity form, and
    // most scriptlets are not idempotent: nostif twice wraps setTimeout twice.
    if (!emitted_calls.insert(call).second) {
      continue;
    }
    calls += call;
    ++emitted;
  }
  if (emitted == 0) {
    return std::string();
  }
  // Everything runs inside an IIFE so nothing (not even `zephyrusScriptlets`)
  // leaks onto `window` — the page can't detect the blocker by probing globals,
  // and each scriptlet is individually idempotent/try-caught so a rare repeat
  // injection is harmless.
  return base::StrCat({"(function(){\n", kScriptletLibrary, "\n", calls,
                       "})();"});
}

}  // namespace zephyrus_adblock

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/adblock/adblock_scriptlet_engine.h"

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace zephyrus_adblock {

namespace {

// A small, self-contained library of uBlock-Origin-compatible scriptlets. Kept
// intentionally focused (the YouTube-critical ones + a couple of common
// defusers); more can be ported over time. Exposed to the injection payload as
// `zephyrusScriptlets[name](...args)`.
constexpr char kScriptletLibrary[] = R"JS(
var zephyrusScriptlets = (function(){
  var toValue = function(raw){
    switch (raw) {
      case 'true': return true;
      case 'false': return false;
      case 'null': return null;
      case 'undefined': return undefined;
      case "''": return '';
      case '[]': return [];
      case '{}': return {};
      case 'noopFunc': return function(){};
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
    var parts = path.split('.');
    var install = function(owner, i){
      if (owner == null) return;
      var prop = parts[i];
      if (i === parts.length - 1){
        try {
          Object.defineProperty(owner, prop, {
            get: function(){ throw new ReferenceError(prop); },
            set: function(){}, configurable: false
          });
        } catch(e){}
        return;
      }
      var existing = owner[prop];
      if (existing && typeof existing === 'object') install(existing, i + 1);
    };
    try { install(self, 0); } catch(e){}
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
  // nano-setTimeout-booster: rescales matching setTimeout delays (used to
  // collapse ad-countdown timers). Args: needle (substring or /re/ matched
  // against the stringified callback), delay (exact ms, '*' = any), boost.
  var nanoSetTimeoutBooster = function(needleRaw, delayRaw, boostRaw){
    var reNeedle = null;
    if (needleRaw){
      var m = /^\/(.+)\/(\w*)$/.exec(needleRaw);
      try { reNeedle = m ? new RegExp(m[1], m[2])
              : new RegExp(String(needleRaw).replace(/[.*+?^${}()|[\]\\]/g, '\\$&')); }
      catch(e){}
    }
    var delay = delayRaw === '*' ? -1 : parseInt(delayRaw, 10);
    if (isNaN(delay)) delay = 1000;
    var boost = parseFloat(boostRaw);
    if (isNaN(boost) || boost < 0.001 || boost > 50) boost = 0.05;
    var realST = self.setTimeout;
    if (typeof realST !== 'function') return;
    self.setTimeout = function(fn, ms){
      var ok = true;
      if (reNeedle){ try { ok = reNeedle.test(String(fn)); } catch(e){ ok = false; } }
      if (ok && (delay === -1 || ms === delay)){
        arguments[1] = ms * boost;
      }
      return realST.apply(self, arguments);
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
  var replaceNodeText = function(rawSelector, rawPattern, rawReplacement){
    // A selector is required. Defaulting to '*' would run querySelectorAll('*')
    // on every DOM mutation, which on a page like YouTube is ruinous.
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
    var apply = function(){
      var nodes;
      try { nodes = document.querySelectorAll(selector); } catch(e){ return; }
      for (var i = 0; i < nodes.length; i++){
        var node = nodes[i];
        var text = node.textContent;
        if (!text) continue;
        var next = re ? text.replace(re, replacement)
                      : text.split(pattern).join(replacement);
        if (next === text) continue;
        // Pages with Trusted Types (YouTube among them) throw when script text
        // is assigned. Swallow it: an uncaught error here both spams the
        // console and is itself a signal a blocker is present.
        try {
          node.textContent = next;
        } catch(e){}
      }
    };
    apply();
    // Coalesce mutation bursts; re-scanning the document per mutation is far
    // too expensive on script-heavy pages.
    var pending = false;
    var schedule = function(){
      if (pending) return;
      pending = true;
      setTimeout(function(){ pending = false; apply(); }, 100);
    };
    try {
      new MutationObserver(schedule).observe(document.documentElement || document,
                                             { childList: true, subtree: true });
    } catch(e){}
  };


  // trusted-json-edit-xhr-request(edit, propsToMatch, <urlPattern>)
  // Rewrites the JSON body of an outgoing XHR. YouTube's anti-adblock fixes use
  // this on the /player request (spoofing clientScreen, stamping
  // lactMilliseconds, tagging the referer) so the response returns without ad
  // payloads.
  //
  // uBO expresses the edit in a small JSONPath-like DSL. This implements the
  // subset the live YouTube rules actually use:
  //   [?<path><op><val>]   leading guard - apply only if it holds
  //   ..name               recursive descent to every key of that name
  //   .name                child access
  //   [?.name==<v>]        predicate on the current node
  //   ops: ==   *= (contains)   =/regex/
  //   actions: = <value>  assign     += {json}  shallow-merge
  //   values: "text", ${now}, repl({"regex":..,"replacement":..})
  // Anything outside this subset is ignored rather than half-applied: a
  // malformed player request breaks playback, which is worse than an ad.
  var jsonEditXhrRequest = function(editSpec, propName, propValue){
    if (!editSpec) return;
    var urlNeedle = '';
    if (propName === 'propsToMatch' && propValue) urlNeedle = String(propValue);

    var parseValue = function(raw){
      raw = String(raw).trim();
      if (raw.indexOf('repl(') === 0 && raw.charAt(raw.length - 1) === ')'){
        try {
          var spec = JSON.parse(raw.slice(5, -1));
          return { kind: 'repl', regex: spec.regex,
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
      if ((q === '"' || q === "'") && raw.charAt(raw.length - 1) === q){
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
        if (!Object.prototype.hasOwnProperty.call(node, k)) continue;
        if (k === name) out.push({ parent: node, key: k, value: node[k] });
        descend(node[k], name, out);
      }
    };

    var compare = function(actual, op, expected){
      if (actual === undefined || actual === null) return false;
      var a = String(actual);
      if (op === '==') return a === String(expected);
      if (op === '*=') return a.indexOf(String(expected)) !== -1;
      if (op === '=~'){
        var body = String(expected);
        if (body.charAt(0) !== '/') return false;
        var end = body.lastIndexOf('/');
        try {
          return new RegExp(body.slice(1, end), body.slice(end + 1)).test(a);
        } catch(e){ return false; }
      }
      return false;
    };

    var parsePredicate = function(text){
      var m = text.match(/^\[\?(\.{1,2})([A-Za-z0-9_$]+)(==|\*=|=)([\s\S]*)\]$/);
      if (!m) return null;
      var recursive = m[1] === '..';
      var name = m[2];
      var op = m[3];
      var rawVal = m[4].trim();
      if (op === '=' && rawVal.charAt(0) === '/') op = '=~';
      var expected = (op === '=~') ? rawVal : (parseValue(rawVal) || {}).value;
      return function(node){
        var hits = [];
        if (recursive){
          descend(node, name, hits);
        } else if (node && typeof node === 'object' &&
                   Object.prototype.hasOwnProperty.call(node, name)){
          hits.push({ parent: node, key: name, value: node[name] });
        }
        for (var i = 0; i < hits.length; i++){
          if (compare(hits[i].value, op, expected)) return true;
        }
        return false;
      };
    };

    var spec = String(editSpec).trim();
    var guard = null;
    if (spec.charAt(0) === '['){
      var close = spec.indexOf(']');
      if (close === -1) return;
      guard = parsePredicate(spec.slice(0, close + 1));
      spec = spec.slice(close + 1);
    }

    // Find the action operator OUTSIDE any predicate, so "==" inside [?...]
    // is not mistaken for the assignment.
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
    if (opIndex === -1) return;
    var pathText = spec.slice(0, opIndex);
    var valueText = spec.slice(opIndex + (action === 'merge' ? 2 : 1));
    var parsedValue = parseValue(valueText);
    if (!parsedValue) return;

    var tokens = [];
    var re = /(\.{1,2})([A-Za-z0-9_$]+)(\[\?[^\]]*\])?/g;
    var tm;
    while ((tm = re.exec(pathText)) !== null){
      tokens.push({ recursive: tm[1] === '..', name: tm[2],
                    filter: tm[3] ? parsePredicate(tm[3]) : null });
    }
    if (!tokens.length) return;

    var applyEdit = function(root){
      if (guard && !guard(root)) return false;
      var current = [{ parent: null, key: null, value: root }];
      for (var t = 0; t < tokens.length; t++){
        var token = tokens[t];
        var next = [];
        for (var c = 0; c < current.length; c++){
          var node = current[c].value;
          var hits = [];
          if (token.recursive){
            descend(node, token.name, hits);
          } else if (node && typeof node === 'object' &&
                     Object.prototype.hasOwnProperty.call(node, token.name)){
            hits.push({ parent: node, key: token.name,
                        value: node[token.name] });
          }
          for (var h = 0; h < hits.length; h++){
            if (token.filter && !token.filter(hits[h].value)) continue;
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
          if (parsedValue.kind !== 'json' || !target.value ||
              typeof target.value !== 'object') continue;
          for (var k2 in parsedValue.value){
            if (Object.prototype.hasOwnProperty.call(parsedValue.value, k2)){
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
          try {
            newValue = text.replace(new RegExp(parsedValue.regex),
                                    parsedValue.replacement);
          } catch(e){ continue; }
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
          if (applyEdit(parsed)) arguments[0] = JSON.stringify(parsed);
        }
      } catch(e){
        // Never let an edit failure break the request: a broken /player call is
        // far more visible - and more detectable - than an unblocked ad.
      }
      return realSend.apply(this, arguments);
    };
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
    'trusted-prevent-dom-bypass': trustedPreventDomBypass,
    'nano-setTimeout-booster': nanoSetTimeoutBooster,
    'nano-stb': nanoSetTimeoutBooster,
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
    'rmnt': replaceNodeText,
    'remove-node-text': replaceNodeText,
    'trusted-rpnt': replaceNodeText,
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

size_t AdblockScriptletEngine::AddRules(std::string_view filter_list_text) {
  size_t added = 0;
  for (std::string_view raw :
       base::SplitStringPiece(filter_list_text, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
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
    if (parts.empty() || parts[0].empty()) {
      continue;
    }
    Invocation inv;
    inv.name = parts[0];
    inv.args.assign(parts.begin() + 1, parts.end());

    if (domains.empty()) {
      // Generic scriptlets are uncommon and risky; skip for now.
      continue;
    }
    for (std::string_view d :
         base::SplitStringPiece(domains, ",", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      if (d[0] == '~') {
        continue;
      }
      std::string domain = base::ToLowerASCII(d);
      if (exception) {
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

  // Exceptions (by scriptlet name) that apply to this host.
  std::unordered_set<std::string> exceptions;
  std::vector<Invocation> invocations;
  for (size_t pos = 0; pos != std::string::npos;) {
    std::string candidate = host.substr(pos);
    auto ex = domain_exceptions_.find(candidate);
    if (ex != domain_exceptions_.end()) {
      exceptions.insert(ex->second.begin(), ex->second.end());
    }
    auto it = domain_scriptlets_.find(candidate);
    if (it != domain_scriptlets_.end()) {
      invocations.insert(invocations.end(), it->second.begin(),
                         it->second.end());
    }
    size_t dot = host.find('.', pos);
    pos = (dot == std::string::npos) ? std::string::npos : dot + 1;
  }
  if (invocations.empty()) {
    return std::string();
  }

  std::string calls;
  size_t emitted = 0;
  for (const Invocation& inv : invocations) {
    if (exceptions.contains(inv.name)) {
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
    calls += base::StrCat({"try{var f=zephyrusScriptlets[", name_quoted,
                           "];if(f)f(", args, ");}catch(e){}\n"});
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

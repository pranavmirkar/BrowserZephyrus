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

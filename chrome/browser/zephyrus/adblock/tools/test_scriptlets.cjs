// Runs the scriptlet library (read out of adblock_scriptlet_engine.cc) in a bare
// context and checks the scriptlets that have no other test.
//   node test_scriptlets.cjs
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const cc = fs.readFileSync(path.join(__dirname, '..', 'adblock_scriptlet_engine.cc'), 'utf8');
const lib = cc.slice(cc.indexOf('constexpr char kScriptletLibrary[] = R"JS(') + 'constexpr char kScriptletLibrary[] = R"JS('.length,
                     cc.indexOf(')JS";', cc.indexOf('kScriptletLibrary')));
let passed = 0, failures = 0;
function test(name, fn) {
  try { fn(); passed++; } catch (e) { failures++; console.log('FAIL', name, '\n    ', String(e.message).slice(0, 2000)); }
}

// A fresh page: its own JSON, so a patched stringify cannot leak between tests.
function page() {
  const ctx = vm.createContext({console});
  vm.runInContext('var self = this; var window = this; var JSON = this.JSON;', ctx);
  vm.runInContext(lib + '\nthis.__lib = zephyrusScriptlets;', ctx);
  return ctx;
}

// The /player request YouTube's web player sends (the parts the rules look at).
function playerRequest(userAgent) {
  return {
    attestationRequest: {omitBotguardData: true},
    context: {client: {clientName: 'WEB', clientVersion: '2.2026', userAgent: userAgent || 'Mozilla/5.0 x'}},
    playbackContext: {contentPlaybackContext: {referer: 'https://www.youtube.com/', lactMilliseconds: '-1'}},
    videoId: 'abc',
  };
}

const RULES = {
  channel: '[?.attestationRequest][?.context.client.userAgent*="channel"].context.client[?.clientName=="WEB"]+={"clientScreen":"CHANNEL"}',
  inline: '[?.attestationRequest][?.context.client.userAgent*="inline"].playbackContext.contentPlaybackContext+={"isInlinePlaybackNoAd":true}',
  lact: '[?.attestationRequest][?.context.client.userAgent=/channel|lactmilli|inline/].playbackContext.contentPlaybackContext.lactMilliseconds="${now}"',
  referer: '[?.attestationRequest][?.context.client.userAgent=/adunit|channel|lactmilli|inline|yahi|eafg|yaf4/].playbackContext.contentPlaybackContext.referer=repl({"regex":"(?:#reloadxhr)?$","replacement":"#reloadxhr"})',
  comment: '[?.createCommentParams].context.client.userAgent=repl({"regex":"/; (adunit|channel|lactmilli|inline|yahi|eafg|yaf4)\\b/","replacement":""})',
};

test('the guarded merge edits the serialised copy, never the page\'s object', () => {
  const ctx = page();
  ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', RULES.channel);
  const body = playerRequest('Mozilla/5.0 x; channel');
  ctx.body = body;
  const out = JSON.parse(vm.runInContext('JSON.stringify(body)', ctx));
  assert.strictEqual(out.context.client.clientScreen, 'CHANNEL');
  assert.strictEqual(body.context.client.clientScreen, undefined, 'the page object was changed');
});

test('a guard that does not hold leaves the request alone', () => {
  const ctx = page();
  ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', RULES.channel);
  ctx.body = playerRequest('Mozilla/5.0 plain');
  const out = JSON.parse(vm.runInContext('JSON.stringify(body)', ctx));
  assert.strictEqual(out.context.client.clientScreen, undefined);
  // No attestationRequest: not a player request at all.
  ctx.other = {context: {client: {clientName: 'WEB', userAgent: 'x; channel'}}};
  const out2 = JSON.parse(vm.runInContext('JSON.stringify(other)', ctx));
  assert.strictEqual(out2.context.client.clientScreen, undefined);
});

test('a filter on the path ([?.clientName=="WEB"]) is honoured', () => {
  const ctx = page();
  ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', RULES.channel);
  const body = playerRequest('x; channel');
  body.context.client.clientName = 'MWEB';
  ctx.body = body;
  assert.strictEqual(JSON.parse(vm.runInContext('JSON.stringify(body)', ctx)).context.client.clientScreen, undefined);
});

test('every live YouTube rule compiles and does what it says', () => {
  const ctx = page();
  for (const k of ['inline', 'lact', 'referer']) ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', RULES[k]);
  ctx.body = playerRequest('Mozilla/5.0 x; inline');
  const before = Date.now();
  const out = JSON.parse(vm.runInContext('JSON.stringify(body)', ctx));
  const c = out.playbackContext.contentPlaybackContext;
  assert.strictEqual(c.isInlinePlaybackNoAd, true);
  assert.ok(Number(c.lactMilliseconds) >= before, 'lactMilliseconds not stamped: ' + c.lactMilliseconds);
  assert.strictEqual(c.referer, 'https://www.youtube.com/#reloadxhr');
  // Twice is the same: the referer is tagged once, not twice.
  const again = JSON.parse(vm.runInContext('JSON.stringify(body)', ctx));
  assert.strictEqual(again.playbackContext.contentPlaybackContext.referer, 'https://www.youtube.com/#reloadxhr');
});

test('a regex written as "/.../" inside repl() is a regex, not literal slashes', () => {
  const ctx = page();
  ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', RULES.comment);
  ctx.body = {createCommentParams: 'p', context: {client: {userAgent: 'Mozilla/5.0 x; channel'}}};
  assert.strictEqual(JSON.parse(vm.runInContext('JSON.stringify(body)', ctx)).context.client.userAgent, 'Mozilla/5.0 x');
});

test('JSON.stringify still looks native and serialises everything else as before', () => {
  const ctx = page();
  ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', RULES.channel);
  assert.ok(/\[native code\]/.test(vm.runInContext('Function.prototype.toString.call(JSON.stringify)', ctx)));
  assert.strictEqual(vm.runInContext('JSON.stringify({a:[1,"x"]}, null, 0)', ctx), '{"a":[1,"x"]}');
  assert.strictEqual(vm.runInContext('JSON.stringify("s")', ctx), '"s"');
  assert.strictEqual(vm.runInContext('JSON.stringify(undefined)', ctx), undefined);
});

test('an edit outside the understood language does nothing at all', () => {
  const ctx = page();
  for (const bad of ['', '[?.a', '[?.a=nope].b=1', '.a+="not json"', 'a.b=1'])
    ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', bad);
  ctx.body = {a: {b: 0}};
  assert.strictEqual(vm.runInContext('JSON.stringify(body)', ctx), '{"a":{"b":0}}');
});

test('trusted-rpfr is the fetch response replacer under its short name', () => {
  const ctx = page();
  assert.strictEqual(ctx.__lib['trusted-rpfr'], ctx.__lib['trusted-replace-fetch-response']);
  assert.strictEqual(ctx.__lib['trusted-rpxr'], ctx.__lib['trusted-replace-xhr-response']);
});

// A document small enough to drive replace-node-text: script nodes with text,
// querySelectorAll('script'), and a mutation observer the test can fire.
function scriptPage(texts) {
  const ctx = page();
  const nodes = [];
  const make = (text) => ({nodeType: 1, nodeName: 'SCRIPT', textContent: text, firstElementChild: null,
                           matches: (sel) => sel === 'script'});
  for (const t of texts) nodes.push(make(t));
  let observerCb = null;
  ctx.document = {documentElement: {}, querySelectorAll: (sel) => (sel === 'script' ? nodes.slice() : [])};
  ctx.MutationObserver = function(cb) { observerCb = cb; this.observe = () => {}; this.disconnect = () => { observerCb = null; }; };
  // Captured by the library at load time, so load it again with these in place.
  vm.runInContext(lib + '\nthis.__lib = zephyrusScriptlets;', ctx);
  ctx.__fire = (records) => { if (observerCb) observerCb(records); };
  return {ctx, nodes, add: (text) => { const n = make(text); nodes.push(n); if (observerCb) observerCb([{addedNodes: [n]}]); return n; }};
}

test('rpnt edits only the script its condition names, once, and skips excluded ones', () => {
  const {ctx, nodes, add} = scriptPage(['if (window.ytcsi) { early(); }']);
  ctx.__lib['rpnt']('script', '/(\\(function serverContract\\(\\)|^if \\(window\\.ytcsi)/', 'FIX;$1',
                    'sedCount', '1', 'excludes', 'MutationObserver', 'condition', '/serverContract|js_ld/');
  // The early script matches the pattern but not the condition: untouched.
  assert.strictEqual(nodes[0].textContent, 'if (window.ytcsi) { early(); }');
  const excluded = add('(function serverContract(){ new MutationObserver(); })');
  assert.ok(!excluded.textContent.startsWith('FIX;'), 'an excluded script was edited');
  const target = add('(function serverContract(){ go(); })()');
  assert.strictEqual(target.textContent, 'FIX;(function serverContract(){ go(); })()');
  const second = add('(function serverContract(){ again(); })()');
  assert.ok(!second.textContent.startsWith('FIX;'), 'sedCount 1 was exceeded');
});

test('rpnt edits a script whose text arrives after the element, as a streamed page delivers it', () => {
  const {ctx, add} = scriptPage([]);
  ctx.__lib['rpnt']('script', '/(\\(function serverContract\\(\\)|^if \\(window\\.ytcsi)/', 'FIX;$1',
                    'sedCount', '1', 'excludes', 'MutationObserver', 'condition', '/serverContract|js_ld/');
  // The parser inserts the element empty...
  const el = add('');
  assert.strictEqual(el.textContent, '');
  // ...and its text in a later task: the record names the text node only.
  el.textContent = '(function serverContract(){ go(); })()';
  ctx.__fire([{addedNodes: [{nodeType: 3, nodeName: '#text', parentNode: el}]}]);
  assert.strictEqual(el.textContent, 'FIX;(function serverContract(){ go(); })()');
});

test('rpnt without options edits every matching script as it is added', () => {
  const {ctx, nodes, add} = scriptPage(['var a = "ad";']);
  ctx.__lib['rpnt']('script', 'ad', 'no');
  assert.strictEqual(nodes[0].textContent, 'var a = "no";');
  assert.strictEqual(add('var b = "ad";').textContent, 'var b = "no";');
});

test('rmnt empties a whole matching script and leaves the others', () => {
  const {ctx, nodes, add} = scriptPage(['window.x = 1;', 'setup(window,"fetch"); more();']);
  ctx.__lib['rmnt']('script', 'window,"fetch"');
  assert.strictEqual(nodes[0].textContent, 'window.x = 1;');
  assert.strictEqual(nodes[1].textContent, '');
  assert.strictEqual(add('a(window,"fetch")').textContent, '');
  assert.strictEqual(ctx.__lib['remove-node-text'], ctx.__lib['rmnt']);
});

test('every patched built-in still stringifies as it did, and a request gets no own properties', () => {
  const ctx = page();
  // A stand-in XMLHttpRequest with the shape the hook looks for.
  vm.runInContext(`
    function XMLHttpRequest(){ this.readyState = 4; }
    XMLHttpRequest.prototype.open = function open(){};
    XMLHttpRequest.prototype.send = function send(){};
    Object.defineProperty(XMLHttpRequest.prototype, 'responseText', {configurable: true, get: function responseText(){ return '{"a":1}'; }});
    Object.defineProperty(XMLHttpRequest.prototype, 'response', {configurable: true, get: function response(){ return '{"a":1}'; }});
    this.XMLHttpRequest = XMLHttpRequest;
    this.fetch = function fetch(){ return Promise.resolve(null); };
    function Response(){} Response.prototype.json = function json(){ return Promise.resolve({}); };
    this.Response = Response;`, ctx);
  const before = vm.runInContext(`(() => { const t = (f) => Function.prototype.toString.call(f);
    return [t(JSON.parse), t(JSON.stringify), t(fetch), t(Response.prototype.json), t(XMLHttpRequest.prototype.open),
            t(XMLHttpRequest.prototype.send), t(Object.getOwnPropertyDescriptor(XMLHttpRequest.prototype, 'responseText').get)]; })()`, ctx);
  ctx.__lib['json-prune']('ads');
  ctx.__lib['json-prune-fetch-response']('ads', '', 'propsToMatch', 'x');
  ctx.__lib['trusted-replace-xhr-response']('a', 'b', 'x');
  ctx.__lib['json-prune-xhr-response']('ads', '', 'propsToMatch', 'x');
  ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', '[?.a].b=1');
  ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', '[?.a].c=2');
  ctx.__lib['json-prune']('more');
  const after = vm.runInContext(`(() => { const t = (f) => Function.prototype.toString.call(f);
    return [t(JSON.parse), t(JSON.stringify), t(fetch), t(Response.prototype.json), t(XMLHttpRequest.prototype.open),
            t(XMLHttpRequest.prototype.send), t(Object.getOwnPropertyDescriptor(XMLHttpRequest.prototype, 'responseText').get)]; })()`, ctx);
  assert.deepStrictEqual(after, before);
  const own = vm.runInContext(`(() => { const x = new XMLHttpRequest(); x.open('GET', 'https://x/'); x.send();
    void x.responseText; return Object.getOwnPropertyNames(x).filter((k) => k !== 'readyState'); })()`, ctx);
  assert.deepStrictEqual(Array.from(own), []);
  // And the hook still edits the response it was asked to.
  assert.strictEqual(vm.runInContext(`(() => { const x = new XMLHttpRequest(); x.open('GET', 'https://x/'); x.send(); return x.responseText; })()`, ctx), '{"b":1}');
});

test('the defusers and toString itself keep their exact native text', () => {
  const ctx = page();
  vm.runInContext(`this.EventTarget = function EventTarget(){}; EventTarget.prototype.addEventListener = function addEventListener(){};
                   this.setTimeout = function setTimeout(){}; this.setInterval = function setInterval(){};`, ctx);
  const read = () => vm.runInContext(`[Function.prototype.toString.call(Function.prototype.toString),
      Function.prototype.toString.call(EventTarget.prototype.addEventListener), Function.prototype.toString.call(setTimeout),
      String(Function.prototype.toString), Function.prototype.toString.name, Function.prototype.toString.length]`, ctx);
  const before = read();
  ctx.__lib['aeld']('scroll');
  ctx.__lib['nostif']('adblock');
  assert.deepStrictEqual(read(), before);
});

test('a path with no operator removes what it names, arrays included', () => {
  const ctx = page();
  ctx.__lib['trusted-edit-inbound-object']('JSON.stringify', '0', '..node[?.__typename=="AdStory"]');
  ctx.body = {edges: [{node: {__typename: 'Story', id: 1}}, {node: {__typename: 'AdStory', id: 2}}]};
  assert.strictEqual(vm.runInContext('JSON.stringify(body)', ctx), '{"edges":[{"node":{"__typename":"Story","id":1}},{}]}');
});

test('json-edit removes ad nodes from what JSON.parse returns; the plain form cannot assign', () => {
  const ctx = page();
  ctx.__lib['json-edit']('..node[?.__typename=="MarketplaceFeedAdStory"]');
  ctx.__lib['json-edit']('.a=1');  // not allowed untrusted: ignored
  const out = vm.runInContext(`JSON.parse('{"a":0,"items":[{"node":{"__typename":"MarketplaceFeedAdStory"}},{"node":{"__typename":"Listing"}}]}')`, ctx);
  assert.strictEqual(JSON.stringify(out), '{"a":0,"items":[{},{"node":{"__typename":"Listing"}}]}');
  ctx.__lib['trusted-json-edit']('[?.type=="ads"].visibility.status="hidden"');
  const ad = vm.runInContext(`JSON.parse('{"type":"ads","visibility":{"status":"shown"}}')`, ctx);
  assert.strictEqual(ad.visibility.status, 'hidden');
});

test('trusted-replace-argument swaps an argument, or the receiver, when the condition holds', () => {
  const ctx = page();
  vm.runInContext(`this.api = { send: function send(a, b){ return a + '|' + b; } };`, ctx);
  ctx.__lib['trusted-replace-argument']('api.send', '1', 'json:{"blocked":false}', 'condition', 'adblock');
  assert.strictEqual(vm.runInContext(`api.send('x', 'adblock=1')`, ctx), 'x|[object Object]');
  assert.strictEqual(vm.runInContext(`api.send('x', 'other')`, ctx), 'x|other');
  ctx.__lib['trusted-replace-argument']('String.prototype.split', 'this', 'repl:/network_machine=true/network_machine=false/', 'condition', 'H5_');
  assert.strictEqual(vm.runInContext(`'H5_x&network_machine=true'.split('&').join('&')`, ctx), 'H5_x&network_machine=false');
  assert.strictEqual(vm.runInContext(`'a,b'.split(',').length`, ctx), 2);
});

test('set-cookie with reload 1 reloads once the cookie is really set, and not otherwise', () => {
  const ctx = page();
  let jar = '', reloads = 0;
  ctx.document = {};
  Object.defineProperty(ctx.document, 'cookie', {get: () => jar, set: (v) => { const pair = v.split(';')[0]; jar = jar ? jar + '; ' + pair : pair; }});
  ctx.location = {reload: () => { reloads++; }};
  vm.runInContext('self.location = location;', ctx);
  ctx.__lib['set-cookie']('consent', 'accepted', '', 'reload', '1');
  assert.strictEqual(reloads, 1);
  ctx.__lib['set-cookie']('consent', 'accepted', '', 'reload', '1');  // already there: nothing
  assert.strictEqual(reloads, 1);
  ctx.__lib['set-cookie']('other', 'yes');
  assert.strictEqual(reloads, 1);
});

test('no-xhr-if answers a matching request as a quiet success, without sending it or marking it', () => {
  const ctx = page();
  vm.runInContext(`
    var sent = 0;
    function XMLHttpRequest(){ this.listeners = {}; }
    XMLHttpRequest.prototype.open = function open(){};
    XMLHttpRequest.prototype.send = function send(){ sent++; };
    XMLHttpRequest.prototype.addEventListener = function(t, f){ (this.listeners[t] = this.listeners[t] || []).push(f); };
    XMLHttpRequest.prototype.dispatchEvent = function(e){ (this.listeners[e.type] || []).forEach((f) => f(e)); };
    ['readyState','status','statusText','responseText','response','responseURL'].forEach((k) =>
      Object.defineProperty(XMLHttpRequest.prototype, k, {configurable: true, get: function(){ return 'real-' + k; }}));
    this.XMLHttpRequest = XMLHttpRequest; this.Event = function(t){ this.type = t; };
    this.setTimeout = function setTimeout(f){ f(); };`, ctx);
  vm.runInContext('this.__lib = (function(){' + lib + '\nreturn zephyrusScriptlets;})();', ctx);
  ctx.__lib['trusted-prevent-xhr']('/ads/', '<VAST version="4.0"></VAST>');
  const r = vm.runInContext(`(() => { const x = new XMLHttpRequest(); let loaded = false; x.addEventListener('load', () => loaded = true);
    x.open('GET', 'https://ad.example/ads/1'); x.send();
    return {sent, loaded, status: x.status, text: x.responseText, own: Object.keys(x).filter((k) => k !== 'listeners')}; })()`, ctx);
  assert.strictEqual(r.sent, 0);
  assert.strictEqual(r.loaded, true);
  assert.strictEqual(r.status, 200);
  assert.strictEqual(r.text, '<VAST version="4.0"></VAST>');
  assert.deepStrictEqual(Array.from(r.own), []);
  const other = vm.runInContext(`(() => { const x = new XMLHttpRequest(); x.open('GET', 'https://site.example/api'); x.send(); return [sent, x.status]; })()`, ctx);
  assert.deepStrictEqual(Array.from(other), [1, 'real-status']);
});

test('trusted-replace-outbound-text edits what a function returns, only under its condition', () => {
  const ctx = page();
  vm.runInContext(`this.TextDecoder = function TextDecoder(){}; TextDecoder.prototype.decode = function decode(x){ return x; };`, ctx);
  ctx.__lib['trusted-replace-outbound-text']('TextDecoder.prototype.decode', '/<template data-ads>[\\s\\S]*?<\\/template>/', '', 'condition', 'data-ads');
  assert.strictEqual(vm.runInContext(`new TextDecoder().decode('a<template data-ads>x</template>b')`, ctx), 'ab');
  assert.strictEqual(vm.runInContext(`new TextDecoder().decode('plain')`, ctx), 'plain');
  assert.ok(/decode\(x\)/.test(vm.runInContext(`Function.prototype.toString.call(TextDecoder.prototype.decode)`, ctx)));
});

test('trusted-suppress-native-method stops only the calls that match its signature', () => {
  const ctx = page();
  vm.runInContext(`var made = []; this.doc = { createElement: function createElement(t){ made.push(t); return {tag: t}; } };`, ctx);
  ctx.__lib['trusted-suppress-native-method']('doc.createElement', '"script"', 'prevent');
  assert.strictEqual(vm.runInContext(`doc.createElement('script')`, ctx), undefined);
  assert.strictEqual(vm.runInContext(`doc.createElement('div').tag`, ctx), 'div');
  assert.deepStrictEqual(Array.from(vm.runInContext('made', ctx)), ['div']);
});

test('m3u-prune drops ad segments from a playlist an XHR returns', () => {
  const ctx = page();
  vm.runInContext(`
    function XMLHttpRequest(){ this.readyState = 4; }
    XMLHttpRequest.prototype.open = function open(){}; XMLHttpRequest.prototype.send = function send(){};
    Object.defineProperty(XMLHttpRequest.prototype, 'responseText', {configurable: true, get: function(){ return '#EXTM3U\\n#EXT-X-AD\\nad1.ts\\nreal1.ts\\n'; }});
    this.XMLHttpRequest = XMLHttpRequest;`, ctx);
  ctx.__lib['m3u-prune']('/#EXT-X-AD\\nad\\d\\.ts\\n/', 'video.m3u8');
  assert.strictEqual(vm.runInContext(`(() => { const x = new XMLHttpRequest(); x.open('GET', 'https://cdn/x/video.m3u8'); x.send(); return x.responseText; })()`, ctx), '#EXTM3U\nreal1.ts\n');
});

test('every new scriptlet name resolves to a function', () => {
  const ctx = page();
  for (const n of ['prevent-xhr', 'trusted-prevent-xhr', 'trusted-prevent-fetch', 'set-cookie-reload', 'trusted-set-cookie-reload',
                   'json-edit', 'trusted-json-edit', 'json-edit-xhr-response', 'trusted-json-edit-xhr-response', 'json-edit-fetch-response',
                   'trusted-json-edit-fetch-response', 'trusted-replace-argument', 'spoof-css', 'prevent-innerHTML', 'href-sanitizer',
                   'set-attr', 'trusted-set-attr', 'trusted-replace-outbound-text', 'refresh-defuser', 'prevent-clipboard-write',
                   'xml-prune', 'm3u-prune', 'jsonl-edit-xhr-response', 'trusted-suppress-native-method', 'trusted-create-html',
                   'proxy-apply-config', 'adjust-setTimeout', 'adjust-setInterval', 'trusted-edit-inbound-object', 'trusted-rpfr'])
    assert.strictEqual(typeof ctx.__lib[n], 'function', n);
});

test('set through a constructor leaves the constructor alone and objects their own values', () => {
  // gamerant.com's rule. The old walk replaced window.Object with undefined.
  const ctx = page();
  ctx.__lib['set']('Object.prototype.ads.nopreroll_', 'true');
  assert.strictEqual(vm.runInContext('typeof Object.freeze', ctx), 'function');
  assert.strictEqual(vm.runInContext('typeof Object.defineProperty', ctx), 'function');
  // The page's config object gets the constant once it is created...
  assert.strictEqual(vm.runInContext('(() => { const cfg = {}; cfg.ads = {nopreroll_: false}; return cfg.ads.nopreroll_; })()', ctx), true);
  // ...and two unrelated objects do not share one value.
  assert.deepStrictEqual(Array.from(vm.runInContext('(() => { const a = {}, b = {}; a.ads = 1; b.ads = 2; return [a.ads, b.ads, Object.keys(a).length]; })()', ctx)), [1, 2, 1]);
});

test('several set rules waiting on one object that does not exist yet all take', () => {
  // YouTube: three rules on ytInitialPlayerResponse, installed before the page
  // assigns it. Each placeholder replaced the last; only adSlots was cleared.
  const ctx = page();
  for (const k of ['playerAds', 'adPlacements', 'adSlots']) {
    ctx.__lib['set']('ytInitialPlayerResponse.' + k, 'undefined');
  }
  const got = vm.runInContext(`(() => {
    var ytInitialPlayerResponse = {playerAds: [1], adPlacements: [2], adSlots: [3], videoDetails: {id: 'x'}};
    return JSON.stringify([typeof ytInitialPlayerResponse.playerAds, typeof ytInitialPlayerResponse.adPlacements,
                           typeof ytInitialPlayerResponse.adSlots, ytInitialPlayerResponse.videoDetails.id]);
  })()`.replace('var ', 'window.'), ctx);
  assert.strictEqual(got, '["undefined","undefined","undefined","x"]');
  // The same for abort-on-property-read rules sharing a parent.
  const ctx2 = page();
  ctx2.__lib['abort-on-property-read']('cfg.first');
  ctx2.__lib['abort-on-property-read']('cfg.second');
  const thrown = vm.runInContext(`(() => { window.cfg = {first: 1, second: 2, other: 3};
    const r = [];
    for (const k of ['first', 'second', 'other']) { try { void window.cfg[k]; r.push('read'); } catch (e) { r.push('threw'); } }
    return r.join(','); })()`, ctx2);
  assert.strictEqual(thrown, 'threw,threw,read');
});

console.log(passed + ' passed, ' + failures + ' failed');
process.exitCode = failures ? 1 : 0;

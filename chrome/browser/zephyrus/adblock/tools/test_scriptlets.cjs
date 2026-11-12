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
  try { fn(); passed++; } catch (e) { failures++; console.log('FAIL', name, '\n    ', e.stack.split('\n').slice(0, 4).join('\n     ')); }
}

// A fresh page: its own JSON, so a patched stringify cannot leak between tests.
function page() {
  const ctx = vm.createContext({console, Proxy, Reflect, Date, Math, RegExp, Array, Object, String, Number, Symbol, Promise});
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
  for (const bad of ['', '[?.a', '.a.b', '[?.a=nope].b=1', '.a+="not json"'])
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

console.log(passed + ' passed, ' + failures + ' failed');
process.exitCode = failures ? 1 : 0;

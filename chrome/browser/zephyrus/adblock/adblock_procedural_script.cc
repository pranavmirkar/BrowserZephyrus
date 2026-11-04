// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_procedural_script.h"

#include <utility>

#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/values.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockProceduralCosmetic,
             "ZephyrusAdblockProceduralCosmetic",
             base::FEATURE_ENABLED_BY_DEFAULT);

namespace {

// A function expression taking the rule list. Clean-room: the grammar is the
// public uBlock Origin "procedural cosmetic filters" syntax, the code is ours.
constexpr char kRunner[] = R"ZPROC((function (rules) {
'use strict';
var MAX_NODES = 20000, BUDGET_MS = 30;
var PROC = {
  'has-text': 1, 'contains': 1, '-abp-contains': 1, 'matches-css': 1,
  'matches-css-before': 1, 'matches-css-after': 1, 'matches-attr': 1,
  'matches-path': 1, 'min-text-length': 1, 'upward': 1, 'nth-ancestor': 1,
  'xpath': 1, 'remove': 1, 'style': 1, 'if': 1, 'if-not': 1, '-abp-has': 1,
  'has': 1, 'not': 1, 'watch-attr': 1, 'remove-attr': 1, 'remove-class': 1,
  'others': 1, 'shadow': 1, 'matches-property': 1, 'matches-media': 1
};
var UNSUPPORTED = { 'others': 1, 'shadow': 1, 'matches-property': 1,
                    'matches-media': 1 };
var ACTIONS = { 'remove': 1, 'style': 1, 'remove-attr': 1, 'remove-class': 1 };
var MARK = 'data-zp-' + Math.random().toString(36).slice(2, 8);
var hidden = new WeakSet();

function hasProc(s) {
  return /:(has-text|contains|-abp-contains|-abp-has|matches-css(-before|-after)?|matches-attr|matches-path|min-text-length|upward|nth-ancestor|xpath|if|if-not|remove|style|others|shadow|watch-attr)\(/.test(s);
}

// A regex that can backtrack catastrophically would freeze the page it runs
// in, so anything with a quantified group that itself contains a quantifier,
// or an unreasonable length, is refused.
function makeRegex(src, flags) {
  if (src.length > 200 || /\([^)]*[+*][^)]*\)[+*{]/.test(src)) return null;
  try { return new RegExp(src, (flags || '').replace(/[^imsu]/g, '')); }
  catch (e) { return null; }
}

// "text", "/regex/flags" -> a test function, or null.
function textTest(arg) {
  var m = /^\/(.+)\/([a-z]*)$/.exec(arg);
  if (m) { var re = makeRegex(m[1], m[2]); if (!re) return null;
           return function (s) { return re.test(s); }; }
  return function (s) { return s.indexOf(arg) !== -1; };
}

function globTest(arg) {
  var m = /^\/(.+)\/([a-z]*)$/.exec(arg);
  if (m) { var re = makeRegex(m[1], m[2]); if (!re) return null;
           return function (s) { return re.test(s); }; }
  if (arg.indexOf('*') === -1) return function (s) { return s === arg; };
  var parts = arg.split('*').map(function (p) {
    return p.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'); });
  var re2 = makeRegex('^' + parts.join('.*') + '$', '');
  return re2 ? function (s) { return re2.test(s); } : null;
}

function closeParen(s, i) {  // s[i] === '('; index of its match, or -1
  var depth = 0, q = '';
  for (; i < s.length; i++) {
    var c = s[i];
    if (q) { if (c === '\\') i++; else if (c === q) q = ''; continue; }
    if (c === '"' || c === "'") q = c;
    else if (c === '\\') i++;
    else if (c === '(') depth++;
    else if (c === ')') { depth--; if (depth === 0) return i; }
  }
  return -1;
}

// "div:has-text(x) > a:upward(2)" -> css and operator steps, or null.
function parse(s) {
  var steps = [], cur = '', depth = 0, q = '';
  for (var i = 0; i < s.length; i++) {
    var c = s[i];
    if (q) { cur += c; if (c === '\\' && i + 1 < s.length) cur += s[++i];
             else if (c === q) q = ''; continue; }
    if (c === '"' || c === "'") { q = c; cur += c; continue; }
    if (c === '\\') { cur += c + (s[++i] || ''); continue; }
    if (c === '[') depth++;
    else if (c === ']') depth--;
    if (c === ':' && depth === 0) {
      var m = /^:([a-z-]+)\(/.exec(s.slice(i));
      if (m && PROC[m[1]]) {
        var open = i + m[0].length - 1, end = closeParen(s, open);
        if (end < 0) return null;
        var name = m[1], arg = s.slice(open + 1, end);
        var native = (name === 'has' || name === 'not') && !hasProc(arg);
        if (!native) {
          if (UNSUPPORTED[name]) return null;
          if (cur.trim()) steps.push({ t: 'css', v: cur });
          cur = '';
          steps.push({ t: 'op', n: name, a: arg });
          i = end; continue;
        }
      }
    }
    cur += c;
  }
  if (cur.trim()) steps.push({ t: 'css', v: cur });
  return steps.length ? steps : null;
}

// Compile one raw selector into { steps, action } or null.
function compile(raw) {
  if (typeof raw !== 'string' || raw.length > 4096) return null;
  var steps = parse(raw);
  if (!steps) return null;
  var action = null;
  while (steps.length && steps[steps.length - 1].t === 'op' &&
         ACTIONS[steps[steps.length - 1].n]) {
    action = steps.pop();
  }
  if (!steps.length) return null;
  for (var i = 0; i < steps.length; i++) {
    var st = steps[i];
    if (st.t !== 'op') continue;
    var n = st.n, a = st.a;
    if (n === 'has-text' || n === 'contains' || n === '-abp-contains') {
      st.f = textTest(a); if (!st.f) return null;
    } else if (n === 'matches-css' || n === 'matches-css-before' ||
               n === 'matches-css-after') {
      var k = a.indexOf(':'); if (k < 1) return null;
      st.prop = a.slice(0, k).trim(); st.f = globTest(a.slice(k + 1).trim());
      st.pseudo = n === 'matches-css-before' ? '::before'
                : n === 'matches-css-after' ? '::after' : null;
      if (!st.f) return null;
    } else if (n === 'matches-attr') {
      var am = /^\s*(?:"([^"]*)"|(\/.+\/[a-z]*)|([^=\s]+))\s*(?:=\s*(?:"([^"]*)"|(\/.+\/[a-z]*)|(.+?)))?\s*$/.exec(a);
      if (!am) return null;
      st.nf = globTest(am[1] !== undefined ? am[1] : am[2] || am[3]);
      var v = am[4] !== undefined ? am[4] : am[5] || am[6];
      st.vf = v === undefined ? null : globTest(v);
      if (!st.nf || (v !== undefined && !st.vf)) return null;
    } else if (n === 'matches-path') {
      st.f = textTest(a.trim()); if (!st.f) return null;
    } else if (n === 'min-text-length') {
      st.k = parseInt(a, 10); if (!(st.k >= 0)) return null;
    } else if (n === 'upward' || n === 'nth-ancestor') {
      if (/^\s*\d+\s*$/.test(a)) { st.k = parseInt(a, 10);
                                   if (st.k < 1 || st.k > 255) return null; }
      else { st.css = a.trim(); if (!st.css || hasProc(st.css)) return null; }
    } else if (n === 'xpath') {
      st.x = a;
    } else if (n === 'has' || n === 'if' || n === '-abp-has' ||
               n === 'not' || n === 'if-not') {
      st.inner = compile(a); if (!st.inner) return null;
      st.neg = n === 'not' || n === 'if-not';
    } else if (n === 'watch-attr') {
      steps.splice(i--, 1);
    } else {
      return null;
    }
  }
  if (action) {
    if (action.n === 'style') {
      var decl = action.a.split(';').map(function (d) { return d.trim(); })
          .filter(Boolean).map(function (d) {
            var j = d.indexOf(':');
            return j > 0 ? [d.slice(0, j).trim(), d.slice(j + 1).trim()] : null;
          });
      if (decl.indexOf(null) !== -1 || !decl.length) return null;
      action.decl = decl;
    } else if (action.n === 'remove-attr' || action.n === 'remove-class') {
      action.name = action.a.trim(); if (!action.name) return null;
      action.f = globTest(action.name); if (!action.f) return null;
    }
  }
  return { steps: steps, action: action };
}

function inScope(list) { return list.length > MAX_NODES ? list.slice(0, MAX_NODES) : list; }

function cssFrom(ctx, css, first) {
  // First css step: the document. Later: relative to each element.
  if (first) {
    try { return inScope(Array.prototype.slice.call(document.querySelectorAll(css || '*'))); }
    catch (e) { return []; }
  }
  var out = [], seen = new Set();
  for (var i = 0; i < ctx.length; i++) {
    var el = ctx[i], rel = css.replace(/^\s+/, '');
    el.setAttribute(MARK, '');
    try {
      // "[mark] > a", "[mark] + a", "[mark] a": the element stands in for
      // :scope, whatever combinator the rule starts with.
      var found = document.querySelectorAll('[' + MARK + '] ' + rel);
      for (var j = 0; j < found.length; j++)
        if (!seen.has(found[j])) { seen.add(found[j]); out.push(found[j]); }
    } catch (e) { /* bad selector: nothing */ }
    el.removeAttribute(MARK);
    if (out.length > MAX_NODES) break;
  }
  return out;
}

function evalOp(st, ctx, first) {
  var n = st.n, out = [], i;
  if (n === 'xpath') {
    var roots = first ? [document] : ctx;
    for (i = 0; i < roots.length; i++) {
      try {
        var r = document.evaluate(st.x, roots[i], null,
                                  XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);
        for (var j = 0; j < r.snapshotLength && out.length < MAX_NODES; j++) {
          var node = r.snapshotItem(j);
          if (node && node.nodeType === 1) out.push(node);
        }
      } catch (e) { /* bad expression */ }
    }
    return out;
  }
  if (first) ctx = cssFrom(null, '*', true);
  if (n === 'upward' || n === 'nth-ancestor') {
    for (i = 0; i < ctx.length; i++) {
      var p = ctx[i];
      if (st.css) { p = p.parentElement && p.parentElement.closest(st.css); }
      else { for (var k = 0; k < st.k && p; k++) p = p.parentElement; }
      if (p && out.indexOf(p) === -1) out.push(p);
    }
    return out;
  }
  if (n === 'matches-path') {
    return st.f(location.pathname + location.search) ? ctx : [];
  }
  if (n === 'not' || n === 'if-not') {
    var set = new Set(run1(st.inner, false));
    return ctx.filter(function (e) { return !set.has(e); });
  }
  for (i = 0; i < ctx.length; i++) {
    var e = ctx[i], ok = false;
    if (n === 'has-text' || n === 'contains' || n === '-abp-contains') {
      ok = st.f(e.textContent || '');
    } else if (n === 'min-text-length') {
      ok = (e.textContent || '').length >= st.k;
    } else if (n === 'matches-css' || n === 'matches-css-before' ||
               n === 'matches-css-after') {
      try { ok = st.f(String(getComputedStyle(e, st.pseudo)
                         .getPropertyValue(st.prop))); } catch (x) { ok = false; }
    } else if (n === 'matches-attr') {
      var names = e.getAttributeNames();
      for (var q = 0; q < names.length && !ok; q++) {
        if (st.nf(names[q])) ok = !st.vf || st.vf(e.getAttribute(names[q]) || '');
      }
    } else if (n === 'has' || n === 'if' || n === '-abp-has') {
      ok = run1(st.inner, true, e).length > 0;
    }
    if (ok) out.push(e);
  }
  return out;
}

// All elements a compiled selector matches. `relative`: the first css step
// is searched inside `scope`; otherwise in the whole document.
function run1(c, relative, scope) {
  var ctx = null, first = true, steps = c.steps;
  for (var i = 0; i < steps.length; i++) {
    var st = steps[i];
    if (st.t === 'css') {
      ctx = (first && relative) ? cssFrom([scope], st.v, false)
          : cssFrom(ctx, st.v, first);
    } else {
      if (first && relative) { ctx = [scope]; first = false; }
      ctx = evalOp(st, ctx, first);
    }
    first = false;
    if (!ctx.length) return [];
  }
  return ctx || [];
}

function act(el, action) {
  if (el === document.documentElement || el === document.body) return;
  if (!action) {
    if (!hidden.has(el)) { hidden.add(el);
      el.style.setProperty('display', 'none', 'important'); }
  } else if (action.n === 'remove') {
    if (el.parentNode) el.remove();
  } else if (action.n === 'style') {
    for (var i = 0; i < action.decl.length; i++)
      el.style.setProperty(action.decl[i][0], action.decl[i][1], 'important');
  } else if (action.n === 'remove-attr') {
    // A name, a glob or a /regex/: every attribute whose name it matches.
    var an = el.getAttributeNames();
    for (var i2 = 0; i2 < an.length; i2++)
      if (action.f(an[i2])) el.removeAttribute(an[i2]);
  } else if (action.n === 'remove-class') {
    var cl = Array.prototype.slice.call(el.classList);
    for (var i3 = 0; i3 < cl.length; i3++)
      if (action.f(cl[i3])) el.classList.remove(cl[i3]);
  }
}

var compiled = [];
for (var r = 0; r < rules.length; r++) {
  var c = compile(rules[r]);
  if (c) compiled.push(c);
}
if (!compiled.length) return;

var cursor = 0, pending = false, last = 0;
function runAll() {
  var start = Date.now(), n = compiled.length;
  for (var done = 0; done < n; done++) {
    var c = compiled[cursor];
    cursor = (cursor + 1) % n;
    try {
      var els = run1(c, false);
      for (var i = 0; i < els.length; i++) act(els[i], c.action);
    } catch (e) { /* a broken rule must not stop the others */ }
    if (Date.now() - start > BUDGET_MS) { schedule(); break; }
  }
  last = Date.now();
}
function schedule() {
  if (pending) return;
  pending = true;
  setTimeout(function () { pending = false; runAll(); },
             Math.max(60, 400 - (Date.now() - last)));
}
document.addEventListener('DOMContentLoaded', runAll);
window.addEventListener('load', runAll);
new MutationObserver(function (muts) {
  for (var i = 0; i < muts.length; i++) {
    var t = muts[i].target;
    if (t && t.nodeType === 1 && t.hasAttribute && t.hasAttribute(MARK)) continue;
    schedule(); return;
  }
}).observe(document, { childList: true, subtree: true });
if (document.readyState !== 'loading') runAll();
}))ZPROC";

}  // namespace

std::string_view ProceduralRunnerSource() {
  return kRunner;
}

std::string BuildProceduralScript(const std::vector<std::string>& selectors) {
  if (selectors.empty()) {
    return std::string();
  }
  base::ListValue list;
  for (const std::string& selector : selectors) {
    list.Append(selector);
  }
  std::string json;
  if (!base::JSONWriter::Write(list, &json)) {
    return std::string();
  }
  // JSON is a subset of JavaScript except for U+2028/2029, which the writer
  // escapes. A "</" is harmless: this is script text, never markup.
  return base::StrCat({kRunner, "(", json, ");\n"});
}

}  // namespace zephyrus_adblock

// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Zephyrus new tab — no NTP. A single floating search bar over a blurred
// desktop wallpaper, with a compact row of frequent-site chips.
//
// This must stay a rollup entry point (optimize_webui_in_files in BUILD.gn):
// optimized builds ship ONLY the bundles named there, so a non-entry module is
// never registered and the page's script 404s in release while working fine in
// out/Default. The served path is `zephyrus_newtab.js` in both configs.

function el(id: string): HTMLElement {
  return document.getElementById(id) as HTMLElement;
}

// --- Search: type a URL or a query, Enter to go. ---------------------------
const input = el('searchinput') as HTMLInputElement;

input.addEventListener('keydown', function(this: HTMLInputElement, e: KeyboardEvent) {
  if (e.key !== 'Enter') {
    return;
  }
  const q = this.value.trim();
  if (!q) {
    return;
  }
  const isUrl = /^(https?:\/\/|www\.)/.test(q) ||
      /^[a-zA-Z0-9-]+\.[a-zA-Z]{2,}(\/|$)/.test(q);
  window.location.href = isUrl ?
      (q.startsWith('http') ? q : 'https://' + q) :
      'https://www.google.com/search?q=' + encodeURIComponent(q);
});

el('searchbar').addEventListener('click', () => input.focus());

// Ctrl+K (the hint on the bar) and "/" focus the search field.
document.addEventListener('keydown', function(e: KeyboardEvent) {
  if ((e.ctrlKey && e.key.toLowerCase() === 'k') ||
      (e.key === '/' && document.activeElement !== input)) {
    e.preventDefault();
    input.focus();
  }
});

// Focus the field on load so the user can just start typing.
input.focus();

// --- Frequent-site chips (curated, India-first). ---------------------------
// Built with DOM methods only (no innerHTML — Trusted Types safe).
function mkChip(
    label: string, url: string, letter: string, bg: string,
    fg: string): HTMLAnchorElement {
  const a = document.createElement('a');
  a.className = 'chip';
  a.href = url;

  const icon = document.createElement('span');
  icon.className = 'chip-icon';
  icon.style.background = bg;
  icon.style.color = fg;
  icon.textContent = letter;

  const span = document.createElement('span');
  span.textContent = label;

  a.appendChild(icon);
  a.appendChild(span);
  return a;
}

const frequent: string[][] = [
  ['YouTube', 'https://www.youtube.com', '▶', '#ff0033', '#fff'],
  ['GitHub', 'https://github.com', 'G', '#e6edf3', '#0d1117'],
  ['Gmail', 'https://mail.google.com', 'M', '#ea4335', '#fff'],
  ['ChatGPT', 'https://chatgpt.com', '✦', '#10a37f', '#fff'],
  ['IRCTC', 'https://www.irctc.co.in', 'I', '#0b5394', '#fff'],
  ['GPay', 'https://pay.google.com', 'G', '#1a73e8', '#fff'],
];

const chips = el('chips');
frequent.forEach(
    (t) => chips.appendChild(mkChip(t[0]!, t[1]!, t[2]!, t[3]!, t[4]!)));

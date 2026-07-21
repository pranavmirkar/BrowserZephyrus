// Zephyrus NTP — runtime logic

function pad(n: number): string {
  return String(n).padStart(2, '0');
}

function el(id: string): HTMLElement {
  return document.getElementById(id) as HTMLElement;
}

// Cache written values so the DOM is only touched when text actually changes
// (greeting/date change at most twice a day, not every second).
const lastText = new Map<string, string>();
function setText(id: string, text: string): void {
  if (lastText.get(id) !== text) {
    lastText.set(id, text);
    el(id).textContent = text;
  }
}

// Ambient tint: the page's glow + background shift with the time of day.
// Pure color — no motion cost, but the page feels alive across a day.
let lastDaypart = '';
function applyDaypart(h: number): void {
  const part = h >= 5 && h < 11 ? 'dawn' :
      h >= 11 && h < 17        ? 'day' :
      h >= 17 && h < 20        ? 'dusk' :
                                 'night';
  if (part !== lastDaypart) {
    lastDaypart = part;
    document.documentElement.setAttribute('data-daypart', part);
  }
}

function tick(): void {
  const now = new Date();
  const h = now.getHours();
  applyDaypart(h);
  setText('time', pad(h) + ':' + pad(now.getMinutes()));
  setText('seconds', pad(now.getSeconds()));
  setText(
      'greeting',
      (h < 12 ? 'Good morning' : h < 17 ? 'Good afternoon' : 'Good evening') +
          ', Pranav');
  let dateText: string;
  try {
    dateText = now.toLocaleDateString('en-IN', {
      weekday: 'long',
      day: 'numeric',
      month: 'long',
    });
  } catch (_e) {
    dateText = now.toDateString();
  }
  setText('date', dateText);
}

// Run the clock only while the page is visible; background NTPs otherwise
// wake the renderer every second for nothing.
let clockTimer: number|null = null;
function startClock(): void {
  if (clockTimer === null) {
    tick();
    clockTimer = setInterval(tick, 1000);
  }
}
function stopClock(): void {
  if (clockTimer !== null) {
    clearInterval(clockTimer);
    clockTimer = null;
  }
}
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    stopClock();
  } else {
    startClock();
  }
});
startClock();

// Search
(el('searchinput') as HTMLInputElement)
    .addEventListener(
        'keydown', function(this: HTMLInputElement, e: KeyboardEvent) {
          if (e.key !== 'Enter') return;
          const q = this.value.trim();
          if (!q) return;
          const isUrl = /^(https?:\/\/|www\.)/.test(q) ||
              /^[a-zA-Z0-9-]+\.[a-zA-Z]{2,}(\/|$)/.test(q);
          window.location.href = isUrl
              ? (q.startsWith('http') ? q : 'https://' + q)
              : 'https://www.google.com/search?q=' + encodeURIComponent(q);
        });

el('searchbar').addEventListener('click', function() {
  (el('searchinput') as HTMLInputElement).focus();
});

// Ctrl+K (the hint on the search bar) and "/" focus the search field.
document.addEventListener('keydown', function(e: KeyboardEvent) {
  const input = el('searchinput') as HTMLInputElement;
  if ((e.ctrlKey && e.key.toLowerCase() === 'k') ||
      (e.key === '/' && document.activeElement !== input)) {
    e.preventDefault();
    input.focus();
  }
});

// Tiles built with DOM methods only (no innerHTML — Trusted Types safe)
function mkTile(
    label: string, url: string, letter: string, bg: string,
    fg: string): HTMLAnchorElement {
  const a = document.createElement('a');
  a.className = 'tile';
  a.href = url;

  const icon = document.createElement('div');
  icon.className = 'tile-icon';
  icon.style.background = bg;
  icon.style.color = fg;
  icon.textContent = letter;

  const span = document.createElement('span');
  span.className = 'tile-label';
  span.textContent = label;

  a.appendChild(icon);
  a.appendChild(span);
  return a;
}

const india: string[][] = [
  ['IRCTC', 'https://www.irctc.co.in', 'I', '#0b5394', '#fff'],
  ['DigiLocker', 'https://www.digilocker.gov.in', 'D', '#0b63ce', '#fff'],
  ['UMANG', 'https://web.umang.gov.in', 'U', '#ff6b35', '#fff'],
  ['GPay', 'https://pay.google.com', 'G', '#1a73e8', '#fff'],
  ['PhonePe', 'https://www.phonepe.com', 'P', '#5f259f', '#fff'],
  ['Paytm', 'https://paytm.com', 'P', '#00b9f1', '#04263a'],
  ['Income Tax', 'https://www.incometax.gov.in', '₹', '#1f4e79', '#fff'],
  ['EPFO', 'https://www.epfindia.gov.in', 'E', '#c8102e', '#fff'],
];

const globalTiles: string[][] = [
  ['YouTube', 'https://www.youtube.com', '▶', '#ff0033', '#fff'],
  ['GitHub', 'https://github.com', 'G', '#e6edf3', '#0d1117'],
  ['Gmail', 'https://mail.google.com', 'M', '#ea4335', '#fff'],
  ['Maps', 'https://maps.google.com', '◎', '#34a853', '#fff'],
  ['Notion', 'https://www.notion.so', 'N', '#f4f4f4', '#111'],
  ['ChatGPT', 'https://chatgpt.com', '✦', '#10a37f', '#fff'],
  ['X', 'https://x.com', '𝕏', '#1a1a1a', '#fff'],
  ['Reddit', 'https://www.reddit.com', 'r', '#ff4500', '#fff'],
];

// One-time welcome card: shown until dismissed, then never again. Purely
// informational (the calm defaults are already on) — no consent needed to
// keep data local, because nothing is collected in the first place.
try {
  if (!localStorage.getItem('zephyrus_welcomed')) {
    el('welcome').classList.add('show');
    el('welcome-dismiss').addEventListener('click', () => {
      localStorage.setItem('zephyrus_welcomed', '1');
      el('welcome').style.display = 'none';
    });
  }
} catch (_e) {
  // localStorage unavailable (rare); skip the card rather than nag forever.
}

const ig = el('india-grid');
const gg = el('global-grid');
india.forEach(
    (t) => ig.appendChild(mkTile(t[0]!, t[1]!, t[2]!, t[3]!, t[4]!)));
globalTiles.forEach(
    (t) => gg.appendChild(mkTile(t[0]!, t[1]!, t[2]!, t[3]!, t[4]!)));

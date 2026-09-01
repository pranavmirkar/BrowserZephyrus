// §12.6 breakage detectors.
//
// The one that matters most is the bot-challenge detector, and the reason is
// worth stating: canvas randomization CANNOT break a site visibly. The noise is
// at most one step per subpixel and never touches alpha, so a screenshot diff
// comes back clean on essentially every site. What randomization can do is make
// a site's bot detection fire — and that is invisible to a screenshot, because
// the page renders perfectly and quietly serves a challenge, a degraded
// experience, or a silent 403.
//
// Prior evidence: four of the top 200 were observed serving bot challenges when
// a worker disagreed with its document about the same canvas. So a "clean"
// screenshot run proves very little, and THIS is the signal to hunt.

// Vendor markers. Matched against request URLs, and separately against page
// text and frame URLs, because a challenge can arrive either way.
const CHALLENGE_URL_MARKERS = [
  'challenges.cloudflare.com',
  '/cdn-cgi/challenge-platform',
  'hcaptcha.com',
  'recaptcha.net',
  'google.com/recaptcha',
  'captcha-delivery.com',      // DataDome
  'perimeterx.net',
  'px-cloud.net',              // PerimeterX
  'datadome.co',
  'arkoselabs.com',
  'funcaptcha.com',
  'geo.captcha-delivery.com',
  'imperva.com',
  'incapsula.com',
  'akam/', // Akamai Bot Manager sensor paths
  '_sec/cp_challenge',
];

// Text a challenge interstitial shows. Deliberately conservative: these must not
// match ordinary page copy, or every run drowns in false positives.
const CHALLENGE_TEXT_MARKERS = [
  'checking your browser before accessing',
  'verify you are human',
  'verifying you are human',
  'please verify you are a human',
  'enable javascript and cookies to continue',
  'why have i been blocked',
  'access denied',
  'unusual traffic from your computer network',
  'our systems have detected unusual traffic',
  'are you a robot',
  'complete the security check',
  'ray id',                    // Cloudflare block pages
  'pardon our interruption',   // Distil/Imperva
  'client challenge',
];

export function scanChallenges({ requests, pageText, frameUrls, title }) {
  const hits = [];

  for (const r of requests) {
    const u = (r.url || '').toLowerCase();
    for (const m of CHALLENGE_URL_MARKERS) {
      if (u.includes(m)) {
        hits.push({ kind: 'challenge-request', marker: m, detail: r.url.slice(0, 160) });
        break;
      }
    }
  }

  const text = (pageText || '').toLowerCase();
  for (const m of CHALLENGE_TEXT_MARKERS) {
    if (text.includes(m)) {
      hits.push({ kind: 'challenge-text', marker: m, detail: excerpt(pageText, m) });
    }
  }

  for (const f of frameUrls || []) {
    const u = (f || '').toLowerCase();
    for (const m of CHALLENGE_URL_MARKERS) {
      if (u.includes(m)) {
        hits.push({ kind: 'challenge-frame', marker: m, detail: f.slice(0, 160) });
        break;
      }
    }
  }

  const t = (title || '').toLowerCase();
  if (t.includes('just a moment') || t.includes('attention required') ||
      t.includes('access to this page has been denied')) {
    hits.push({ kind: 'challenge-title', marker: t.slice(0, 60), detail: title });
  }

  return dedupe(hits);
}

// HTTP outcomes that indicate the site refused us. 429 and 403 are the ones bot
// walls actually use; 5xx is usually the site's own problem but is still a
// difference worth reporting when it appears in only one arm.
export function scanHttp({ requests, mainUrl }) {
  const hits = [];
  for (const r of requests) {
    if (r.status === 403 || r.status === 429 || r.status === 503) {
      hits.push({
        kind: 'http-' + r.status,
        marker: String(r.status),
        detail: (r.url || '').slice(0, 160),
        isMain: !!(mainUrl && r.url === mainUrl),
      });
    }
  }
  return dedupe(hits);
}

// Console errors and uncaught exceptions. Noisy on the open web, which is
// exactly why this is only ever reported as an A/B DIFFERENCE — an error
// present in both arms is the site's own, and uninteresting here.
export function scanConsole({ consoleErrors, exceptions }) {
  const hits = [];
  for (const e of consoleErrors) {
    hits.push({ kind: 'console-error', marker: normalize(e), detail: e.slice(0, 200) });
  }
  for (const e of exceptions) {
    hits.push({ kind: 'exception', marker: normalize(e), detail: e.slice(0, 200) });
  }
  return dedupe(hits);
}

// Strip the parts that differ run to run (ids, ports, hashes, timestamps) so
// "the same error" in both arms compares equal and cancels out.
export function normalize(s) {
  return String(s)
    .toLowerCase()
    .replace(/https?:\/\/[^\s)]+/g, '<url>')
    .replace(/\b[0-9a-f]{8,}\b/g, '<hex>')
    .replace(/\d+/g, '<n>')
    .replace(/\s+/g, ' ')
    .trim()
    .slice(0, 180);
}

function excerpt(text, marker) {
  const i = (text || '').toLowerCase().indexOf(marker);
  if (i < 0) return '';
  return text.slice(Math.max(0, i - 40), i + 120).replace(/\s+/g, ' ');
}

function dedupe(hits) {
  const seen = new Set();
  const out = [];
  for (const h of hits) {
    const k = h.kind + '|' + h.marker;
    if (seen.has(k)) continue;
    seen.add(k);
    out.push(h);
  }
  return out;
}

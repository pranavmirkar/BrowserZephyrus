// Everything a request must pass before a provider key is touched.
//
// The browser ships an expiring, HMAC-signed token, not a provider key. Someone
// who extracts it from the installer gets what the installer already gives them:
// this proxy, on these terms -- allowed models, capped sizes, a rate limit per
// address, and an expiry -- until the token's date passes or DEMO_DISABLED is set.
// The provider keys exist only in this deployment's environment.
'use strict';

const crypto = require('node:crypto');

const TOKEN_PREFIX = 'zd1';

// Models a demo token may use, and the largest reply it may ask for.
const ALLOWED_MODELS = new Set([
  'claude-opus-5-5',
  'claude-opus-5',
  'claude-sonnet-5-5',
  'claude-haiku-4-5-20251001',
]);
const MAX_OUTPUT_TOKENS = 16000;  // what the browser asks for; thinking counts against it
const MAX_JSON_BYTES = 3 * 1024 * 1024;   // a step's page text plus a screenshot
const MAX_AUDIO_BYTES = 4 * 1024 * 1024;  // Vercel's own body limit is 4.5 MB

function sign(secret, expires) {
  return crypto.createHmac('sha256', secret)
      .update(`${TOKEN_PREFIX}.${expires}`).digest('hex').slice(0, 40);
}

function mintToken(secret, expiresEpochSeconds) {
  return `${TOKEN_PREFIX}.${expiresEpochSeconds}.${sign(secret, expiresEpochSeconds)}`;
}

// Returns null when the token is good, else a short reason (never the token).
function checkToken(token, env, nowSeconds) {
  if (env.DEMO_DISABLED === '1') return 'the demo has been switched off';
  const secret = env.DEMO_TOKEN_SECRET;
  if (!secret || secret.length < 32) return 'the demo is not configured';
  if (typeof token !== 'string' || token.length > 200) return 'bad token';
  const parts = token.split('.');
  if (parts.length !== 3 || parts[0] !== TOKEN_PREFIX || !/^\d{1,12}$/.test(parts[1])) {
    return 'bad token';
  }
  const expected = sign(secret, parts[1]);
  const a = Buffer.from(parts[2]);
  const b = Buffer.from(expected);
  if (a.length !== b.length || !crypto.timingSafeEqual(a, b)) return 'bad token';
  if (Number(parts[1]) < nowSeconds) return 'the demo has ended';
  return null;
}

// Best-effort limits. Serverless instances do not share memory, so this bounds
// one instance, not the fleet: the provider-side spend caps are the real ceiling.
const buckets = new Map();
function rateLimit(kind, ip, limitPerMinute, nowMs) {
  const key = `${kind}|${ip}`;
  const windowStart = nowMs - 60_000;
  const hits = (buckets.get(key) || []).filter((t) => t > windowStart);
  if (hits.length >= limitPerMinute) {
    buckets.set(key, hits);
    return false;
  }
  hits.push(nowMs);
  buckets.set(key, hits);
  if (buckets.size > 5000) buckets.clear();  // an attacker cannot grow this forever
  return true;
}

function clientIp(req) {
  const fwd = req.headers['x-forwarded-for'];
  return (typeof fwd === 'string' ? fwd.split(',')[0].trim() : '') ||
      (req.socket && req.socket.remoteAddress) || 'unknown';
}

// The browser sends the token where the provider's own key would go.
function bearer(req) {
  const h = req.headers;
  const auth = typeof h.authorization === 'string' ? h.authorization : '';
  const key = typeof h['x-api-key'] === 'string' ? h['x-api-key'] : '';
  return key || auth.replace(/^Bearer\s+/i, '');
}

function reply(res, status, message) {
  res.statusCode = status;
  res.setHeader('content-type', 'application/json');
  res.setHeader('cache-control', 'no-store');
  // An oversize body was not read to the end; do not let the client reuse the socket.
  if (status === 413) res.setHeader('connection', 'close');
  res.end(JSON.stringify({ type: 'error', error: { type: 'demo_proxy', message } }));
}

async function readBody(req, limit) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > limit) return null;
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

// The upstream host is fixed. It can be redirected only when DEMO_TEST=1, which
// the deployment never sets, so a request can never choose where a key goes.
function upstream(env, name, fallback) {
  return env.DEMO_TEST === '1' && env[name] ? env[name] : fallback;
}

module.exports = {
  ALLOWED_MODELS, MAX_OUTPUT_TOKENS, MAX_JSON_BYTES, MAX_AUDIO_BYTES,
  mintToken, checkToken, rateLimit, clientIp, bearer, reply, readBody, upstream,
};

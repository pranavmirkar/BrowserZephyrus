'use strict';
const g = require('../../../lib/guard');

async function handler(req, res) {
  if (req.method !== 'POST') return g.reply(res, 405, 'POST only');
  const why = g.checkToken(g.bearer(req), process.env, Math.floor(Date.now() / 1000));
  if (why) return g.reply(res, 401, why);
  if (!g.rateLimit('anthropic', g.clientIp(req), 30, Date.now())) {
    return g.reply(res, 429, 'slow down a little');
  }
  const key = (process.env.ANTHROPIC_API_KEY || '').trim();
  if (!key) return g.reply(res, 503, 'the demo is not configured');

  const raw = await g.readBody(req, g.MAX_JSON_BYTES);
  if (!raw) return g.reply(res, 413, 'request too large');
  let body;
  try {
    body = JSON.parse(raw.toString('utf8'));
  } catch {
    return g.reply(res, 400, 'bad JSON');
  }
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    return g.reply(res, 400, 'bad request');
  }
  if (!g.ALLOWED_MODELS.has(body.model)) {
    return g.reply(res, 400, 'that model is not available in the demo');
  }
  if (!Number.isInteger(body.max_tokens) || body.max_tokens < 1 ||
      body.max_tokens > g.MAX_OUTPUT_TOKENS) {
    return g.reply(res, 400, `max_tokens must be 1 to ${g.MAX_OUTPUT_TOKENS}`);
  }
  if (body.stream) return g.reply(res, 400, 'streaming is not available in the demo');

  const version = typeof req.headers['anthropic-version'] === 'string'
      ? req.headers['anthropic-version'] : '2023-06-01';
  if (!/^[0-9-]{1,20}$/.test(version)) return g.reply(res, 400, 'bad version');

  let out;
  try {
    const base = g.upstream(process.env, 'UPSTREAM_ANTHROPIC', 'https://api.anthropic.com');
    out = await fetch(`${base}/v1/messages`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', 'x-api-key': key,
                 'anthropic-version': version },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(55_000),
    });
  } catch {
    return g.reply(res, 502, 'could not reach the model provider');
  }
  const text = await out.text();
  res.statusCode = out.status;
  res.setHeader('content-type', out.headers.get('content-type') || 'application/json');
  res.setHeader('cache-control', 'no-store');
  res.end(text);
}

module.exports = handler;
// The body is read as a stream so its size can be capped before it is parsed.
module.exports.config = { api: { bodyParser: false } };

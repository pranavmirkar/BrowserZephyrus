'use strict';
const g = require('../../lib/guard');

async function handler(req, res) {
  if (req.method !== 'POST') return g.reply(res, 405, 'POST only');
  const why = g.checkToken(g.bearer(req), process.env, Math.floor(Date.now() / 1000));
  if (why) return g.reply(res, 401, why);
  if (!g.rateLimit('assemblyai', g.clientIp(req), 30, Date.now())) {
    return g.reply(res, 429, 'slow down a little');
  }
  const key = (process.env.ASSEMBLYAI_API_KEY || '').trim();
  if (!key) return g.reply(res, 503, 'the demo is not configured');

  const type = String(req.headers['content-type'] || '');
  if (!/^multipart\/form-data; boundary=[\w'()+,./:=?-]{1,70}$/.test(type)) {
    return g.reply(res, 400, 'bad content type');
  }
  const raw = await g.readBody(req, g.MAX_AUDIO_BYTES);
  if (!raw) return g.reply(res, 413, 'recording too long');

  const model = req.headers['x-aai-model'];
  const headers = {
    authorization: key,  // AssemblyAI takes the raw key, no "Bearer"
    'content-type': type,
  };
  // Only AssemblyAI's own speech models: the header is the client's to set.
  if (typeof model === 'string' && /^universal-[\w.-]{1,50}$/.test(model)) {
    headers['x-aai-model'] = model;
  }

  let out;
  try {
    const base = g.upstream(process.env, 'UPSTREAM_ASSEMBLYAI', 'https://sync.assemblyai.com');
    out = await fetch(`${base}/transcribe`, {
      method: 'POST',
      headers,
      body: raw,
      signal: AbortSignal.timeout(55_000),
    });
  } catch {
    return g.reply(res, 502, 'could not reach the transcription provider');
  }
  const text = await out.text();
  res.statusCode = out.status;
  res.setHeader('content-type', out.headers.get('content-type') || 'application/json');
  res.setHeader('cache-control', 'no-store');
  res.end(text);
}

module.exports = handler;
module.exports.config = { api: { bodyParser: false } };

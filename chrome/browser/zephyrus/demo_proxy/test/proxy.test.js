'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');

const SECRET = 'a'.repeat(48);
const KEY = 'sk-ant-REAL-SERVER-KEY';
const AAI = 'aai-REAL-SERVER-KEY';

let fake, fakeUrl, proxy, base, seen;

test.before(async () => {
  // A stand-in for the providers that records exactly what reached it.
  fake = http.createServer((req, res) => {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => {
      seen = { url: req.url, headers: req.headers, body: Buffer.concat(chunks).toString() };
      res.setHeader('content-type', 'application/json');
      res.end(JSON.stringify({ ok: true, echoed: req.url }));
    });
  });
  await new Promise((r) => fake.listen(0, '127.0.0.1', r));
  fakeUrl = `http://127.0.0.1:${fake.address().port}`;
  Object.assign(process.env, {
    DEMO_TOKEN_SECRET: SECRET, ANTHROPIC_API_KEY: KEY, ASSEMBLYAI_API_KEY: AAI,
    DEMO_TEST: '1', UPSTREAM_ANTHROPIC: fakeUrl, UPSTREAM_ASSEMBLYAI: fakeUrl,
  });
  delete process.env.DEMO_DISABLED;
  proxy = require('../scripts/local_server').createServer();
  await new Promise((r) => proxy.listen(0, '127.0.0.1', r));
  base = `http://127.0.0.1:${proxy.address().port}`;
});
test.after(() => { proxy.close(); fake.close(); });

const { mintToken } = require('../lib/guard');
const good = () => mintToken(SECRET, Math.floor(Date.now() / 1000) + 3600);
const call = (path, { token = good(), body = {}, headers = {}, method = 'POST', raw } = {}) =>
  fetch(base + path, {
    method,
    headers: { 'x-api-key': token, 'content-type': 'application/json', ...headers },
    body: method === 'GET' ? undefined : (raw ?? JSON.stringify(body)),
  });
const msg = (over = {}) => ({ model: 'claude-opus-5-5', max_tokens: 100,
                              messages: [{ role: 'user', content: 'hi' }], ...over });

test('a good token reaches the provider WITH the server key, never the token', async () => {
  const r = await call('/api/anthropic/v1/messages', { body: msg() });
  assert.equal(r.status, 200);
  assert.equal(seen.url, '/v1/messages');
  assert.equal(seen.headers['x-api-key'], KEY);
  assert.ok(!JSON.stringify(seen.headers).includes('zd1.'));
});

test('the provider key is never returned to the caller', async () => {
  const r = await call('/api/anthropic/v1/messages', { body: msg() });
  assert.ok(!(await r.text()).includes(KEY));
});

test('no token, a forged token, an expired token, and a token for another secret are refused', async () => {
  for (const token of ['', 'zd1.99999999999.deadbeef', 'garbage',
                       mintToken(SECRET, Math.floor(Date.now() / 1000) - 10),
                       mintToken('b'.repeat(48), Math.floor(Date.now() / 1000) + 3600)]) {
    seen = null;
    const r = await call('/api/anthropic/v1/messages', { token, body: msg() });
    assert.equal(r.status, 401, `token ${token}`);
    assert.equal(seen, null, 'a refused request must not reach the provider');
  }
});

test('changing the expiry invalidates the signature', async () => {
  const [p, , sig] = good().split('.');
  const r = await call('/api/anthropic/v1/messages',
      { token: `${p}.${Math.floor(Date.now() / 1000) + 9e6}.${sig}`, body: msg() });
  assert.equal(r.status, 401);
});

test('only allowed models, bounded output, no streaming', async () => {
  assert.equal((await call('/api/anthropic/v1/messages', { body: msg({ model: 'claude-fable-5-1' }) })).status, 400);
  assert.equal((await call('/api/anthropic/v1/messages', { body: msg({ max_tokens: 999999 }) })).status, 400);
  assert.equal((await call('/api/anthropic/v1/messages', { body: msg({ max_tokens: '5' }) })).status, 400);
  assert.equal((await call('/api/anthropic/v1/messages', { body: msg({ stream: true }) })).status, 400);
  assert.equal((await call('/api/anthropic/v1/messages', { raw: '[1,2]' })).status, 400);
  assert.equal((await call('/api/anthropic/v1/messages', { raw: '{nope' })).status, 400);
});

test('an oversize body is refused before it is parsed, and never reaches the provider', async () => {
  seen = null;
  let status = 'reset';
  try {
    status = (await call('/api/anthropic/v1/messages',
        { raw: JSON.stringify(msg({ pad: 'x'.repeat(3.5 * 1024 * 1024) })) })).status;
  } catch {
    // The server may cut the upload short before the client has finished it.
  }
  assert.ok(status === 413 || status === 'reset', `status ${status}`);
  assert.equal(seen, null);
});

test('GET is refused', async () => {
  assert.equal((await call('/api/anthropic/v1/messages', { method: 'GET' })).status, 405);
});

test('a caller cannot choose the upstream host through the request', async () => {
  seen = null;
  await call('/api/anthropic/v1/messages', { body: msg(), headers: { 'x-forwarded-host': 'evil.example', 'x-upstream': 'http://evil.example' } });
  assert.equal(seen.url, '/v1/messages');
});

test('the audio route forwards the raw key and the multipart body', async () => {
  const body = '--XX\r\ncontent-disposition: form-data; name="file"\r\n\r\nWAVDATA\r\n--XX--\r\n';
  const r = await fetch(`${base}/api/assemblyai/transcribe`, {
    method: 'POST',
    headers: { authorization: good(), 'content-type': 'multipart/form-data; boundary=XX', 'x-aai-model': 'universal-3-5-pro' },
    body,
  });
  assert.equal(r.status, 200);
  assert.equal(seen.headers.authorization, AAI);
  assert.equal(seen.headers['x-aai-model'], 'universal-3-5-pro');
  assert.equal(seen.body, body);
});

test('the audio route refuses a non-multipart body and a bad token', async () => {
  const r = await fetch(`${base}/api/assemblyai/transcribe`, {
    method: 'POST', headers: { authorization: good(), 'content-type': 'text/plain' }, body: 'x' });
  assert.equal(r.status, 400);
  const r2 = await fetch(`${base}/api/assemblyai/transcribe`, {
    method: 'POST', headers: { authorization: 'nope', 'content-type': 'multipart/form-data; boundary=XX' }, body: 'x' });
  assert.equal(r2.status, 401);
});

test('health checks a token without calling a provider', async () => {
  seen = null;
  const r = await call('/api/health', { method: 'GET' });
  assert.equal(r.status, 200);
  assert.equal(seen, null);
  assert.equal((await call('/api/health', { method: 'GET', token: 'x' })).status, 401);
});

test('the kill switch stops everything at once', async () => {
  process.env.DEMO_DISABLED = '1';
  try {
    assert.equal((await call('/api/anthropic/v1/messages', { body: msg() })).status, 401);
    assert.equal((await call('/api/health', { method: 'GET' })).status, 401);
  } finally {
    delete process.env.DEMO_DISABLED;
  }
});

test('without the test flag the upstream cannot be redirected', () => {
  const { upstream } = require('../lib/guard');
  assert.equal(upstream({ UPSTREAM_ANTHROPIC: 'http://evil' }, 'UPSTREAM_ANTHROPIC', 'https://api.anthropic.com'),
               'https://api.anthropic.com');
});

test('one address is rate limited, another is not', () => {
  const { rateLimit } = require('../lib/guard');
  const now = Date.now();
  for (let i = 0; i < 5; i++) assert.ok(rateLimit('t', '1.1.1.1', 5, now));
  assert.equal(rateLimit('t', '1.1.1.1', 5, now), false);
  assert.ok(rateLimit('t', '2.2.2.2', 5, now));
  assert.ok(rateLimit('t', '1.1.1.1', 5, now + 61_000));
});

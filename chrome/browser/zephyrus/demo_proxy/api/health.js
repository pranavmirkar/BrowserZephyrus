'use strict';
const g = require('../lib/guard');

// Answers "is this token good?" without touching a provider or spending anything.
module.exports = function handler(req, res) {
  const why = g.checkToken(g.bearer(req), process.env, Math.floor(Date.now() / 1000));
  if (why) return g.reply(res, 401, why);
  res.statusCode = 200;
  res.setHeader('content-type', 'application/json');
  res.end(JSON.stringify({
    ok: true,
    anthropic: Boolean(process.env.ANTHROPIC_API_KEY),
    assemblyai: Boolean(process.env.ASSEMBLYAI_API_KEY),
  }));
};

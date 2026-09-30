// Serves the proxy's handlers on localhost, for tests and for running the
// browser against a proxy without deploying. Not used in production: Vercel
// routes the api/ files itself.
//
//   DEMO_TOKEN_SECRET=... ANTHROPIC_API_KEY=... node scripts/local_server.js 8787
'use strict';

const http = require('node:http');
const routes = {
  '/api/anthropic/v1/messages': require('../api/anthropic/v1/messages'),
  '/api/assemblyai/transcribe': require('../api/assemblyai/transcribe'),
  '/api/health': require('../api/health'),
};

function createServer() {
  return http.createServer((req, res) => {
    const handler = routes[new URL(req.url, 'http://x').pathname];
    if (!handler) {
      res.statusCode = 404;
      return res.end('not found');
    }
    Promise.resolve(handler(req, res)).catch(() => {
      if (!res.headersSent) res.statusCode = 500;
      res.end();
    });
  });
}

module.exports = { createServer };

if (require.main === module) {
  const port = Number(process.argv[2] || 8787);
  createServer().listen(port, '127.0.0.1', () => console.log(`demo proxy on http://127.0.0.1:${port}`));
}

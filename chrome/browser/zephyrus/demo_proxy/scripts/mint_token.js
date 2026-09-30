// Prints a demo token that expires in N days: node scripts/mint_token.js <secret> <days>
// The secret comes from the deploy script; it never leaves this machine and the
// deployment except as the token's signature.
'use strict';

const { mintToken } = require('../lib/guard');

const secret = process.argv[2];
const days = Number(process.argv[3] || 14);
if (!secret || secret.length < 32 || !(days > 0 && days <= 90)) {
  console.error('usage: mint_token.js <secret of 32+ chars> <days 1-90>');
  process.exit(2);
}
process.stdout.write(mintToken(secret, Math.floor(Date.now() / 1000) + Math.round(days * 86400)));

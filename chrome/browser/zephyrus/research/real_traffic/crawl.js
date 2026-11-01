// Loads each page as an ordinary visitor (nothing blocked) and records every request it makes.
const { chromium } = require('playwright'); const fs = require('fs');
const sites = fs.readFileSync(process.argv[2], 'utf8').split('\n').map(s => s.trim()).filter(Boolean);
const outDir = process.argv[3]; const CONC = +process.argv[4] || 4; fs.mkdirSync(outDir, { recursive: true });
const proxy = process.env.HTTPS_PROXY ? { server: process.env.HTTPS_PROXY } : undefined;   // use the environment's proxy if there is one
const UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36';
async function one(browser, url, idx) {
  const ctx = await browser.newContext({ userAgent: UA, viewport: { width: 1366, height: 768 }, locale: 'en-IN' });
  const page = await ctx.newPage(); const reqs = []; const meta = new Map();
  page.on('request', r => { const f = r.frame(); meta.set(r, { url: r.url(), type: r.resourceType(), method: r.method(), frame: f ? f.url() : '', main: f ? f === page.mainFrame() : true, nav: r.isNavigationRequest(), post: (r.postData() || '').length, t: Date.now() }); });
  page.on('response', async r => { const m = meta.get(r.request()); if (!m) return; m.status = r.status(); const h = r.headers(); m.ct = h['content-type'] || ''; m.cl = +(h['content-length'] || -1); });
  page.on('requestfinished', r => { const m = meta.get(r); if (m) reqs.push(m); }); page.on('requestfailed', r => { const m = meta.get(r); if (m) { m.failed = true; reqs.push(m); } });
  let ok = true, err = '', title = '';
  try {
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 });
    await page.waitForTimeout(3500); for (let i = 0; i < 3; i++) { await page.mouse.wheel(0, 900); await page.waitForTimeout(1200); } await page.waitForTimeout(2000);
    title = (await page.title()).slice(0, 80);
  } catch (e) { ok = false; err = String(e).split('\n')[0].slice(0, 120); }
  const top = page.url(); await ctx.close();
  fs.writeFileSync(`${outDir}/${String(idx).padStart(3, '0')}.json`, JSON.stringify({ site: url, top, ok, err, title, n: reqs.length, reqs }));
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${String(reqs.length).padStart(4)} req  ${url}  ${ok ? title : err}`);
}
(async () => {
  const browsers = await Promise.all(Array.from({ length: CONC }, () => chromium.launch(proxy ? { proxy } : {})));
  let next = 0; await Promise.all(browsers.map(async b => { while (next < sites.length) { const i = next++; try { await one(b, sites[i], i); } catch (e) { console.log('ERR', sites[i], String(e).slice(0, 100)); } } }));
  await Promise.all(browsers.map(b => b.close()));
})();

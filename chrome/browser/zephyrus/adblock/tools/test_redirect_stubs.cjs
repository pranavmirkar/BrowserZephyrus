// Runs every redirect stub in a fresh context and uses it the way a page does.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const dir = path.join(__dirname, '..', 'redirect_resources');
let failures = 0, passed = 0;

function load(file, setup) {
  const timers = [];
  const win = {setTimeout: (f, ms) => { timers.push(f); return timers.length; }, Promise, Proxy, Symbol,
               Date, Array, Object, Math, JSON, console};
  win.window = win;
  if (setup) setup(win);
  const ctx = vm.createContext(win);
  const src = fs.readFileSync(path.join(dir, file), 'utf8');
  vm.runInContext(src, ctx, {filename: file});
  const flush = () => { while (timers.length) timers.shift()(); };
  return {win, flush};
}
function test(name, fn) {
  try { fn(); passed++; } catch (e) { failures++; console.log('FAIL', name, '\n    ', e.stack.split('\n').slice(0, 3).join('\n     ')); }
}

// -- every file parses and runs without throwing, twice (a second load must be harmless)
for (const f of fs.readdirSync(dir).filter((n) => n.endsWith('.js'))) {
  test('runs twice: ' + f, () => {
    const {win, flush} = load(f);
    vm.runInContext(fs.readFileSync(path.join(dir, f), 'utf8'), vm.createContext(win), {filename: f});
    flush();
  });
}

test('google-ima: a player gets an ads manager and is told to resume content', () => {
  const {win, flush} = load('google-ima.js');
  const ima = win.google.ima;
  const container = new ima.AdDisplayContainer({}, {});
  container.initialize();
  const loader = new ima.AdsLoader(container);
  const seen = [];
  loader.addEventListener(ima.AdsManagerLoadedEvent.Type.ADS_MANAGER_LOADED, (e) => {
    const manager = e.getAdsManager({}, new ima.AdsRenderingSettings());
    [ima.AdEvent.Type.LOADED, ima.AdEvent.Type.CONTENT_RESUME_REQUESTED, ima.AdEvent.Type.ALL_ADS_COMPLETED]
        .forEach((t) => manager.addEventListener(t, (ev) => seen.push(ev.type)));
    manager.addEventListener(ima.AdErrorEvent.Type.AD_ERROR, () => seen.push('ERROR'));
    manager.init(640, 360, ima.ViewMode.NORMAL);
    manager.start();
  }, false);
  const req = new ima.AdsRequest();
  req.adTagUrl = 'https://x.example/vast';
  req.setAdWillAutoPlay(true);
  loader.requestAds(req);
  flush();
  assert.deepStrictEqual(seen, ['loaded', 'contentResumeRequested', 'allAdsCompleted']);
  assert.strictEqual(new ima.AdError('t', 1, 'm').getMessage(), 'm');
  assert.strictEqual(ima.ImaSdkSettings.VpaidMode.ENABLED, 1);
});

test('gpt: the usual slot-definition chain and the command queue work', () => {
  const {win} = load('googletagservices_gpt.js', (w) => {
    w.googletag = {cmd: []};
    w.googletag.cmd.push(function() { w.__early = true; });
  });
  const g = win.googletag;
  assert.strictEqual(win.__early, true, 'commands queued before load run');
  let ran = false;
  g.cmd.push(function() {
    const slot = g.defineSlot('/1/x', [300, 250], 'div-1').addService(g.pubads()).setTargeting('k', 'v');
    g.pubads().enableSingleRequest().collapseEmptyDivs().setTargeting('a', 'b');
    g.enableServices();
    g.display('div-1');
    assert.strictEqual(slot.getSlotElementId(), 'div-1');
    assert.strictEqual(g.pubads().getSlots().length, 1);
    const sm = g.sizeMapping().addSize([0, 0], [1, 1]).build();
    assert.ok(Array.isArray(sm));
    ran = true;
  });
  assert.ok(ran, 'commands pushed after load run at once');
});

test('gtm: eventCallback is called so the page can move on', () => {
  const {win, flush} = load('googletagmanager_gtm.js');
  let called = 0;
  win.dataLayer.push({event: 'purchase', eventCallback: () => called++});
  flush();
  assert.strictEqual(called, 1);
  assert.strictEqual(win.dataLayer.length, 1);
});

test('analytics.js: ga() is callable, runs callbacks, and returns a tracker', () => {
  const {win, flush} = load('google-analytics_analytics.js', (w) => {
    w.GoogleAnalyticsObject = 'ga';
    w.ga = {q: [['create', 'UA-1', 'auto']]};
  });
  let ready = 0, hit = 0;
  win.ga(function(tracker) { ready++; assert.ok(tracker); });
  win.ga('send', 'event', {hitCallback: () => hit++});
  flush();
  assert.strictEqual(ready, 1); assert.strictEqual(hit, 1);
  assert.ok(Array.isArray(win.ga.getAll()));
  assert.ok(win.ga.create('x'));
});

test('ga.js: _gaq takes both commands and functions', () => {
  const {win} = load('google-analytics_ga.js', (w) => { w._gaq = [['_setAccount', 'UA-1']]; });
  let fn = 0;
  win._gaq.push(['_trackPageview'], function() { fn++; });
  assert.strictEqual(fn, 1);
  assert.ok(win._gat._getTracker('UA-1'));
});

test('adsbygoogle: push does not throw and the flag is set', () => {
  const {win} = load('googlesyndication_adsbygoogle.js', (w) => { w.adsbygoogle = []; });
  win.adsbygoogle.push({});
  assert.strictEqual(win.adsbygoogle.loaded, true);
  const fresh = load('googlesyndication_adsbygoogle.js');
  fresh.win.adsbygoogle.push({});
  assert.strictEqual(fresh.win.adsbygoogle.loaded, true);
});

test('apstag: fetchBids answers with no bids', () => {
  const {win, flush} = load('amazon_apstag.js');
  let bids = null, inited = 0;
  win.apstag.init({pubID: '1'}, () => inited++);
  win.apstag.fetchBids({slots: []}, (b) => { bids = b; });
  flush();
  assert.strictEqual(JSON.stringify(bids), '[]'); assert.strictEqual(inited, 1);
  assert.strictEqual(JSON.stringify(win.apstag.targetingKeys()), '[]');
});

test('amazon_ads: callbacks fire', () => {
  const {win, flush} = load('amazon_ads.js');
  let n = 0;
  win.amznads.getAdsCallback('id', () => n++); win.amznads.tryGetAdsAsync('id', () => n++);
  flush();
  assert.strictEqual(n, 2);
});

test('fingerprint2: both calling styles return the same constant', () => {
  const {win, flush} = load('fingerprint2.js');
  const got = [];
  new win.Fingerprint2().get((r) => got.push(r));
  win.Fingerprint2.get({}, (r) => got.push(r));
  flush();
  assert.strictEqual(got.length, 2); assert.strictEqual(got[0], got[1]);
  assert.strictEqual(got[0].length, 32);
});

test('fingerprint3: load().get() gives a visitorId', async () => {
  const {win} = load('fingerprint3.js');
  const agent = await win.FingerprintJS.load();
  const r = await agent.get();
  assert.strictEqual(r.visitorId.length, 32);
});

test('prebid-ads / anti-adblock bait', () => {
  const {win} = load('prebid-ads.js');
  assert.strictEqual(win.canRunAds, true); assert.strictEqual(win.isAdBlockActive, false);
});

test('fuckadblock family: onNotDetected fires, onDetected never', () => {
  for (const f of ['fuckadblock.js-3.2.0.js', 'nobab.js', 'nobab2.js', 'nofab.js']) {
    const {win, flush} = load(f);
    let not = 0, det = 0;
    win.blockAdBlock.onNotDetected(() => not++).onDetected(() => det++);
    win.blockAdBlock.check();
    flush();
    assert.ok(not >= 1, f); assert.strictEqual(det, 0, f);
  }
});

test('popads: any use of PopAds/popns is harmless', () => {
  const {win} = load('popads.js');
  win.PopAds.anything.at.all(1, 2).more;
  new win.popns();
  assert.ok(win.PopAds);
});

test('small analytics stubs define their objects', () => {
  load('scorecardresearch_beacon.js').win.COMSCORE.beacon({c1: '2'});
  const cb = load('chartbeat.js').win; cb.pSUPERFLY.activity(); cb.chartbeat.virtualPage('/x');
  const ob = load('outbrain-widget.js').win; ob.OBR.extern.reloadWidget();
  assert.strictEqual(load('doubleclick_instream_ad_status.js').win.google_ad_status, 1);
  const sa = load('sensors-analytics.js').win; sa.sensorsDataAnalytic201505.track('x'); sa.sa.login('u');
  assert.ok(load('nitropay_ads.js').win.nitroAds.loaded);
  assert.strictEqual(load('adthrive_abd.js').win.adthrive.abd, false);
  assert.ok(load('google-analytics_cx_api.js').win.cxApi);
  load('hd-main.js');
});

console.log(`${passed} passed, ${failures} failed`);
process.exit(failures ? 1 : 0);

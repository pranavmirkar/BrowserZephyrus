// Draws the same 64x64 picture the Zephyrus §6.5 browsertest draws in a
// document and in a dedicated worker, and answers with an FNV hash of the
// bytes getImageData() hands back.
//
// A service worker has no creating frame, so it cannot be handed a seed the way
// the other two contexts are; it fetches its own. If that path breaks, this
// answers with the true unperturbed bytes and the test's cross-origin
// comparison catches it.

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()));

self.addEventListener('message', (e) => {
  let answer;
  try {
    const oc = new OffscreenCanvas(64, 64);
    const x = oc.getContext('2d');
    x.fillStyle = '#4477aa'; x.fillRect(0, 0, 64, 64);
    x.fillStyle = '#f60';    x.fillRect(8, 8, 20, 20);
    x.fillStyle = '#0a3';    x.fillRect(30, 40, 25, 15);
    const d = x.getImageData(0, 0, 64, 64).data;
    let h = 2166136261 >>> 0;
    for (let i = 0; i < d.length; i++) {
      h ^= d[i]; h = Math.imul(h, 16777619) >>> 0;
    }
    answer = h.toString(16);
  } catch (err) {
    answer = 'ERR:' + err.message;
  }
  e.ports[0].postMessage(answer);
});

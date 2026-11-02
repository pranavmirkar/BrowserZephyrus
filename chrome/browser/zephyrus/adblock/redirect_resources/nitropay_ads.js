(function() {
  'use strict';
  var w = window;
  // NitroPay's ad loader: pages queue calls on `window.nitroAds`.
  var noop = function() {};
  w.nitroAds = w.nitroAds || {};
  w.nitroAds.loaded = true;
  w.nitroAds.createAd = function() { return Promise.resolve(null); };
  w.nitroAds.addUserToken = noop;
  w.nitroAds.removeAd = noop;
  w.nitroAds.queue = w.nitroAds.queue || [];
  w.nitroAds.siteId = w.nitroAds.siteId;
})();

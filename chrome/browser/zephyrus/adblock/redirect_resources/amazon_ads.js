(function() {
  'use strict';
  var w = window;
  if (w.amznads) { return; }
  // The older Amazon ad loader (aax2/amzn_ads.js); `amznads.asyncParams` is its
  // configuration and the callbacks are what pages wait on.
  var done = function(cb) { if (typeof cb === 'function') { setTimeout(cb, 1); } };
  w.amznads = {
    asyncParams: w.amznads && w.amznads.asyncParams,
    getAdsCallback: function(id, cb) { done(cb); },
    getAdsAsync: function(id, cb) { done(cb); },
    tryGetAdsAsync: function(id, cb) { done(cb); },
    updateAds: function() {},
    setTargeting: function() {},
    getTokens: function() { return {}; },
    getTargeting: function() { return {}; },
    getMoatTargeting: function() { return {}; },
    loaded: true
  };
  w.aax_getAds = function() {};
  w.amzn_assoc_ad_type = w.amzn_assoc_ad_type;
})();

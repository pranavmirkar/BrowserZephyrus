(function() {
  'use strict';
  var w = window;
  // Google Optimize's content-experiment API. No experiment is ever running.
  var cxApi = {
    NO_CHOSEN_VARIATION: -1,
    NOT_PARTICIPATING: -2,
    UNKNOWN: -3,
    getChosenVariation: function() { return -1; },
    setChosenVariation: function() {},
    chooseVariation: function() { return -1; },
    setAllowHash: function() {},
    setDomainName: function() {},
    setCookiePath: function() {},
    setUtmEnabled: function() {},
    getScriptUrl: function() { return ''; },
    load: function() {},
    addListener: function() {},
    canonicalizeUrl: function(u) { return u; }
  };
  w.cxApi = w.cxApi || cxApi;
  w.gaData = w.gaData || {};
})();

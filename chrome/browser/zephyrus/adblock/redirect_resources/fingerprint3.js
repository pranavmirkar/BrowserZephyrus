(function() {
  'use strict';
  var w = window;
  // FingerprintJS v3+: `FingerprintJS.load().then(agent => agent.get())`. One
  // constant visitor id for everyone.
  var ID = '00000000000000000000000000000000';
  var agent = {
    get: function() {
      return Promise.resolve({
        visitorId: ID, version: '3.0.0', confidence: {score: 0},
        components: {},
        visitorId_: ID
      });
    }
  };
  w.FingerprintJS = {
    load: function() { return Promise.resolve(agent); },
    hashComponents: function() { return ID; },
    componentsToDebugString: function() { return ''; },
    getFullscreenElement: function() { return null; },
    isMobile: function() { return false; }
  };
})();

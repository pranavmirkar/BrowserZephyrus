(function() {
  'use strict';
  var w = window;
  // Outbrain's recommendation widget: the page calls into `OBR.extern`.
  var noop = function() {};
  w.OBR = w.OBR || {};
  w.OBR.extern = w.OBR.extern || {};
  ['researchWidget', 'reloadWidget', 'renderSpaWidgets', 'refreshSpaWidget',
   'refreshWidget', 'setAdblockerCookie', 'marketplaceWidgetClick', 'closeAd',
   'showRecommendation', 'registerEvent'].forEach(function(name) {
    w.OBR.extern[name] = noop;
  });
  w.OBR.loaded = true;
})();

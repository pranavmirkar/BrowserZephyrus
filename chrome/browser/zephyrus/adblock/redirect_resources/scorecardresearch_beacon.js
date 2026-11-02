(function() {
  'use strict';
  var w = window;
  // comScore's beacon: pages call `COMSCORE.beacon({c1: '2', c2: '...'})`.
  w.COMSCORE = w.COMSCORE || {};
  w.COMSCORE.beacon = function() {};
  w.COMSCORE.purge = function() {};
  w.COMSCORE.bind = w.COMSCORE.bind || function() {};
  w.COMSCORE.isDDInstalled = true;
  w.COMSCORE.loaded = true;
  w._comscore = w._comscore || [];
})();

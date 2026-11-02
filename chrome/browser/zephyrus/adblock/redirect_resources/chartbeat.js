(function() {
  'use strict';
  var w = window;
  // Chartbeat's page-activity library; `pSUPERFLY` is the object pages call.
  var noop = function() {};
  w.pSUPERFLY = w.pSUPERFLY || {};
  w.pSUPERFLY.activity = noop;
  w.pSUPERFLY.virtualPage = noop;
  w.pSUPERFLY.manualPing = noop;
  w.pSUPERFLY.sf = noop;
  w._cbq = w._cbq || [];
  w.chartbeat = w.chartbeat || {};
  w.chartbeat.virtualPage = noop;
  w.chartbeat.setSections = noop;
  w.chartbeat.setAuthors = noop;
  w.chartbeat.setPath = noop;
  w.chartbeat.setTitle = noop;
})();

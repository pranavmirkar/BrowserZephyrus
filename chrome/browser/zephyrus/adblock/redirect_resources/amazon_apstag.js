(function() {
  'use strict';
  var w = window;
  if (w.apstag && w.apstag.__stub) { return; }
  var queued = (w.apstag && w.apstag._Q) || [];
  // Amazon's header-bidding client. Every bid request answers at once with no bids,
  // which is what a page does when Amazon did not respond.
  var apstag = {
    __stub: true,
    _Q: [],
    init: function(config, cb) { if (typeof cb === 'function') { setTimeout(cb, 1); } },
    fetchBids: function(config, cb) {
      if (typeof cb === 'function') { setTimeout(function() { cb([]); }, 1); }
    },
    setDisplayBids: function() {},
    targetingKeys: function() { return []; },
    renewId: function() {},
    deleteId: function() {},
    updateId: function() {},
    dpa: function() {},
    rpa: function() {},
    upa: function() {},
    thirdPartyData: function() {},
    debug: function() {}
  };
  w.apstag = apstag;
  for (var i = 0; i < queued.length; i++) {
    var item = queued[i];
    try {
      if (item && typeof apstag[item[0]] === 'function') {
        apstag[item[0]].apply(apstag, item[1] || []);
      }
    } catch (e) {}
  }
})();

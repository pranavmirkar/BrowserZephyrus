(function() {
  'use strict';
  var w = window;
  var dl = w.dataLayer = w.dataLayer || [];
  var gtm = w.google_tag_manager = w.google_tag_manager || {};
  if (gtm.__stub) { return; }
  gtm.__stub = true;

  // A page that hands GTM an `eventCallback` is waiting for the tags to be done
  // (typically before it navigates or submits). Nothing will ever run, so say so.
  function finish(item) {
    if (item && typeof item === 'object' && typeof item.eventCallback === 'function') {
      var cb = item.eventCallback;
      item.eventCallback = undefined;
      setTimeout(function() { try { cb(item['gtm.uniqueEventId'] || ''); } catch (e) {} }, 1);
    }
  }
  var push = dl.push;
  dl.push = function() {
    for (var i = 0; i < arguments.length; i++) { finish(arguments[i]); }
    return push.apply(dl, arguments);
  };
  for (var i = 0; i < dl.length; i++) { finish(dl[i]); }

  var model = {};
  gtm.dataLayer = {
    set: function(k, v) { model[k] = v; },
    get: function(k) { return model[k]; },
    reset: function() { model = {}; }
  };
  gtm.pix = gtm.pix || [];
  gtm.gtagInit = true;
})();

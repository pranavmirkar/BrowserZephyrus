(function() {
  'use strict';
  var w = window;
  // `(adsbygoogle = window.adsbygoogle || []).push({})` is how a page asks for an ad;
  // a page that checks `adsbygoogle.loaded` is asking whether the library came.
  var queue = w.adsbygoogle;
  if (queue && queue.loaded === true) { return; }
  var stub = {
    loaded: true,
    push: function() { return arguments.length; },
    pauseAdRequests: 0
  };
  if (Array.isArray(queue)) {
    queue.length = 0;
    queue.loaded = true;
    queue.push = stub.push;
  } else {
    w.adsbygoogle = stub;
  }
})();

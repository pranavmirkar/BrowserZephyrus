(function() {
  'use strict';
  var w = window;
  // `(adsbygoogle = window.adsbygoogle || []).push({})` is how a page asks for an ad;
  // a page that checks `adsbygoogle.loaded` is asking whether the library came.
  var queue = w.adsbygoogle;
  if (queue && queue.loaded === true) { return; }
  // The real library answers each push by taking the next ad slot it has not
  // handled yet and marking it: data-adsbygoogle-status="done", and
  // data-ad-status="unfilled" when there was no ad to show. Anti-adblock
  // checks read those marks, so a slot that was asked for and never marked
  // gives the stand-in away. "No ad to show" is an ordinary AdSense answer.
  function fill() {
    var doc = w.document;
    if (!doc || typeof doc.querySelectorAll !== 'function') { return; }
    var slots = doc.querySelectorAll('ins.adsbygoogle');
    for (var i = 0; i < slots.length; i++) {
      var slot = slots[i];
      if (!slot.hasAttribute('data-adsbygoogle-status')) {
        slot.setAttribute('data-adsbygoogle-status', 'done');
        slot.setAttribute('data-ad-status', 'unfilled');
        return;
      }
    }
  }
  function push() {
    for (var i = 0; i < arguments.length; i++) {
      var item = arguments[i];
      if (item && typeof item === 'object' && !item.google_ad_client &&
          !item.params && !item.element) {
        fill();
      } else if (item && item.element && item.element.setAttribute) {
        item.element.setAttribute('data-adsbygoogle-status', 'done');
        item.element.setAttribute('data-ad-status', 'unfilled');
      }
    }
    return arguments.length;
  }
  var stub = {
    loaded: true,
    push: push,
    pauseAdRequests: 0
  };
  if (Array.isArray(queue)) {
    var pending = queue.slice();
    queue.length = 0;
    queue.loaded = true;
    queue.push = push;
    for (var j = 0; j < pending.length; j++) { push(pending[j]); }
  } else {
    w.adsbygoogle = stub;
  }
})();

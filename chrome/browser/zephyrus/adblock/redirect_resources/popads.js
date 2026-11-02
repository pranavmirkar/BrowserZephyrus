(function() {
  'use strict';
  var w = window;
  // The ad network's own check: a page asks whether `PopAds` (or `popns`) loaded.
  // Whatever is called on these, and whatever is read from them, answers without
  // an error and with nothing.
  function inert() {
    var target = function() {};
    return new Proxy(target, {
      get: function(t, key) { return key === Symbol.toPrimitive ? function() { return ''; } : inert(); },
      apply: function() { return inert(); },
      construct: function() { return inert(); },
      set: function() { return true; }
    });
  }
  try {
    Object.defineProperty(w, 'PopAds', {value: inert(), configurable: true});
    Object.defineProperty(w, 'popns', {value: inert(), configurable: true});
  } catch (e) {}
})();

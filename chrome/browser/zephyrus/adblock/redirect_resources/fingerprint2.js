(function() {
  'use strict';
  var w = window;
  // Fingerprint2 (the browser-fingerprinting library) answers with one constant,
  // the same for every visitor, so the page gets a value and learns nothing.
  var HASH = '00000000000000000000000000000000';
  function reply(cb) {
    if (typeof cb === 'function') { setTimeout(function() { cb(HASH, []); }, 1); }
  }
  function Fingerprint2(options) { this.options = options || {}; }
  // `new Fingerprint2().get(cb)` and `Fingerprint2.get(options, cb)`
  Fingerprint2.prototype.get = function(options, cb) {
    reply(typeof options === 'function' ? options : cb);
  };
  Fingerprint2.prototype.getPromise = function() {
    return Promise.resolve({fingerprint: HASH, components: []});
  };
  Fingerprint2.get = function(options, cb) { reply(typeof options === 'function' ? options : cb); };
  Fingerprint2.getPromise = function() { return Promise.resolve([]); };
  Fingerprint2.getV18 = function(options, cb) { reply(typeof options === 'function' ? options : cb); };
  Fingerprint2.x64hash128 = function() { return HASH; };
  Fingerprint2.VERSION = '2.1.4';
  w.Fingerprint2 = Fingerprint2;
  w.Fingerprint = w.Fingerprint || Fingerprint2;
})();

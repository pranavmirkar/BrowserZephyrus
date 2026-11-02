(function() {
  'use strict';
  var w = window;
  // FuckAdBlock / BlockAdBlock: a detector that never detects anything.
  function Detector(options) {
    this._options = options || {};
    this._handlers = {detected: [], notDetected: []};
  }
  Detector.prototype.setOption = function(key, value) {
    if (typeof key === 'object') { for (var k in key) { this._options[k] = key[k]; } }
    else { this._options[key] = value; }
    return this;
  };
  Detector.prototype.check = function() {
    var handlers = this._handlers.notDetected.slice();
    setTimeout(function() {
      for (var i = 0; i < handlers.length; i++) { try { handlers[i](); } catch (e) {} }
    }, 1);
    return true;
  };
  Detector.prototype.emitEvent = function() { return this; };
  Detector.prototype.clearEvent = function() {
    this._handlers = {detected: [], notDetected: []};
    return this;
  };
  Detector.prototype.on = function(detected, fn) {
    if (typeof fn !== 'function') { return this; }
    if (detected) { this._handlers.detected.push(fn); return this; }
    this._handlers.notDetected.push(fn);
    setTimeout(function() { try { fn(); } catch (e) {} }, 1);
    return this;
  };
  Detector.prototype.onDetected = function(fn) { return this.on(true, fn); };
  Detector.prototype.onNotDetected = function(fn) { return this.on(false, fn); };
  w.FuckAdBlock = w.BlockAdBlock = Detector;
  if (!w.fuckAdBlock) { w.fuckAdBlock = new Detector(); }
  if (!w.blockAdBlock) { w.blockAdBlock = w.fuckAdBlock; }
})();

(function() {
  'use strict';
  var w = window;
  var name = w.GoogleAnalyticsObject || 'ga';
  var queued = (w[name] && w[name].q) || [];

  function Tracker(trackerName) { this._name = trackerName || 't0'; this._f = {}; }
  Tracker.prototype.get = function(k) { return this._f[k]; };
  Tracker.prototype.set = function(k, v) {
    if (k && typeof k === 'object') { for (var p in k) { this._f[p] = k[p]; } }
    else { this._f[k] = v; }
  };
  Tracker.prototype.send = function() {};
  var tracker = new Tracker();

  // The last argument of a command is either a callback (`ga(function(){...})`,
  // `ga('send', ..., {hitCallback: fn})`) that the page waits for.
  function ga() {
    var args = Array.prototype.slice.call(arguments);
    var first = args[0];
    if (typeof first === 'function') {
      try { first(tracker); } catch (e) {}
      return;
    }
    var last = args[args.length - 1];
    if (last && typeof last === 'object' && typeof last.hitCallback === 'function') {
      setTimeout(function() { try { last.hitCallback(); } catch (e) {} }, 1);
    }
  }
  ga.l = +new Date();
  ga.loaded = true;
  ga.create = function() { return tracker; };
  ga.remove = function() {};
  ga.getByName = function() { return tracker; };
  ga.getAll = function() { return [tracker]; };
  ga.answer = 42;
  w[name] = ga;
  w.gaplugins = w.gaplugins || {};
  w.gaGlobal = w.gaGlobal || {vid: '0.0'};

  for (var i = 0; i < queued.length; i++) {
    try { ga.apply(null, queued[i]); } catch (e) {}
  }
})();

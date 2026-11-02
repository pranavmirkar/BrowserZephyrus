(function() {
  'use strict';
  var w = window;
  w.googletag = w.googletag || {};
  var g = w.googletag;
  if (g.__stub) { return; }
  g.__stub = true;
  var pending = Array.isArray(g.cmd) ? g.cmd : [];

  function Chain() {}
  // Every method of a service or slot answers with the object itself, so that
  // chained calls (`defineSlot(...).addService(...).setTargeting(...)`) work.
  function chainable(target, names) {
    names.forEach(function(name) {
      target[name] = function() { return this; };
    });
  }

  var slots = [];
  function Slot(path, size, id) {
    this._path = path; this._size = size; this._id = id; this._targeting = {};
  }
  chainable(Slot.prototype, [
    'addService', 'clearCategoryExclusions', 'clearTargeting', 'defineSizeMapping',
    'setCategoryExclusion', 'setClickUrl', 'setCollapseEmptyDiv', 'setForceSafeFrame',
    'setSafeFrameConfig', 'setTargeting', 'updateTargetingFromMap', 'set', 'setConfig']);
  Slot.prototype.getSlotElementId = function() { return this._id; };
  Slot.prototype.getAdUnitPath = function() { return this._path; };
  Slot.prototype.getName = function() { return this._path; };
  Slot.prototype.getSizes = function() { return []; };
  Slot.prototype.getTargeting = function() { return []; };
  Slot.prototype.getTargetingKeys = function() { return []; };
  Slot.prototype.getCategoryExclusions = function() { return []; };
  Slot.prototype.getResponseInformation = function() { return null; };
  Slot.prototype.getServices = function() { return []; };
  Slot.prototype.getOutOfPage = function() { return false; };
  Slot.prototype.get = function() { return null; };

  var listeners = {};
  var pubads = {};
  chainable(pubads, [
    'addEventListener', 'clear', 'clearCategoryExclusions', 'clearTagForChildDirectedTreatment',
    'clearTargeting', 'collapseEmptyDivs', 'disableInitialLoad', 'enableAsyncRendering',
    'enableLazyLoad', 'enableSingleRequest', 'enableSyncRendering', 'enableVideoAds',
    'refresh', 'removeEventListener', 'set', 'setCategoryExclusion', 'setCentering',
    'setCookieOptions', 'setForceSafeFrame', 'setLocation', 'setPrivacySettings',
    'setPublisherProvidedId', 'setRequestNonPersonalizedAds', 'setSafeFrameConfig',
    'setTagForChildDirectedTreatment', 'setTagForUnderAgeOfConsent', 'setTargeting',
    'setVideoContent', 'updateCorrelator', 'display', 'definePassback']);
  pubads.addEventListener = function(type, fn) {
    (listeners[type] = listeners[type] || []).push(fn); return this;
  };
  pubads.getSlots = function() { return slots.slice(); };
  pubads.getSlotIdMap = function() { return {}; };
  pubads.get = function() { return null; };
  pubads.getAttributeKeys = function() { return []; };
  pubads.getTargeting = function() { return []; };
  pubads.getTargetingKeys = function() { return []; };
  pubads.isInitialLoadDisabled = function() { return false; };
  pubads.isSRA = function() { return false; };
  pubads.getVersion = function() { return '1'; };

  function define(path, size, id) {
    var slot = new Slot(path, size, id);
    slots.push(slot);
    return slot;
  }
  g.defineSlot = function(path, size, id) { return define(path, size, id); };
  g.defineOutOfPageSlot = function(path, id) { return define(path, [], id); };
  g.defineUnit = g.defineSlot;
  g.destroySlots = function() { slots.length = 0; return true; };
  g.display = function() {};
  g.enableServices = function() {};
  g.getVersion = function() { return '1'; };
  g.openConsole = function() {};
  g.pubads = function() { return pubads; };
  g.companionAds = function() {
    var c = {}; chainable(c, ['addEventListener', 'enableSyncLoading', 'setRefreshUnfilledSlots']);
    c.getSlots = function() { return []; }; return c;
  };
  g.content = function() { var c = {}; chainable(c, ['addEventListener', 'setContent']); return c; };
  g.sizeMapping = function() {
    var m = {addSize: function() { return m; }, build: function() { return []; }};
    return m;
  };
  g.setAdIframeTitle = function() {};
  g.apiReady = true;
  g.pubadsReady = true;
  g.evalScripts = function() {};
  g.encryptedSignalProviders = [];
  g.secureSignalProviders = {push: function() {}};
  g.config = {};
  g.enums = {
    OutOfPageFormat: {INTERSTITIAL: 5, REWARDED: 4, TOP_ANCHOR: 2, BOTTOM_ANCHOR: 3, LEFT_SIDE_RAIL: 6, RIGHT_SIDE_RAIL: 7},
    TrafficSource: {ORGANIC: 1, PURCHASED: 2}
  };

  function run(fn) {
    if (typeof fn !== 'function') { return; }
    try { fn.call(g); } catch (e) {}
  }
  // The command queue pages feed before and after the library "loads".
  g.cmd = {push: function() {
    for (var i = 0; i < arguments.length; i++) { run(arguments[i]); }
    return arguments.length;
  }};
  for (var i = 0; i < pending.length; i++) { run(pending[i]); }
})();

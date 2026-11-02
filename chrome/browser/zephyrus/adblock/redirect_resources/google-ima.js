(function() {
  'use strict';
  var w = window;
  w.google = w.google || {};
  if (w.google.ima && w.google.ima.__stub) { return; }
  var ima = {};
  ima.__stub = true;
  ima.VERSION = '3.0.0';

  // A tiny event target, shared by everything that emits events.
  function Emitter() { this._l = {}; }
  Emitter.prototype.addEventListener = function(type, fn, capture, ctx) {
    if (typeof fn !== 'function') { return; }
    (this._l[type] = this._l[type] || []).push({fn: fn, ctx: ctx});
  };
  Emitter.prototype.removeEventListener = function(type, fn) {
    var list = this._l[type] || [];
    for (var i = list.length - 1; i >= 0; i--) {
      if (list[i].fn === fn) { list.splice(i, 1); }
    }
  };
  Emitter.prototype._emit = function(event) {
    var list = (this._l[event.type] || []).slice();
    for (var i = 0; i < list.length; i++) {
      try { list[i].fn.call(list[i].ctx || this, event); } catch (e) {}
    }
  };
  function later(fn) { setTimeout(fn, 0); }

  function AdEvent(type, ad, adData) {
    this.type = type; this._ad = ad || null; this.adData = adData || {};
  }
  AdEvent.prototype.getAd = function() { return this._ad; };
  AdEvent.prototype.getAdData = function() { return this.adData; };
  AdEvent.Type = {
    AD_BREAK_READY: 'adBreakReady', AD_BUFFERING: 'adBuffering',
    AD_CAN_PLAY: 'adCanPlay', AD_METADATA: 'adMetadata',
    AD_PROGRESS: 'adProgress', ALL_ADS_COMPLETED: 'allAdsCompleted',
    CLICK: 'click', COMPLETE: 'complete',
    CONTENT_PAUSE_REQUESTED: 'contentPauseRequested',
    CONTENT_RESUME_REQUESTED: 'contentResumeRequested',
    DURATION_CHANGE: 'durationChange', EXPANDED_CHANGED: 'expandedChanged',
    FIRST_QUARTILE: 'firstQuartile', IMPRESSION: 'impression',
    INTERACTION: 'interaction', LINEAR_CHANGED: 'linearChanged',
    LOADED: 'loaded', LOG: 'log', MIDPOINT: 'midpoint', PAUSED: 'pause',
    RESUMED: 'resume', SKIPPABLE_STATE_CHANGED: 'skippableStateChanged',
    SKIPPED: 'skip', STARTED: 'start', THIRD_QUARTILE: 'thirdQuartile',
    USER_CLOSE: 'userClose', VIDEO_CLICKED: 'videoClicked',
    VIDEO_ICON_CLICKED: 'videoIconClicked', VOLUME_CHANGED: 'volumeChange',
    VOLUME_MUTED: 'mute'
  };

  function AdError(type, code, message) {
    this._type = type; this._code = code; this._message = message;
  }
  AdError.prototype.getErrorCode = function() { return this._code; };
  AdError.prototype.getInnerError = function() { return null; };
  AdError.prototype.getMessage = function() { return this._message; };
  AdError.prototype.getType = function() { return this._type; };
  AdError.prototype.getVastErrorCode = function() { return 900; };
  AdError.prototype.toString = function() {
    return 'AdError ' + this._code + ': ' + this._message;
  };
  AdError.Type = {AD_LOAD: 'adLoadError', AD_PLAY: 'adPlayError'};
  AdError.ErrorCode = {
    VIDEO_PLAY_ERROR: 400, FAILED_TO_REQUEST_ADS: 1005,
    REQUIRED_LISTENERS_NOT_ADDED: 900, VAST_LOAD_TIMEOUT: 301,
    VAST_NO_ADS_AFTER_WRAPPER: 303, VAST_MEDIA_LOAD_TIMEOUT: 402,
    VAST_TOO_MANY_REDIRECTS: 302, VAST_ASSET_MISMATCH: 403,
    VAST_LINEAR_ASSET_MISMATCH: 403, VAST_NONLINEAR_ASSET_MISMATCH: 503,
    VAST_ASSET_NOT_FOUND: 1007, VAST_UNSUPPORTED_VERSION: 102,
    VAST_SCHEMA_VALIDATION_ERROR: 101, VAST_TRAFFICKING_ERROR: 200,
    VAST_UNEXPECTED_LINEARITY: 201, VAST_UNEXPECTED_DURATION_ERROR: 202,
    VAST_WRAPPER_ERROR: 300, NONLINEAR_DIMENSIONS_ERROR: 501,
    COMPANION_REQUIRED_ERROR: 602, VAST_EMPTY_RESPONSE: 1009,
    UNSUPPORTED_LOCALE: 1011, INVALID_ADX_EXTENSION: 1105,
    INVALID_ARGUMENTS: 1101, UNKNOWN_AD_RESPONSE: 1010,
    UNKNOWN_ERROR: 900, OVERLAY_AD_PLAYING_FAILED: 500,
    VIDEO_ELEMENT_USED: -1, VIDEO_ELEMENT_REQUIRED: -1,
    VAST_MEDIA_ERROR: -1, ADSLOT_NOT_VISIBLE: -1,
    OVERLAY_AD_LOADING_FAILED: -1, VAST_MALFORMED_RESPONSE: -1,
    COMPANION_AD_LOADING_FAILED: -1
  };
  function AdErrorEvent(error, ctx) {
    this.type = 'adError'; this._error = error; this._ctx = ctx;
  }
  AdErrorEvent.prototype.getError = function() { return this._error; };
  AdErrorEvent.prototype.getUserRequestContext = function() { return this._ctx; };
  AdErrorEvent.Type = {AD_ERROR: 'adError'};

  function Ad() {}
  var adGetters = {
    getAdId: '', getCreativeId: '', getCreativeAdId: '', getAdSystem: '',
    getAdvertiserName: '', getApiFramework: null, getContentType: '',
    getDescription: '', getTitle: '', getDuration: -1, getHeight: 0,
    getWidth: 0, getVastMediaHeight: 0, getVastMediaWidth: 0,
    getVastMediaBitrate: 0, getMinSuggestedDuration: -1, getSkipTimeOffset: -1,
    getTraffickingParameters: {}, getTraffickingParametersString: '',
    getUiElements: [], getWrapperAdIds: [], getWrapperAdSystems: [],
    getWrapperCreativeIds: [], getUniversalAdIds: [], getCompanionAds: [],
    getAdPodInfo: null, getSurveyUrl: null, getDealId: '', isLinear: true,
    isSkippable: false, isUiDisabled: false, getMediaUrl: null
  };
  Object.keys(adGetters).forEach(function(name) {
    Ad.prototype[name] = function() { return adGetters[name]; };
  });
  ima.Ad = Ad;

  function AdsManager() { Emitter.call(this); this._volume = 1; }
  AdsManager.prototype = Object.create(Emitter.prototype);
  ['init', 'resize', 'pause', 'resume', 'stop', 'skip', 'expand', 'collapse',
   'focus', 'discardAdBreak', 'updateAdsRenderingSettings', 'clicked',
   'configureAdsManager', 'requestNextAdBreak'].forEach(function(name) {
    AdsManager.prototype[name] = function() {};
  });
  AdsManager.prototype.start = function() {
    var self = this, ad = new Ad();
    later(function() {
      self._emit(new AdEvent(AdEvent.Type.LOADED, ad));
      self._emit(new AdEvent(AdEvent.Type.CONTENT_RESUME_REQUESTED, ad));
      self._emit(new AdEvent(AdEvent.Type.ALL_ADS_COMPLETED, ad));
    });
  };
  AdsManager.prototype.destroy = function() { this._l = {}; };
  AdsManager.prototype.getVolume = function() { return this._volume; };
  AdsManager.prototype.setVolume = function(v) { this._volume = v; };
  AdsManager.prototype.getRemainingTime = function() { return 0; };
  AdsManager.prototype.getCuePoints = function() { return []; };
  AdsManager.prototype.getAdSkippableState = function() { return false; };
  AdsManager.prototype.isCustomPlaybackUsed = function() { return false; };
  AdsManager.prototype.isCustomClickTrackingUsed = function() { return false; };
  AdsManager.prototype.getCurrentAd = function() { return null; };
  ima.AdsManager = AdsManager;

  function AdsManagerLoadedEvent(manager, ctx) {
    this.type = 'adsManagerLoaded'; this._m = manager; this._ctx = ctx;
  }
  AdsManagerLoadedEvent.prototype.getAdsManager = function() { return this._m; };
  AdsManagerLoadedEvent.prototype.getUserRequestContext = function() {
    return this._ctx;
  };
  AdsManagerLoadedEvent.Type = {ADS_MANAGER_LOADED: 'adsManagerLoaded'};
  ima.AdsManagerLoadedEvent = AdsManagerLoadedEvent;

  function AdsLoader(container) { Emitter.call(this); this._c = container; }
  AdsLoader.prototype = Object.create(Emitter.prototype);
  AdsLoader.prototype.requestAds = function(request, ctx) {
    var self = this;
    later(function() {
      self._emit(new AdsManagerLoadedEvent(new AdsManager(), ctx));
    });
  };
  AdsLoader.prototype.contentComplete = function() {};
  AdsLoader.prototype.destroy = function() { this._l = {}; };
  AdsLoader.prototype.getSettings = function() { return ima.settings; };
  AdsLoader.prototype.getVersion = function() { return ima.VERSION; };
  ima.AdsLoader = AdsLoader;

  function AdDisplayContainer(element, video) { this._e = element; this._v = video; }
  AdDisplayContainer.prototype.initialize = function() {};
  AdDisplayContainer.prototype.destroy = function() {};
  ima.AdDisplayContainer = AdDisplayContainer;

  function AdsRequest() {}
  ['setAdWillAutoPlay', 'setAdWillPlayMuted', 'setContinuousPlayback'].forEach(
      function(name) { AdsRequest.prototype[name] = function() {}; });
  ima.AdsRequest = AdsRequest;

  function AdsRenderingSettings() {}
  ima.AdsRenderingSettings = AdsRenderingSettings;

  function ImaSdkSettings() { this._p = ''; this._pv = ''; this._l = 'en'; }
  ['setAutoPlayAdBreaks', 'setCompanionBackfill', 'setDisableCustomPlaybackForIOS10Plus',
   'setDisableFlashAds', 'setFeatureFlags', 'setNumRedirects', 'setPpid',
   'setSessionId', 'setVpaidAllowed', 'setVpaidMode', 'setCookiesEnabled',
   'setLocation', 'setPlayerType', 'setPlayerVersion', 'setLocale'
  ].forEach(function(name) { ImaSdkSettings.prototype[name] = function() {}; });
  ImaSdkSettings.prototype.getCompanionBackfill = function() { return 'always'; };
  ImaSdkSettings.prototype.getDisableCustomPlaybackForIOS10Plus = function() { return false; };
  ImaSdkSettings.prototype.getLocale = function() { return 'en'; };
  ImaSdkSettings.prototype.getNumRedirects = function() { return 4; };
  ImaSdkSettings.prototype.getPlayerType = function() { return this._p; };
  ImaSdkSettings.prototype.getPlayerVersion = function() { return this._pv; };
  ImaSdkSettings.prototype.getPpid = function() { return ''; };
  ImaSdkSettings.prototype.getVpaidMode = function() { return 1; };
  ImaSdkSettings.CompanionBackfillMode = {ALWAYS: 'always', ON_MASTER_AD: 'on_master_ad'};
  ImaSdkSettings.VpaidMode = {DISABLED: 0, ENABLED: 1, INSECURE: 2};
  ima.ImaSdkSettings = ImaSdkSettings;
  ima.settings = new ImaSdkSettings();

  ima.AdEvent = AdEvent;
  ima.AdError = AdError;
  ima.AdErrorEvent = AdErrorEvent;
  ima.ViewMode = {FULLSCREEN: 'fullscreen', NORMAL: 'normal'};
  ima.UiElements = {AD_ATTRIBUTION: 'adAttribution', COUNTDOWN: 'countdown'};
  ima.OmidAccessMode = {DOMAIN: 'domain', FULL: 'full', LIMITED: 'limited'};
  ima.OmidVerificationVendor = {OTHER: 1, MOAT: 2, DOUBLEVERIFY: 3, INTEGRAL_AD_SCIENCE: 4,
                                PIXELATE: 5, NIELSEN: 6, COMSCORE: 7, MEETRICS: 8,
                                GOOGLE: 9};
  ima.CompanionAdSelectionSettings = function() {};
  ima.CompanionAdSelectionSettings.CreativeType = {ALL: 'All', FLASH: 'Flash', IMAGE: 'Image'};
  ima.CompanionAdSelectionSettings.ResourceType = {ALL: 'All', HTML: 'Html', IFRAME: 'IFrame', STATIC: 'Static'};
  ima.CompanionAdSelectionSettings.SizeCriteria = {IGNORE: 'IgnoreSize', SELECT_EXACT_MATCH: 'SelectExactMatch', SELECT_NEAR_MATCH: 'SelectNearMatch'};
  ima.AdCuePoints = function() {};
  ima.AdPodInfo = function() {};
  ima.dai = {api: {}};
  ima.dai.api.StreamEvent = {Type: {}};

  w.google.ima = ima;
})();

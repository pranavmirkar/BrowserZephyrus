(function() {
  'use strict';
  var w = window;
  // The classic async queue: `_gaq.push(['_trackPageview'])`, and functions to run.
  function Tracker() {}
  ['_trackEvent', '_trackPageview', '_trackTrans', '_trackSocial', '_trackTiming',
   '_addTrans', '_addItem', '_addIgnoredOrganic', '_addIgnoredRef', '_addOrganic',
   '_clearIgnoredOrganic', '_clearIgnoredRef', '_clearOrganic', '_cookiePathCopy',
   '_deleteCustomVar', '_link', '_linkByPost', '_setAccount', '_setAllowAnchor',
   '_setAllowHash', '_setAllowLinker', '_setCampContentKey', '_setCampMediumKey',
   '_setCampNameKey', '_setCampNOKey', '_setCampSourceKey', '_setCampTermKey',
   '_setCampaignCookieTimeout', '_setCampaignTrack', '_setClientInfo', '_setCookiePath',
   '_setCookiePersistence', '_setCustomVar', '_setDetectFlash', '_setDetectTitle',
   '_setDomainName', '_setLocalGifPath', '_setLocalRemoteServerMode',
   '_setLocalServerMode', '_setReferrerOverride', '_setRemoteServerMode',
   '_setSampleRate', '_setSessionCookieTimeout', '_setSiteSpeedSampleRate',
   '_setVar', '_setVisitorCookieTimeout', '_trackPageLoadTime', '_initData',
   '_anonymizeIp', '_forceSSL', '_require', '_setCampaignCookieTimeout'
  ].forEach(function(n) { Tracker.prototype[n] = function() {}; });
  Tracker.prototype._getAccount = function() { return ''; };
  Tracker.prototype._getVisitorCustomVar = function() { return ''; };
  Tracker.prototype._getLinkerUrl = function(url) { return url; };
  Tracker.prototype._visitCode = function() { return 0; };
  var tracker = new Tracker();

  var gat = {
    _getTracker: function() { return tracker; },
    _createTracker: function() { return tracker; },
    _getTrackerByName: function() { return tracker; },
    _anonymizeIp: function() {}
  };
  w._gat = gat;

  function run(cmd) {
    try {
      if (typeof cmd === 'function') { cmd(); return; }
      if (Array.isArray(cmd) && typeof cmd[0] === 'string') {
        var fn = tracker[cmd[0].replace(/^[^.]*\./, '')];
        if (typeof fn === 'function') { fn.apply(tracker, cmd.slice(1)); }
      }
    } catch (e) {}
  }
  var queued = Array.isArray(w._gaq) ? w._gaq : [];
  w._gaq = {push: function() {
    for (var i = 0; i < arguments.length; i++) { run(arguments[i]); }
    return arguments.length;
  }};
  for (var i = 0; i < queued.length; i++) { run(queued[i]); }
})();

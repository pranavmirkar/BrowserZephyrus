(function() {
  'use strict';
  var w = window;
  // Sensors Analytics: `sensorsDataAnalytic201505` receives init/track/login calls.
  var noop = function() {};
  var sa = w.sensorsDataAnalytic201505 || {};
  ['init', 'track', 'trackSignup', 'login', 'logout', 'identify', 'quick', 'use',
   'registerPage', 'clearAllRegister', 'setProfile', 'setOnceProfile', 'appendProfile',
   'incrementProfile', 'deleteProfile', 'unsetProfile', 'bind', 'unbind', 'trackLink',
   'setPresetProperties', 'registerSuperProperties', 'clearPageRegister'
  ].forEach(function(name) { sa[name] = noop; });
  sa.getDistinctId = function() { return ''; };
  sa.getPresetProperties = function() { return {}; };
  sa.loaded = true;
  w.sensorsDataAnalytic201505 = sa;
  w.sa = w.sa || sa;
})();

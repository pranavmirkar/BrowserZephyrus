(function() {
  'use strict';
  // Adblock Analytics' detection bait (adblockanalytics.com/google-adsense.js,
  // used by detectadblock.com and the sites that embed its check). The real
  // file does one thing: it adds a hidden element with a fixed id, and the page
  // decides "no ad blocker" when that element exists. Answered here with the
  // same element and nothing else, so the vendor is never contacted and the
  // page sees what it would without a blocker. The prebid-ads.js flags are set
  // as well, for checks that look for those instead.
  var d = window.document;
  window.canRunAds = true;
  window.isAdBlockActive = false;
  if (!d) { return; }
  function add() {
    if (!d.body || d.getElementById('nqoybpxnanylgvpfpbz')) { return; }
    var e = d.createElement('div');
    e.id = 'nqoybpxnanylgvpfpbz';
    e.style.display = 'none';
    d.body.appendChild(e);
  }
  if (d.body) { add(); } else { d.addEventListener('DOMContentLoaded', add); }
})();

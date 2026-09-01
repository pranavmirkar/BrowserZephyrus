// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_ID_RESET_H_
#define CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_ID_RESET_H_

class PrefService;

namespace zephyrus_privacy {

// "Auto reset browser IDs": on every launch, discard the identifiers that
// would otherwise persist for the life of the installation, so a site or a
// service cannot use them to recognise this copy of the browser across
// sessions.
//
// WHAT THIS DOES AND DOES NOT BUY YOU
// -----------------------------------
// It resets identifiers the BROWSER stores about itself. It does not make the
// user anonymous: the IP address, the cookie jar, the TLS session cache, the
// window size and the font list are all untouched and all far stronger
// identifiers than anything reset here. Anyone describing this as "like a
// fresh installation every launch" is overselling it, and the UI string should
// not repeat that claim.
//
// The honest framing is narrower and still worth having: these are stable IDs
// with no user-visible function in this build, so discarding them costs
// nothing and removes a linkage that would otherwise be free to whoever can
// read it.
//
// IDENTIFIERS DELIBERATELY NOT RESET
// ----------------------------------
// "zephyrus.privacy.lookup_key" -- the OSCrypt-sealed HMAC key that seals the
// Privacy Intelligence lookup columns. It IS a per-profile secret, so it looks
// like it belongs on the list. It must not be: privacy_crypto_impl.cc treats a
// key that no longer unseals as "rows exist that can no longer be read", so
// rotating it each launch would silently destroy the user's entire tracker
// history while the dashboard kept reporting success. A privacy feature that
// erases the privacy feature's data is not a trade worth making silently, and
// there is a test asserting this key is left alone.
//
// The §6.5 fingerprint session secret is also absent, for the opposite reason:
// FingerprintSeedProvider generates it once per BrowserContext and never
// rotates it within a session, so it is already new on every launch. Adding it
// here would be a no-op that looked like a feature.
//
// Resetting is done by CLEARING the pref, never by writing a fresh value. The
// component that owns each identifier regenerates it in its own format and
// range on next use; inventing values here would risk writing something the
// owner cannot parse.

// The toggle. Mirrored as a bare literal in browser_prefs.cc, which registers
// it -- see the note there about the //chrome/browser -> zephyrus layering.
inline constexpr char kAutoResetIdsPref[] = "zephyrus.privacy.auto_reset_ids";

// True when the user has left the feature on (it ships on).
bool IsAutoResetIdsEnabled(const PrefService* local_state);

// Discards the browser-wide identifiers. Call once per launch, before anything
// reads them -- BrowserProcessImpl's pref-migration point, which already runs
// at exactly that moment.
void MaybeResetLocalStateIdentifiers(PrefService* local_state);

// Discards the per-profile identifiers. Call once per profile load, from
// ProfileImpl's pref-migration point. `local_state` supplies the toggle, which
// is browser-wide rather than per-profile.
void MaybeResetProfileIdentifiers(PrefService* profile_prefs,
                                  const PrefService* local_state);

}  // namespace zephyrus_privacy

#endif  // CHROME_BROWSER_ZEPHYRUS_PRIVACY_ZEPHYRUS_ID_RESET_H_

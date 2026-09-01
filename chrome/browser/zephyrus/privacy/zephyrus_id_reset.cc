// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/privacy/zephyrus_id_reset.h"

#include <string_view>

#include "base/containers/span.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/enterprise/browser/identifiers/identifiers_prefs.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/ukm/ukm_pref_names.h"

namespace zephyrus_privacy {

namespace {

// The lists live inside the functions rather than at file scope on purpose:
// several of these pref names are `extern const char[]` rather than constexpr,
// so a file-scope array of pointers to them would need dynamic initialisation
// -- a static initialiser, which Chromium does not allow.

void ClearAll(PrefService* prefs, base::span<const std::string_view> names) {
  for (std::string_view name : names) {
    // ClearPref on an unregistered pref CHECKs. Every name here is registered
    // by its owning component in this build, but a rebase can rename or delete
    // one, and taking the browser down during startup is a bad way to discover
    // that -- the user could not even launch far enough to switch the feature
    // off. Skipping an absent pref degrades to "one identifier not reset".
    if (prefs->FindPreference(name)) {
      prefs->ClearPref(name);
    }
  }
}

}  // namespace

bool IsAutoResetIdsEnabled(const PrefService* local_state) {
  return local_state && local_state->GetBoolean(kAutoResetIdsPref);
}

void MaybeResetLocalStateIdentifiers(PrefService* local_state) {
  if (!IsAutoResetIdsEnabled(local_state)) {
    return;
  }

  // Browser-wide identifiers, all in local state.
  //
  // MEASURED on a real install (Local State, 151.0.7922.171, 2026-08-25),
  // so this list is not guesswork:
  //
  //   limited_entropy_randomization_source  F226BCCA...169B  <- 128-bit, LIVE
  //   low_entropy_source3                   882              <- LIVE
  //   pseudo_low_entropy_source             6601             <- LIVE
  //   machine_id                            8682069          <- LIVE
  //   client_id2 / provisional_client_id    absent (metrics off)
  //   ukm client id                         absent (UKM off)
  //
  // The randomization source is the one that matters most: 128 bits is enough
  // to be unique on its own, and it sat in plaintext across every launch.
  //
  // The absent three stay on the list anyway. "Off today" is runtime state --
  // they start being written the moment metrics consent flips -- and clearing
  // a pref that does not exist costs a single FindPreference call.
  const std::string_view kLocalStateIdentifiers[] = {
      // The metrics client ID: a GUID identifying this installation.
      metrics::prefs::kMetricsClientID,
      // Held while consent is undecided and promoted to the real client ID if
      // consent is later given -- leaving it would restore the very ID that
      // clearing the line above removed.
      metrics::prefs::kMetricsProvisionalClientID,
      // Field-trial randomisation sources. Low entropy by design, but stable:
      // a browser landing in the same trial buckets every launch is
      // recognisable by exactly those buckets.
      metrics::prefs::kMetricsLowEntropySource,
      metrics::prefs::kMetricsOldLowEntropySource,
      metrics::prefs::kMetricsPseudoLowEntropySource,
      metrics::prefs::kMetricsLimitedEntropyRandomizationSource,
      // A hash of machine characteristics, used to notice a profile that has
      // been copied to another machine.
      metrics::prefs::kMetricsMachineId,
      // UKM's own client ID, separate from the metrics one above.
      ukm::prefs::kUkmClientId,
      // The autofill ablation seed: a stable random string assigning this
      // browser to autofill experiment groups. Found populated
      // ("JRL6oAW/2fQ=") in the same real Local State measured above, with no
      // user-visible function -- exactly the category this feature is for.
      // Registered in BOTH local state and profile prefs, so it appears in the
      // profile list too.
      autofill::prefs::kAutofillAblationSeedPref,
  };
  ClearAll(local_state, kLocalStateIdentifiers);
}

void MaybeResetProfileIdentifiers(PrefService* profile_prefs,
                                  const PrefService* local_state) {
  if (!profile_prefs || !IsAutoResetIdsEnabled(local_state)) {
    return;
  }

  const std::string_view kProfileIdentifiers[] = {
      // The enterprise profile GUID; ProfileIdService regenerates on demand.
      enterprise::kProfileGUIDPref,
      // See the note in the local-state list: autofill registers this pref in
      // both services, and clearing only one would leave the other as a stable
      // ID.
      autofill::prefs::kAutofillAblationSeedPref,
  };
  ClearAll(profile_prefs, kProfileIdentifiers);
}

}  // namespace zephyrus_privacy

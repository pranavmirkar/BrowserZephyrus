// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/translate/core/common/translate_features.h"

namespace translate {

BASE_FEATURE(kEnableTranslatePdf, base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTranslateSimplifiedHindi, base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTranslateLanguageSearchUI, base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTranslateElementExperimentFeatures,
             base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<std::string> kTranslateElementExperimentFeaturesParam{
    &kTranslateElementExperimentFeatures, "ef", ""};

BASE_FEATURE(kTranslateElementRegionalization,
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPartialTranslateUseOnePlatformApi,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Zephyrus: the translate stack makes two contacts with Google. One is the
// element.js fetch, which happens only after the user clicks Translate -- that
// is the deal a user accepts by asking for a translation, and it stays.
//
// This is the other one. TranslateLanguageList::GetSupportedLanguages() calls
// RequestLanguageList() as a side effect of merely being ASKED which languages
// exist, which happens on language-settings reads and pref changes, with no
// user intent to translate anything. It sends a request to
// translate.googleapis.com/translate_a/l carrying the UI locale, and an empty
// API key does NOT stop it -- the key is appended to the URL, not checked
// before sending (contrast the optimization-guide gate, which is a genuine
// precondition).
//
// Turning it off costs the roster refresh only: TranslateLanguageList is
// constructed with the compiled-in kDefaultSupportedLanguages, and the server
// list merely overrides it. Translation itself is unaffected -- the languages
// Google supports change on a scale of years, and a stale roster degrades to
// "a newly added language is missing until the next Chromium rebase", not to
// a broken feature.
BASE_FEATURE(kTranslateLanguageListFetch, base::FEATURE_DISABLED_BY_DEFAULT);

}  // namespace translate

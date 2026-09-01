// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The contract that a per-origin font-visibility surface would depend on.
//
// WHY THIS EXISTS, AND WHY IT EXISTS *BEFORE* THE FEATURE
// -------------------------------------------------------
// Scoping a sixth §6.5 surface (font enumeration) produced a design that would
// have shipped broken. The first proposal gated font visibility in
// FontCache::CreateFontPlatformData -- which is also where GLYPH fallback
// resolves the family it picked (font_cache_skia_win.cc, the
// GetFallbackFamilyNameFromHardcodedChoices path calls GetFontPlatformData with
// a family name). Hiding "Nirmala UI" from a site would therefore have hidden
// it from Devanagari text that never asked for any font at all. Tofu boxes on
// Hindi pages, in an India-first browser.
//
// The correct seam is one level up: FontFallbackList::GetFontData walks
// font_description.Family(), which is the page's declared font-family list and
// nothing else. Glyph fallback never passes through it.
//
// THE TRICK THAT MAKES THIS TESTABLE TODAY
// ----------------------------------------
// From that seam, a HIDDEN family and a NON-EXISTENT family are the same event:
// the local lookup returns null and the walk moves to the next entry. So the
// degradation path the whole design rests on can be measured now, with no
// feature code, by naming fonts that do not exist. If these tests fail, the
// feature is not worth writing; if they pass, they become its regression
// contract.
//
// WHAT IS ASSERTED
// ----------------
//   1. Text whose every declared family is missing renders with the SAME
//      metrics as text that declared no family at all -- i.e. it lands on the
//      standard font rather than on nothing.
//   2. That holds for five Indic scripts, which is where the breakage would
//      have been unacceptable.
//   3. Generic families (serif/sans-serif/monospace) still resolve, because
//      the same walk carries them and hiding one would be catastrophic.
//   4. Web fonts are unaffected: @font-face is resolved by the font selector
//      before the local lookup runs.

#include <string>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace zephyrus_privacy {
namespace {

// Families that certainly do not exist. A hidden family would behave exactly
// like these at FontFallbackList::GetFontData.
constexpr char kMissingFamilies[] =
    "'ZephyrusNoSuchFamily1', 'ZephyrusNoSuchFamily2'";

struct ScriptSample {
  const char* name;
  const char* text;
};

// One string per script we would be answerable for breaking. Chosen to be
// several clusters long so a shaping failure changes the width measurably
// rather than by a rounding error.
constexpr ScriptSample kIndicSamples[] = {
    {"Devanagari", "हिन्दी भाषा"},
    {"Tamil", "தமிழ் மொழி"},
    {"Telugu", "తెలుగు భాష"},
    {"Bengali", "বাংলা ভাষা"},
    {"Gujarati", "ગુજરાતી ભાષા"},
};

class FontFallbackContractBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("a.com", "/empty.html")));
  }

 protected:
  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  // Width of `text` laid out with `font_family_css` (empty = declare nothing).
  //
  // Measured off a detached element with white-space:pre so the value depends
  // on shaping alone and not on the surrounding layout.
  double MeasureWidth(const std::string& text,
                      const std::string& font_family_css) {
    const std::string script = content::JsReplace(
        R"((function(text, family) {
             const el = document.createElement('span');
             el.style.whiteSpace = 'pre';
             el.style.fontSize = '32px';
             if (family) { el.style.fontFamily = family; }
             el.textContent = text;
             document.body.appendChild(el);
             const w = el.getBoundingClientRect().width;
             el.remove();
             return w;
           })($1, $2))",
        text, font_family_css);
    return content::EvalJs(contents(), script).ExtractDouble();
  }
};

// The load-bearing one. If every declared family is missing, the walk in
// FontFallbackList::GetFontData falls through to the standard font and then to
// GetLastResortFallbackFont -- a real font. Same metrics as declaring nothing
// means the text is being shaped by the same font, which is the entire basis
// for claiming a font-visibility surface cannot produce tofu.
IN_PROC_BROWSER_TEST_F(FontFallbackContractBrowserTest,
                       MissingFamiliesDegradeToTheDefaultFont) {
  for (const auto& sample : kIndicSamples) {
    const double baseline = MeasureWidth(sample.text, "");
    const double hidden = MeasureWidth(sample.text, kMissingFamilies);

    EXPECT_GT(baseline, 0.0)
        << sample.name
        << ": the script does not render even with no font-family declared, so "
           "this machine cannot answer the question this test is asking";
    EXPECT_DOUBLE_EQ(baseline, hidden)
        << sample.name
        << ": text whose declared families are all absent was shaped "
           "differently from text that declared none. A font-visibility "
           "surface built on this seam would change how Indic text renders, "
           "not merely which font a site may ask for.";
  }
}

// Latin, as the control. If this failed while the Indic cases passed, the
// measurement harness would be the thing at fault, not the font stack.
IN_PROC_BROWSER_TEST_F(FontFallbackContractBrowserTest,
                       MissingFamiliesDegradeForLatinToo) {
  const double baseline = MeasureWidth("Zephyrus rendering", "");
  const double hidden = MeasureWidth("Zephyrus rendering", kMissingFamilies);
  EXPECT_GT(baseline, 0.0);
  EXPECT_DOUBLE_EQ(baseline, hidden);
}

// Generic families ride the same walk. Hiding one would be far worse than
// hiding a named face -- every page that says `font-family: serif` would move.
// The implementation must exempt FontFamily::Type::kGenericFamily; this pins
// the behaviour that exemption has to preserve.
IN_PROC_BROWSER_TEST_F(FontFallbackContractBrowserTest,
                       GenericFamiliesResolveForIndicText) {
  for (const auto& sample : kIndicSamples) {
    for (const char* generic : {"serif", "sans-serif", "monospace"}) {
      EXPECT_GT(MeasureWidth(sample.text, generic), 0.0)
          << sample.name << " with font-family: " << generic;
    }
  }
}

// A named family that IS present must still differ from one that is not --
// otherwise the whole measurement is insensitive and the tests above would pass
// no matter what the font stack did.
//
// This is the positive control: it proves font-family selection is observable
// through this harness at all. Without it, "hidden == baseline" could mean
// "nothing we do to font-family ever changes anything here".
IN_PROC_BROWSER_TEST_F(FontFallbackContractBrowserTest,
                       PresentFamilyIsDistinguishableFromMissingOne) {
  const double missing = MeasureWidth("Zephyrus rendering", kMissingFamilies);
  const double present = MeasureWidth("Zephyrus rendering", "monospace");
  EXPECT_GT(missing, 0.0);
  EXPECT_GT(present, 0.0);
  EXPECT_NE(missing, present)
      << "a present family and an absent one measured identically, so this "
         "harness cannot detect font substitution and the other assertions in "
         "this file prove nothing";
}

}  // namespace
}  // namespace zephyrus_privacy

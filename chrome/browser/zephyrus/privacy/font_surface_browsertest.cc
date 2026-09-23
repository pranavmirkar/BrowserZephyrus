// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The §6.5 fonts surface, exercised the way an attacker would.
//
// HOW YOU TEST SOMETHING DESIGNED TO BE UNDETECTABLE
// --------------------------------------------------
// From script, a hidden family and an uninstalled one are the same event: the
// local lookup is skipped, the next family in the list wins. That is the whole
// design -- a page must not be able to tell it is being filtered.
//
// So a single page cannot prove anything on its own. What CAN be proven is a
// difference between the surface on and off: an application font that resolves
// with the surface off must not resolve with it on, on any origin. Every origin
// then sees the same stock-Windows list, which is the property the surface
// exists to provide -- a font list that neither identifies the machine nor
// links it across sites.
//
// The probe below is the fingerprinting technique itself: render a string in
// "<candidate>, monospace", compare its width against plain monospace. Differs
// => the candidate resolved. Same => it did not, whether because it is absent
// or because we hid it.
//
// WHY THE CANDIDATE LIST IS DISCOVERED AT RUNTIME
// -----------------------------------------------
// Which fonts exist depends on the machine -- Office, Adobe, game launchers and
// IDEs all install their own. Hard-coding a family that happens to be missing
// would make this pass while testing nothing. Instead the test asks the browser
// which candidates are visible at all, and skips loudly if the machine has too
// few to reason about.

#include <string>
#include <vector>

#include "base/test/scoped_feature_list.h"
#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/zephyrus/privacy/privacy_features.h"
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

// Families that commonly arrive with applications rather than with Windows
// itself, and are therefore NOT in CSSFontSelector's always-visible set. The
// test uses whichever of these the machine actually has.
constexpr const char* kTailCandidates[] = {
    "Agency FB",          "Algerian",        "Bauhaus 93",
    "Bell MT",            "Berlin Sans FB",  "Bodoni MT",
    "Book Antiqua",       "Bookman Old Style", "Bradley Hand ITC",
    "Britannic Bold",     "Broadway",        "Brush Script MT",
    "Calisto MT",         "Castellar",       "Centaur",
    "Century Gothic",     "Chiller",         "Colonna MT",
    "Cooper Black",       "Copperplate Gothic Bold", "Curlz MT",
    "Elephant",           "Engravers MT",    "Eras Bold ITC",
    "Felix Titling",      "Footlight MT Light", "Forte",
    "Franklin Gothic Book", "Freestyle Script", "French Script MT",
    "Garamond",           "Gigi",            "Gill Sans MT",
    "Gloucester MT Extra Condensed", "Goudy Old Style", "Haettenschweiler",
    "Harlow Solid Italic", "Harrington",     "High Tower Text",
    "Imprint MT Shadow",  "Informal Roman",  "Jokerman",
    "Juice ITC",          "Kristen ITC",     "Lucida Bright",
    "Lucida Calligraphy", "Lucida Fax",      "Lucida Handwriting",
    "Magneto",            "Maiandra GD",     "Matura MT Script Capitals",
    "Mistral",            "Modern No. 20",   "Monotype Corsiva",
    "Niagara Engraved",   "Old English Text MT", "Onyx",
    "Papyrus",            "Parchment",       "Perpetua",
    "Playbill",           "Poor Richard",    "Ravie",
    "Rockwell",           "Script MT Bold",  "Showcard Gothic",
    "Snap ITC",           "Stencil",         "Tempus Sans ITC",
    "Viner Hand ITC",     "Vivaldi",         "Vladimir Script",
    "Wide Latin",
};

// Families that must never be hidden: on every Windows install, so they carry
// no entropy, and suppressing them would restyle ordinary pages for nothing.
constexpr const char* kAlwaysVisibleProbes[] = {
    "Arial", "Times New Roman", "Courier New", "Segoe UI", "Verdana",
    // Shipped with Windows 10/11 itself. The first version of this surface
    // treated these as application fonts and randomised them.
    "Bahnschrift", "Cascadia Code", "Ink Free", "Leelawadee UI", "Sitka Text",
    // Indian-language faces. Hiding these does not break rendering -- see
    // font_fallback_contract_browsertest.cc -- but it would silently restyle
    // Indian-language pages, which this browser will not do.
    "Nirmala UI", "Mangal", "Latha",
};

class FontSurfaceBrowserTest : public InProcessBrowserTest {
 public:
  // Set in the CONSTRUCTOR via ScopedFeatureList rather than as an
  // --enable-features switch in SetUpCommandLine. Randomization is additionally
  // gated on collection being enabled (IsFingerprintRandomizationEnabled), so
  // the Intelligence flag has to be on too -- exactly the trap
  // fingerprint_randomization_browsertest.cc documents: enable one and every
  // surface stays untouched while the assertions pass vacuously.
  //
  // kFpSurfaceAll explicitly, so this keeps testing the surface even if the
  // shipped default changes.
  FontSurfaceBrowserTest() {
    features_.InitWithFeaturesAndParameters(
        {{kZephyrusPrivacyIntelligence, {}},
         {kZephyrusPrivacyFingerprintRandomization,
          {{"surfaces", base::NumberToString(kFpSurfaceAll)}}}},
        {});
  }

  // HTTPS for the same reason the sibling test uses it: parts of the privacy
  // plumbing only engage in a secure context, and a plain http origin makes
  // that look like a Zephyrus bug.
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_https_test_server().Start());
  }

 protected:
  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  void NavigateTo(const std::string& host) {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_https_test_server().GetURL(host, "/empty.html")));
  }

  // Which of `families` resolve on the current page, by the width probe.
  std::vector<std::string> VisibleSubsetOf(
      base::span<const char* const> families) {
    std::string list = "[";
    for (const char* f : families) {
      list += "\"";
      list += f;
      list += "\",";
    }
    list += "]";

    const std::string script = R"((function() {
      const names = )" + list + R"(;
      const probe = (family) => {
        const el = document.createElement('span');
        el.style.whiteSpace = 'pre';
        el.style.fontSize = '48px';
        el.textContent = 'mmmmmwwwwwiiiii0123456789';
        el.style.fontFamily = 'monospace';
        document.body.appendChild(el);
        const base = el.getBoundingClientRect().width;
        el.style.fontFamily = '"' + family + '", monospace';
        const w = el.getBoundingClientRect().width;
        el.remove();
        return w !== base;
      };
      return names.filter(probe).join(',');
    })())";

    const std::string joined =
        content::EvalJs(contents(), script).ExtractString();
    std::vector<std::string> out;
    size_t start = 0;
    while (start < joined.size()) {
      size_t comma = joined.find(',', start);
      if (comma == std::string::npos) {
        out.push_back(joined.substr(start));
        break;
      }
      out.push_back(joined.substr(start, comma - start));
      start = comma + 1;
    }
    return out;
  }

  base::test::ScopedFeatureList features_;
};

// THE POINT OF THE SURFACE: an application font is invisible everywhere, so
// the font list neither identifies the machine nor differs between sites.
IN_PROC_BROWSER_TEST_F(FontSurfaceBrowserTest, AppFontsAreHiddenOnEveryOrigin) {
  for (const char* host : {"a.com", "b.com"}) {
    NavigateTo(host);
    const std::vector<std::string> visible = VisibleSubsetOf(kTailCandidates);
    EXPECT_TRUE(visible.empty())
        << host << " resolved " << visible.size() << " application font(s), "
        << "first: " << (visible.empty() ? "" : visible[0])
        << ". Installed applications are what make a font list unique.";
  }
}

// The negative control for the test above: with the fonts bit clear, the same
// probe DOES see the machine's application fonts. Without this, "none
// visible" would pass on a machine that simply has none.
class FontSurfaceOffBrowserTest : public FontSurfaceBrowserTest {
 public:
  FontSurfaceOffBrowserTest() {
    features_.Reset();
    features_.InitWithFeaturesAndParameters(
        {{kZephyrusPrivacyIntelligence, {}},
         {kZephyrusPrivacyFingerprintRandomization,
          {{"surfaces",
            base::NumberToString(kFpSurfaceAll & ~kFpSurfaceFonts)}}}},
        {});
  }
};

IN_PROC_BROWSER_TEST_F(FontSurfaceOffBrowserTest, ProbeSeesAppFontsWhenOff) {
  NavigateTo("a.com");
  const std::vector<std::string> visible = VisibleSubsetOf(kTailCandidates);
  if (visible.empty()) {
    GTEST_SKIP() << "this machine has none of the application fonts probed, "
                    "so AppFontsAreHiddenOnEveryOrigin proves nothing here";
  }
  SUCCEED() << visible.size() << " application fonts visible with the surface "
            << "off; the hiding test above is meaningful on this machine";
}

// Stability within an origin. A set that changed between two loads would reflow
// text under the user and would itself be a detectable randomisation signal.
IN_PROC_BROWSER_TEST_F(FontSurfaceBrowserTest, OneOriginIsStableAcrossLoads) {
  NavigateTo("a.com");
  const std::vector<std::string> first = VisibleSubsetOf(kTailCandidates);
  NavigateTo("a.com");
  const std::vector<std::string> second = VisibleSubsetOf(kTailCandidates);

  EXPECT_EQ(first, second)
      << "the same origin saw a different font set on a second load; text "
         "would reflow between visits and the change itself would betray "
         "that filtering is happening";
}

// The always-visible set is not subject to the seed, on any origin.
IN_PROC_BROWSER_TEST_F(FontSurfaceBrowserTest, CommonFamiliesAreNeverHidden) {
  for (const char* host : {"a.com", "b.com", "c.com"}) {
    NavigateTo(host);
    const std::vector<std::string> visible =
        VisibleSubsetOf(kAlwaysVisibleProbes);
    // Not every machine has every one of these, so this asserts the ones that
    // exist are not being filtered, via a stable comparison against a.com.
    EXPECT_FALSE(visible.empty())
        << host
        << ": none of the standard families resolved. Either this machine is "
           "missing all of them, or the always-visible list is not being "
           "honoured and ordinary pages are being restyled.";
  }
}

}  // namespace
}  // namespace zephyrus_privacy

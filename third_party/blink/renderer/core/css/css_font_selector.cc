/*
 * Copyright (C) 2007, 2008, 2011 Apple Inc. All rights reserved.
 *           (C) 2007, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "third_party/blink/renderer/core/css/css_font_selector.h"

#include "third_party/blink/renderer/core/frame/zephyrus_fingerprint_seed.h"

#include "build/build_config.h"
#include "third_party/blink/renderer/core/animation/interpolable_color.h"
#include "third_party/blink/renderer/core/css/css_segmented_font_face.h"
#include "third_party/blink/renderer/core/css/css_value_list.h"
#include "third_party/blink/renderer/core/css/font_face_set_document.h"
#include "third_party/blink/renderer/core/css/font_size_functions.h"
#include "third_party/blink/renderer/core/css/resolver/scoped_style_resolver.h"
#include "third_party/blink/renderer/core/css/resolver/style_resolver.h"
#include "third_party/blink/renderer/core/css/style_engine.h"
#include "third_party/blink/renderer/core/css/style_rule_font_palette_values.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/settings.h"
#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_selector_client.h"
#include "third_party/blink/renderer/platform/runtime_enabled_features.h"

namespace blink {

namespace {

scoped_refptr<FontPalette> RetrieveFontPaletteFromStyleEngine(
    scoped_refptr<const FontPalette> request_palette,
    const Document& document,
    StyleEngine& style_engine,
    const AtomicString& family_name) {
  AtomicString requested_palette_values =
      request_palette->GetPaletteValuesName();
  StyleRuleFontPaletteValues* font_palette_values =
      style_engine.FontPaletteValuesForNameAndFamily(requested_palette_values,
                                                     family_name);
  if (font_palette_values) {
    scoped_refptr<FontPalette> new_request_palette =
        FontPalette::Create(requested_palette_values);
    new_request_palette->SetMatchFamilyName(family_name);
    new_request_palette->SetBasePalette(
        font_palette_values->GetBasePaletteIndex(document));
    Vector<FontPalette::FontPaletteOverride> override_colors =
        font_palette_values->GetOverrideColorsAsVector(document);
    if (override_colors.size()) {
      new_request_palette->SetColorOverrides(std::move(override_colors));
    }
    return new_request_palette;
  }
  return nullptr;
}

scoped_refptr<const FontPalette> ResolveInterpolableFontPalette(
    scoped_refptr<const FontPalette> font_palette,
    const Document& document,
    StyleEngine& style_engine,
    const AtomicString& family_name) {
  if (!font_palette->IsInterpolablePalette()) {
    if (font_palette->IsCustomPalette()) {
      scoped_refptr<FontPalette> retrieved_palette =
          RetrieveFontPaletteFromStyleEngine(font_palette, document,
                                             style_engine, family_name);
      return retrieved_palette ? retrieved_palette : FontPalette::Create();
    } else {
      return font_palette;
    }
  }
  scoped_refptr<const FontPalette> start_palette =
      ResolveInterpolableFontPalette(font_palette->GetStart(), document,
                                     style_engine, family_name);
  scoped_refptr<const FontPalette> end_palette = ResolveInterpolableFontPalette(
      font_palette->GetEnd(), document, style_engine, family_name);

  // If two endpoints of the interpolation are equal, we can simplify the tree
  if (*start_palette.get() == *end_palette.get()) {
    return start_palette;
  }

  scoped_refptr<FontPalette> new_palette;
  new_palette = FontPalette::Mix(
      start_palette, end_palette, font_palette->GetStartPercentage(),
      font_palette->GetEndPercentage(), font_palette->GetNormalizedPercentage(),
      font_palette->GetAlphaMultiplier(),
      font_palette->GetColorInterpolationSpace(),
      font_palette->GetHueInterpolationMethod());
  return new_palette;
}

}  // namespace

CSSFontSelector::CSSFontSelector(const TreeScope& tree_scope)
    : tree_scope_(&tree_scope) {
  DCHECK(tree_scope.GetDocument().GetExecutionContext()->IsContextThread());
  DCHECK(tree_scope.GetDocument().GetFrame());
  generic_font_family_settings_ = tree_scope.GetDocument()
                                      .GetFrame()
                                      ->GetSettings()
                                      ->GetGenericFontFamilySettings();
  FontCache::Get().AddClient(this);
  if (tree_scope.RootNode().IsDocumentNode()) {
    font_face_cache_ = MakeGarbageCollected<FontFaceCache>();
    FontFaceSetDocument::From(tree_scope.GetDocument())
        ->AddFontFacesToFontFaceCache(font_face_cache_);
  }
}

CSSFontSelector::~CSSFontSelector() = default;

UseCounter* CSSFontSelector::GetUseCounter() const {
  auto* const context = GetExecutionContext();
  return context && context->IsContextThread() ? context : nullptr;
}

void CSSFontSelector::RegisterForInvalidationCallbacks(
    FontSelectorClient* client) {
  CHECK(client);
  clients_.insert(client);
}

void CSSFontSelector::UnregisterForInvalidationCallbacks(
    FontSelectorClient* client) {
  clients_.erase(client);
}

void CSSFontSelector::DispatchInvalidationCallbacks(
    FontInvalidationReason reason) {
  HeapVector<Member<FontSelectorClient>> clients(clients_);
  for (auto& client : clients) {
    if (client) {
      client->FontsNeedUpdate(this, reason);
    }
  }
}

void CSSFontSelector::FontFaceInvalidated(FontInvalidationReason reason) {
  DispatchInvalidationCallbacks(reason);
}

void CSSFontSelector::FontCacheInvalidated() {
  DispatchInvalidationCallbacks(FontInvalidationReason::kGeneralInvalidation);
}

const FontData* CSSFontSelector::GetFontData(
    const FontDescription& font_description,
    const FontFamily& font_family) {
  const auto& family_name = font_family.FamilyName();
  Document& document = GetTreeScope()->GetDocument();

  FontDescription request_description(font_description);
  const FontPalette* request_palette = request_description.GetFontPalette();

  if (request_palette && request_palette->IsCustomPalette()) {
    scoped_refptr<FontPalette> new_request_palette =
        RetrieveFontPaletteFromStyleEngine(
            request_palette, document, document.GetStyleEngine(), family_name);
    if (new_request_palette) {
      request_description.SetFontPalette(std::move(new_request_palette));
    }
  }

  if (request_palette && request_palette->IsInterpolablePalette()) {
    scoped_refptr<const FontPalette> computed_interpolable_palette =
        ResolveInterpolableFontPalette(request_palette, document,
                                       document.GetStyleEngine(), family_name);
    request_description.SetFontPalette(
        std::move(computed_interpolable_palette));
  }

  if (request_description.GetFontVariantAlternates()) {
    // TODO(https://crbug.com/1382722): For scoping to work correctly, we'd need
    // to traverse the TreeScopes here and fuse / override values of
    // @font-feature-values from these.
    const FontFeatureValuesStorage* feature_values_storage =
        document.GetScopedStyleResolver()
            ? document.GetScopedStyleResolver()->FontFeatureValuesForFamily(
                  family_name)
            : nullptr;
    scoped_refptr<FontVariantAlternates> new_alternates = nullptr;
    if (feature_values_storage) {
      new_alternates = request_description.GetFontVariantAlternates()->Resolve(
          [feature_values_storage](const AtomicString& alias) {
            return feature_values_storage->ResolveStylistic(alias);
          },
          [feature_values_storage](const AtomicString& alias) {
            return feature_values_storage->ResolveStyleset(alias);
          },
          [feature_values_storage](const AtomicString& alias) {
            return feature_values_storage->ResolveCharacterVariant(alias);
          },
          [feature_values_storage](const AtomicString& alias) {
            return feature_values_storage->ResolveSwash(alias);
          },
          [feature_values_storage](const AtomicString& alias) {
            return feature_values_storage->ResolveOrnaments(alias);
          },
          [feature_values_storage](const AtomicString& alias) {
            return feature_values_storage->ResolveAnnotation(alias);
          });
    } else {
      // If no StyleRuleFontFeature alias table values for this font was found,
      // it still needs a resolve call to convert historical-forms state (which
      // is not looked-up against StyleRuleFontFeatureValues) to an internal
      // feature.
      auto no_lookup = [](const AtomicString&) -> Vector<uint32_t> {
        return {};
      };
      new_alternates = request_description.GetFontVariantAlternates()->Resolve(
          no_lookup, no_lookup, no_lookup, no_lookup, no_lookup, no_lookup);
    }

    if (new_alternates) {
      request_description.SetFontVariantAlternates(std::move(new_alternates));
    }
  }

  if (!font_family.FamilyIsGeneric()) {
    if (CSSSegmentedFontFace* face =
            font_face_cache_->Get(request_description, family_name)) {
      return face->GetFontData(request_description);
    }
  }

  // Try to return the correct font based off our settings, in case we were
  // handed the generic font family name.
  AtomicString settings_family_name =
      FamilyNameFromSettings(request_description, font_family);
  if (settings_family_name.empty()) {
    return nullptr;
  }

  const SimpleFontData* font_data =
      FontCache::Get().GetFontData(request_description, settings_family_name);
  if (font_data && request_description.HasSizeAdjust()) {
    DCHECK(RuntimeEnabledFeatures::CSSFontSizeAdjustEnabled());
    if (auto adjusted_size =
            FontSizeFunctions::MetricsMultiplierAdjustedFontSize(
                font_data, request_description)) {
      FontDescription size_adjusted_description(request_description);
      size_adjusted_description.SetAdjustedSize(adjusted_size.value());
      font_data = FontCache::Get().GetFontData(size_adjusted_description,
                                               settings_family_name);
    }
  }

  return font_data;
}

void CSSFontSelector::UpdateGenericFontFamilySettings(Document& document) {
  if (!document.GetSettings()) {
    return;
  }
  generic_font_family_settings_ =
      document.GetSettings()->GetGenericFontFamilySettings();
  FontCacheInvalidated();
}

bool CSSFontSelector::IsAlive() const {
  return tree_scope_ != nullptr;
}

void CSSFontSelector::Trace(Visitor* visitor) const {
  visitor->Trace(tree_scope_);
  visitor->Trace(clients_);
  CSSFontSelectorBase::Trace(visitor);
}

// Zephyrus §6.5, fonts surface.
//
// The attack this answers does not call an enumeration API. A page writes
//   font-family: "SomeFont", monospace
// renders a string and measures it: a width different from the fallback proves
// "SomeFont" is installed. Repeat over a few hundred names and the installed
// font list -- one of the highest-entropy signals a browser leaks -- falls out.
//
// AN ALLOWLIST, NOT A RANDOM SUBSET
// ---------------------------------
// Fonts that ship with Windows carry no entropy: every Windows 10/11 machine
// has them. The identifying signal is what arrives with applications -- Office,
// Adobe, games, IDEs. So a page may see the stock Windows families and nothing
// else: every Zephyrus user on Windows presents the same font list.
//
// This replaced a design that hid a random 25% of the non-allowlisted families
// per origin. That made the list unlinkable across sites but still UNIQUE on
// any one site -- MEASURED on fingerprinting test pages, which is exactly the
// complaint that "the browser has a unique fingerprint". Worse, its allowlist
// missed families Windows itself ships (Bahnschrift, Cascadia, Sitka, Ink Free,
// Leelawadee UI), so it randomised fonts that carry no entropy at all and
// leaked a machine-specific 75% of the ones that do.
//
// The cost is the one Tor Browser pays: a page that names an application font
// the user happens to have renders in its fallback instead. A site's own
// @font-face fonts are never affected (see font_fallback_list.cc), which is
// how nearly every site that cares about its typeface gets it.
//
// The Indic families shipped with Windows are in the list deliberately. Hiding
// one is SAFE -- font_fallback_contract_browsertest.cc proves a page whose
// declared families are all absent still shapes Devanagari, Tamil, Telugu,
// Bengali and Gujarati correctly -- but a Hindi page that asked for Nirmala UI
// and silently got the default font is still a typography regression, and this
// is an India-first browser.
namespace {

// Windows 10/11 stock families (including the on-demand language packs
// Windows installs for Indian locales), matched lowercased.
bool IsAlwaysVisibleFamily(const std::string& lower) {
  static constexpr auto kAlwaysVisible = std::to_array<std::string_view>({
      "arial", "arial black", "bahnschrift", "calibri", "calibri light",
      "cambria", "cambria math", "candara", "candara light", "cascadia code",
      "cascadia mono", "comic sans ms", "consolas", "constantia", "corbel",
      "corbel light", "courier", "courier new", "ebrima",
      "franklin gothic medium", "gabriola", "gadugi", "georgia",
      "hololens mdl2 assets", "impact", "ink free", "javanese text",
      "leelawadee", "leelawadee ui", "lucida console", "lucida sans unicode",
      "malgun gothic", "marlett", "microsoft himalaya", "microsoft jhenghei",
      "microsoft jhenghei ui", "microsoft new tai lue", "microsoft phagspa",
      "microsoft sans serif", "microsoft tai le", "microsoft yahei",
      "microsoft yahei ui", "microsoft yi baiti", "mingliu-extb",
      "mingliu_hkscs-extb", "modern", "mongolian baiti", "ms gothic",
      "ms pgothic", "ms sans serif", "ms serif", "ms ui gothic", "mv boli",
      "myanmar text", "nirmala text", "nirmala ui", "nsimsun",
      "palatino linotype", "pmingliu-extb", "roman", "sans serif collection",
      "script", "segoe fluent icons", "segoe mdl2 assets", "segoe print",
      "segoe script", "segoe ui", "segoe ui emoji", "segoe ui historic",
      "segoe ui symbol", "segoe ui variable", "simsun", "simsun-extb",
      "simsun-extg", "sitka", "small fonts", "sylfaen", "symbol", "system",
      "tahoma", "terminal", "times new roman", "trebuchet ms", "verdana",
      "webdings", "wingdings", "yu gothic", "yu gothic ui",
      // Indian-language faces from the Windows language packs.
      "aparajita", "gautami", "iskoola pota", "kalinga", "kartika",
      "khmer ui", "kokila", "latha", "mangal", "meiryo", "meiryo ui", "raavi",
      "sanskrit text", "shonar bangla", "shruti", "tunga", "utsaah", "vani",
      "vijaya", "vrinda", "daunpenh", "dokchampa", "estrangelo edessa",
      "euphemia", "lao ui", "moolboran", "plantagenet cherokee",
  });
  // Families Windows ships in several weights, named "<family> <weight>"
  // ("Segoe UI Semibold", "Sitka Banner", "Yu Gothic UI Light").
  static constexpr auto kWeightedFamilies = std::to_array<std::string_view>({
      "segoe ui ", "segoe ui variable ", "sitka ", "yu gothic ",
      "yu gothic ui ", "leelawadee ui ", "nirmala ui ", "nirmala text ",
      "malgun gothic ", "microsoft yahei ", "microsoft jhenghei ",
      "cascadia code ", "cascadia mono ", "bahnschrift ",
  });
  for (std::string_view candidate : kAlwaysVisible) {
    if (candidate == lower) {
      return true;
    }
  }
  for (std::string_view prefix : kWeightedFamilies) {
    if (lower.size() > prefix.size() && lower.starts_with(prefix)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool CSSFontSelector::ShouldHideLocalFontFamily(
    const AtomicString& family) const {
  if (family.empty()) {
    return false;
  }
  // Returns nullopt when the feature is off, when randomisation is off, or when
  // the fonts bit specifically is clear -- so the surface can be retired on its
  // own without touching the other five.
  if (!ZephyrusSeedForSurface(GetExecutionContext(), kZephyrusFpFonts)) {
    return false;
  }
  return !IsAlwaysVisibleFamily(family.ToAsciiLower().Utf8());
}

}  // namespace blink

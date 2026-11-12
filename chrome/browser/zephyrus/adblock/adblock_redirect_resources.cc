// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#include "chrome/browser/zephyrus/adblock/adblock_redirect_resources.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/no_destructor.h"

namespace zephyrus_adblock {

BASE_FEATURE(kZephyrusAdblockStandIns,
             "ZephyrusAdblockStandIns",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kZephyrusAdblockQuietBlock,
             "ZephyrusAdblockQuietBlock",
             base::FEATURE_ENABLED_BY_DEFAULT);

namespace {

#include "chrome/browser/zephyrus/adblock/adblock_redirect_resources_data.inc"

// The table as a span: checked indexing, where an array is not.
constexpr base::span<const RedirectResource> Table() {
  return base::span<const RedirectResource>(kResourceTable);
}

// The decoded body of every resource, built on first use. A function-local
// static is initialised once and safely even when several threads ask at once.
const std::vector<std::string>& DecodedBodies() {
  static const base::NoDestructor<std::vector<std::string>> bodies([] {
    std::vector<std::string> out(Table().size());
    for (size_t i = 0; i < Table().size(); ++i) {
      const RedirectResource& r = Table()[i];
      if (!r.base64.empty()) {
        if (!base::Base64Decode(r.base64, &out[i])) {
          out[i].clear();  // cannot happen: generated from real files
        }
      } else {
        out[i].assign(r.text);
      }
    }
    return out;
  }());
  return *bodies;
}

}  // namespace

const RedirectResource* FindRedirectResource(std::string_view name) {
  for (const RedirectResource& r : kResourceTable) {
    if (r.name == name) {
      return &r;
    }
  }
  for (const RedirectAlias& a : kAliases) {
    if (a.alias == name) {
      return RedirectResourceById(a.id);
    }
  }
  return nullptr;
}

const RedirectResource* RedirectResourceById(uint16_t id) {
  for (const RedirectResource& r : kResourceTable) {
    if (r.id == id) {
      return &r;
    }
  }
  return nullptr;
}

namespace {

// The libraries a page calls by name straight after loading them, at their
// canonical addresses, and the stand-in that answers in their shape.
struct Surrogate {
  std::string_view host;
  std::string_view path_prefix;
  std::string_view resource;
};
constexpr Surrogate kSurrogates[] = {
    {"pagead2.googlesyndication.com", "/pagead/js/adsbygoogle.js",
     "googlesyndication_adsbygoogle.js"},
    {"securepubads.g.doubleclick.net", "/tag/js/gpt.js",
     "googletagservices_gpt.js"},
    {"www.googletagservices.com", "/tag/js/gpt.js", "googletagservices_gpt.js"},
    {"imasdk.googleapis.com", "/js/sdkloader/ima3.js", "google-ima.js"},
    {"www.googletagmanager.com", "/gtm.js", "googletagmanager_gtm.js"},
    {"www.google-analytics.com", "/analytics.js",
     "google-analytics_analytics.js"},
    {"ssl.google-analytics.com", "/ga.js", "google-analytics_ga.js"},
    {"www.google-analytics.com", "/ga.js", "google-analytics_ga.js"},
    {"www.google-analytics.com", "/cx/api.js", "google-analytics_cx_api.js"},
    {"c.amazon-adsystem.com", "/aax2/apstag.js", "amazon_apstag.js"},
    {"c.amazon-adsystem.com", "/aax2/amzn_ads.js", "amazon_ads.js"},
    {"sb.scorecardresearch.com", "/beacon.js", "scorecardresearch_beacon.js"},
    {"static.chartbeat.com", "/js/chartbeat.js", "chartbeat.js"},
    {"widgets.outbrain.com", "/outbrain.js", "outbrain-widget.js"},
    // Detection bait: a file whose only job is to prove that nothing blocked
    // it. Answered with a local replica of what it does, so the detection
    // vendor is not contacted and the page sees what it would without a
    // blocker. MEASURED: detectadblock.com caught Zephyrus, Brave and uBlock
    // Origin Lite alike with this one.
    {"www.adblockanalytics.com", "/google-adsense.js", "adblockanalytics_bait.js"},
};

// A transparent image the size a check would expect. A 1x1 answer for an
// address that is not a tracking pixel is itself a known sign of a blocker:
// detection scripts load an ad server's favicon and treat anything narrower
// than 2 pixels as a stand-in.
const RedirectResource* ImageStandInFor(const GURL& url) {
  const std::string_view path = url.path();
  if (path.ends_with(".ico") || path.find("favicon") != std::string_view::npos) {
    return FindRedirectResource("32x32.png");
  }
  if (path.find("1x1") != std::string_view::npos ||
      path.find("pixel") != std::string_view::npos ||
      path.ends_with(".gif")) {
    return FindRedirectResource("1x1.gif");
  }
  return FindRedirectResource("2x2.png");
}

}  // namespace

const RedirectResource* QuietBlockResource(const GURL& url, ResourceType type) {
  if (!base::FeatureList::IsEnabled(kZephyrusAdblockQuietBlock)) {
    return nullptr;
  }
  switch (type) {
    case kTypeXhr:
      // "{}", not nothing: pages parse what they fetch as JSON, and an empty
      // body throws "Unexpected end of JSON input" (MEASURED on 7 of 78 popular
      // sites, 2026-11-12). Read as text, "{}" is still harmless.
      return FindRedirectResource("noop.json");
    case kTypePing:
      return FindRedirectResource("empty");
    case kTypeImage:
      return ImageStandInFor(url);
    case kTypeScript:
      if (!url.SchemeIsHTTPOrHTTPS()) {
        return nullptr;
      }
      for (const Surrogate& s : kSurrogates) {
        if (url.host() == s.host &&
            url.path().starts_with(s.path_prefix)) {
          return FindRedirectResource(s.resource);
        }
      }
      return nullptr;
    default:
      return nullptr;
  }
}

std::string_view RedirectResourceBody(const RedirectResource& resource) {
  // `resource` came from this table; find which row, by address.
  for (size_t i = 0; i < Table().size(); ++i) {
    if (&Table()[i] == &resource) {
      return DecodedBodies()[i];
    }
  }
  return std::string_view();
}

std::string_view RedirectResourceMime(const RedirectResource& resource,
                                      ResourceType type) {
  // A plain-text stand-in answers in the type the request expects, as `empty`
  // does. MEASURED (irctc.co.in, 2026-11-12): a list rule naming noop.txt for
  // a script got text/plain, which Chromium refuses to run ("strict MIME type
  // checking"), an error the page sees.
  if (resource.name != "empty" && resource.mime != "text/plain") {
    return resource.mime;
  }
  switch (type) {
    case kTypeScript:
      return "text/javascript";
    case kTypeStylesheet:
      return "text/css";
    case kTypeSubdocument:
      return "text/html";
    default:
      return "text/plain";
  }
}

bool RedirectResourceServes(const RedirectResource& resource,
                            ResourceType type) {
  return (resource.types & type) != 0;
}

size_t RedirectResourceCount() {
  return std::size(kResourceTable);
}

std::optional<uint16_t> ChooseRedirect(
    const std::vector<RedirectCandidate>& candidates,
    const std::vector<uint16_t>& exceptions,
    ResourceType type) {
  if (std::ranges::contains(exceptions, kNoRedirectResource)) {
    return std::nullopt;
  }
  const RedirectCandidate* best = nullptr;
  for (const RedirectCandidate& candidate : candidates) {
    const RedirectResource* resource = RedirectResourceById(candidate.resource);
    if (!resource || !RedirectResourceServes(*resource, type) ||
        std::ranges::contains(exceptions, candidate.resource)) {
      continue;
    }
    if (!best || candidate.priority > best->priority ||
        (candidate.priority == best->priority &&
         candidate.resource < best->resource)) {
      best = &candidate;
    }
  }
  if (!best) {
    return std::nullopt;
  }
  return best->resource;
}

}  // namespace zephyrus_adblock

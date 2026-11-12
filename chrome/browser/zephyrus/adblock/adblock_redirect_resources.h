// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_REDIRECT_RESOURCES_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_REDIRECT_RESOURCES_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "base/feature_list.h"
#include "url/gurl.h"
#include "chrome/browser/zephyrus/adblock/adblock_filter_engine.h"

namespace zephyrus_adblock {

// The stand-ins a `$redirect=` rule names.
//
// A rule such as `||tracker.example/ga.js$script,redirect=google-analytics_ga.js`
// does not only refuse the real file: it hands the page a harmless substitute
// with the same shape. A page that calls `_gaq.push(...)` after the real library
// "loaded" keeps working, a video player waits for an ad manager that answers at
// once with no ad, and an image slot gets a transparent pixel.
//
// These are written from scratch against the public documentation of each API
// (the filter lists name them; they do not ship their code). The sources are in
// redirect_resources/ and tools/gen_redirect_resources.py builds the table.
//
// Resource ids are stored in the compiled index, so they never change meaning.
inline constexpr uint16_t kNoRedirectResource = 0;

// Whether a blocked request that a `$redirect=` rule names a stand-in for is
// answered with it (on, the default) or plainly blocked, as it would be with no
// stand-in at all. `--disable-features=ZephyrusAdblockStandIns` is the kill
// switch.
BASE_DECLARE_FEATURE(kZephyrusAdblockStandIns);

// Whether a blocked background request (fetch, XHR, sendBeacon, ping) that no
// rule names a stand-in for is answered with an empty success instead of a
// network error. On by default; `--disable-features=ZephyrusAdblockQuietBlock`
// is the kill switch.
//
// MEASURED (shield_ruler.py, 2026-11-10): the Guardian's front page threw 18
// uncaught "Failed to fetch" errors with Shield on, one for each of its own
// analytics beacons that was blocked, and one load of cnn.com asked for a
// blocked address 11,118 times in 30 seconds. A script that is told its
// request failed reports the failure or tries again; told it succeeded, it
// moves on. Nothing is sent either way.
BASE_DECLARE_FEATURE(kZephyrusAdblockQuietBlock);

struct RedirectResource {
  uint16_t id;
  std::string_view name;
  std::string_view mime;
  // The kinds of request this stand-in can answer (ResourceType bits).
  uint32_t types;
  // The body: text as written, or base64 of the bytes for binary files.
  std::string_view text;
  std::string_view base64;
};

struct RedirectAlias {
  std::string_view alias;
  uint16_t id;
};

// The resource a filter list calls `name`, which is its own name or an alias
// (`noopjs`, `google-analytics.com/ga.js`). A `:priority` suffix is not part of
// the name. Null when there is no such resource.
const RedirectResource* FindRedirectResource(std::string_view name);
const RedirectResource* RedirectResourceById(uint16_t id);

// What a blocked request for `url`, of kind `type`, is answered with when no
// rule named a stand-in for it; null means fail it, as before. Null for
// everything when the feature is off.
//
//  - A background request (fetch, XHR, sendBeacon, ping): an empty success.
//  - An image: a transparent pixel. A blocked image is an ad or a tracking
//    pixel; failing it shows a broken-image box where the ad was and fires
//    `onerror`, which anti-adblock scripts listen for.
//  - A script: the working stand-in for the ad or analytics library it is
//    (AdSense, Publisher Tag, IMA, Analytics, Tag Manager, ...), whether or not
//    a list rule named one. Pages and anti-adblock checks call these libraries
//    straight after loading them. Any other blocked script still fails: a stub
//    that defines nothing is as detectable as an error, and a page may rely on
//    a failed load to fall back.
//  - Frames, media, fonts, styles, documents: null.
//
// MEASURED (detect_ruler.py, 2026-11-10): before this, a page could tell
// Zephyrus was blocking with 7 of 21 published checks, against 4 for Brave and
// uBlock Origin Lite; AdSense and the Publisher Tag were the difference.
const RedirectResource* QuietBlockResource(const GURL& url, ResourceType type);

// The bytes to serve, decoded once. Valid for the life of the process.
std::string_view RedirectResourceBody(const RedirectResource& resource);

// The content type to answer a request of `type` with. Fixed for every resource
// but `empty`, which is whatever the request expected.
std::string_view RedirectResourceMime(const RedirectResource& resource,
                                      ResourceType type);

// Whether the stand-in can answer this kind of request. A page that asked for a
// script and is handed a picture is no better off than one that was blocked.
bool RedirectResourceServes(const RedirectResource& resource,
                            ResourceType type);

// How many resources there are; ids run 1..the largest id.
size_t RedirectResourceCount();

// A stand-in some matching rule asks for.
struct RedirectCandidate {
  uint16_t resource;
  int8_t priority;
};

// Picks the stand-in for a request of `type` from the directives that matched
// it. `exceptions` are the resource ids that matching `@@...$redirect-rule`
// rules cancel (kNoRedirectResource cancels every one). Of what is left and
// can answer this kind of request, the highest priority wins, and among equals
// the lowest resource id, so that the engine and the compiled index, which find
// the candidates in different orders, choose alike. Null: just block.
std::optional<uint16_t> ChooseRedirect(
    const std::vector<RedirectCandidate>& candidates,
    const std::vector<uint16_t>& exceptions,
    ResourceType type);

}  // namespace zephyrus_adblock

#endif  // CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_REDIRECT_RESOURCES_H_

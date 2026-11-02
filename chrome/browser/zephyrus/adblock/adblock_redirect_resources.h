// Copyright 2026 Pranav Mirkar. PolyForm Strict 1.0.0 (see the repository's
// LICENSE).

#ifndef CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_REDIRECT_RESOURCES_H_
#define CHROME_BROWSER_ZEPHYRUS_ADBLOCK_ADBLOCK_REDIRECT_RESOURCES_H_

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "base/feature_list.h"
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

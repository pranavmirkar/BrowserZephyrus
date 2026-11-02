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
  if (resource.name != "empty") {
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

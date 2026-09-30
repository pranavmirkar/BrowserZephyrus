// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_BUNDLED_KEYS_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_BUNDLED_KEYS_H_

#include <optional>
#include <string>
#include <string_view>

namespace zephyrus::agent {

// What a demo build ships so an evaluator can use the agent and voice without
// entering anything (GN arg zephyrus_bundled_keys_file): the address of the demo
// proxy and an expiring, signed token for it. NOT a provider key. The provider
// keys live only in the proxy's environment (demo_proxy/), so everything in the
// installer, once extracted, works nowhere else and stops at the token's expiry.
//
// A key the user saves themselves always wins, and is sent straight to the
// provider, never to the proxy.
//
// `kind` is a model provider ("anthropic") or kVoiceKeyKind.
bool HasBundledKey(std::string_view kind);
std::optional<std::string> BundledKey(std::string_view kind);

// The proxy's origin, e.g. "https://name.vercel.app"; empty in a normal build.
std::string BundledProxyUrl();

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_BUNDLED_KEYS_H_

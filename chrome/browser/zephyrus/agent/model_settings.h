// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ZEPHYRUS_AGENT_MODEL_SETTINGS_H_
#define CHROME_BROWSER_ZEPHYRUS_AGENT_MODEL_SETTINGS_H_

#include <optional>
#include <string>
#include <string_view>

#include "url/gurl.h"

class PrefRegistrySimple;
class PrefService;

namespace os_crypt_async {
class Encryptor;
}

namespace zephyrus::agent {

// Which cloud model the agent uses, and what it may spend. Stored in the
// profile's prefs. The key is NOT in here: it is stored encrypted, separately,
// and only read at the moment a task needs it (see LoadApiKey).
struct CloudModelConfig {
  // "anthropic", "openai" or "gemini". Empty means no cloud model.
  std::string kind;
  std::string model;
  // The endpoint. For OpenAI-compatible services this is the user's to choose
  // (OpenAI, OpenRouter, a local server); for the other two it is fixed.
  GURL base_url;
  // Insist on a native tool call. Off only for servers that reject that.
  bool force_tool = true;
  // The most one task may spend, in US dollars, at list price. Applied as a
  // token cap instead when the model's price is not known.
  double max_usd_per_task = 1.0;
  // Show the model a labelled screenshot every step. On by default: the
  // accessibility tree alone was not enough for real apps like Notion. Off for
  // text-only models, which reject an image.
  bool send_screenshots = true;
};

// Pref names. Registered by RegisterProfilePrefs, from browser_prefs.cc.
inline constexpr char kCloudKindPref[] = "zephyrus.agent.cloud.kind";
inline constexpr char kCloudModelPref[] = "zephyrus.agent.cloud.model";
inline constexpr char kCloudBaseUrlPref[] = "zephyrus.agent.cloud.base_url";
inline constexpr char kCloudForceToolPref[] = "zephyrus.agent.cloud.force_tool";
inline constexpr char kCloudMaxUsdPref[] = "zephyrus.agent.cloud.max_usd";
// Whether the mascot lives in the browser window. On by default.
inline constexpr char kMascotPref[] = "zephyrus.agent.mascot";
// Whether the agent keeps what it is told across chats (long-term memory). On by
// default; the chat memory a follow-up needs is separate and always on.
inline constexpr char kMemoryPref[] = "zephyrus.agent.memory";
inline constexpr char kCloudScreenshotsPref[] =
    "zephyrus.agent.cloud.screenshots";
// kind -> base64 of the OSCrypt ciphertext of that provider's key.
inline constexpr char kCloudKeysPref[] = "zephyrus.agent.cloud.keys";
// The Workspaces (by id) the user has let send page content to the cloud model.
inline constexpr char kCloudWorkspacesPref[] = "zephyrus.agent.cloud.workspaces";

// Set once the user first changes the per-workspace switch. Until then a build
// that bundles a key treats every regular workspace as allowed (bundled_keys.h).
inline constexpr char kCloudWorkspacesCustomisedPref[] =
    "zephyrus.agent.cloud.workspaces_customised";

// "Use Zephyrus provided keys" (the hackathon build). On by default. On: the
// agent and voice use the build's demo token through the demo proxy and the
// user's own keys are not used. Off: only the user's own keys, sent straight to
// the provider. Only has an effect in a build that bundles a token.
inline constexpr char kUseProvidedKeysPref[] = "zephyrus.agent.use_provided_keys";

// What a build with a bundled key uses until the user chooses otherwise.
inline constexpr char kBundledDefaultKind[] = "anthropic";
inline constexpr char kBundledDefaultModel[] = "claude-sonnet-5-5";

// Hands-free voice: "Hey Zep", and Voice Lock. Registered by
// RegisterProfilePrefs. Off by default: the microphone opens only when the
// person asks.
inline constexpr char kHandsFreePref[] = "zephyrus.voice.hands_free";
// Only enrolled voices wake it. On by default.
inline constexpr char kVoiceLockPref[] = "zephyrus.voice.lock";
// 0 strict, 1 balanced, 2 relaxed.
inline constexpr char kVoiceSensitivityPref[] = "zephyrus.voice.sensitivity";
// The first-run invitation to set up "Hey Zep" has been shown.
inline constexpr char kVoiceOnboardingPref[] = "zephyrus.voice.onboarding_shown";

// The key name AssemblyAI's key (voice commands) is stored under, alongside
// the model providers' keys and encrypted the same way.
inline constexpr char kVoiceKeyKind[] = "assemblyai";

void RegisterProfilePrefs(PrefRegistrySimple* registry);

// The endpoint a provider kind uses when the user has not chosen one.
GURL DefaultBaseUrl(std::string_view kind);

// Null when no cloud model is configured, or the stored one is unusable.
std::optional<CloudModelConfig> ReadCloudModelConfig(const PrefService& prefs);
void WriteCloudModelConfig(PrefService& prefs, const CloudModelConfig& config);

// Keys, encrypted with the OS keystore (DPAPI on Windows). Never logged, never
// shown back to the user, never sent to the kernel.
bool StoreApiKey(PrefService& prefs,
                 const os_crypt_async::Encryptor& encryptor,
                 std::string_view kind,
                 const std::string& key);
std::optional<std::string> LoadApiKey(const PrefService& prefs,
                                      const os_crypt_async::Encryptor& encryptor,
                                      std::string_view kind);
// A key the USER saved for this kind, whatever the provided-keys switch says.
bool HasSavedKey(const PrefService& prefs, std::string_view kind);
// A usable key exists for this kind: the user's own, or the provided one.
bool HasApiKey(const PrefService& prefs, std::string_view kind);

// What a voice request needs: the key (the user's, or the build's demo token) and
// where to send it -- empty for AssemblyAI itself, the demo proxy's address for
// the token.
struct VoiceCredentials {
  VoiceCredentials();
  VoiceCredentials(VoiceCredentials&&);
  VoiceCredentials& operator=(VoiceCredentials&&);
  ~VoiceCredentials();

  std::string key;
  std::string endpoint;
};
std::optional<VoiceCredentials> LoadVoiceCredentials(
    const PrefService& prefs,
    const os_crypt_async::Encryptor& encryptor);
// True when this build bundles a token for `kind` and the provided-keys switch
// is on, so LoadApiKey hands back the demo token and the request goes to the
// demo proxy, never to the provider.
bool UsesBundledKey(const PrefService& prefs, std::string_view kind);
void ForgetApiKey(PrefService& prefs, std::string_view kind);

// Cloud consent is per Workspace and off by default (ADR 0004).
bool IsCloudAllowedInWorkspace(const PrefService& prefs, int workspace_id);
void SetCloudAllowedInWorkspace(PrefService& prefs,
                                int workspace_id,
                                bool allowed);

// List prices in US dollars per million tokens, for models whose price is
// known. Dated in the .cc; a model not listed is held to a token cap instead
// of a guessed price.
struct ModelPrices {
  double input = 0;
  double output = 0;
  double cache_read = 0;
  double cache_write = 0;
};
std::optional<ModelPrices> KnownPrices(std::string_view model);

// The token cap applied when the price is unknown: enough for a long task on
// a cheap model, and a hard stop on a runaway loop.
inline constexpr uint64_t kUnknownPriceTokenCap = 2'000'000;

}  // namespace zephyrus::agent

#endif  // CHROME_BROWSER_ZEPHYRUS_AGENT_MODEL_SETTINGS_H_

// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/zephyrus/agent/model_settings.h"

#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/values.h"
#include "chrome/browser/zephyrus/agent/bundled_keys.h"
#include "chrome/browser/zephyrus/buildflags/dev_switches.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace zephyrus::agent {
namespace {

// A model id is letters, digits, dots and hyphens; anything else is not one.
bool IsPlausibleModelId(std::string_view id) {
  if (id.empty() || id.size() > 64) {
    return false;
  }
  for (char c : id) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool IsKnownKind(std::string_view kind) {
  return kind == "anthropic" || kind == "openai" || kind == "gemini";
}

// A spending limit outside this range is a typo, not a decision.
constexpr double kMinUsd = 0.01;
constexpr double kMaxUsd = 100.0;

}  // namespace

void RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterStringPref(kCloudKindPref, std::string());
  registry->RegisterStringPref(kCloudModelPref, std::string());
  registry->RegisterStringPref(kCloudBaseUrlPref, std::string());
  registry->RegisterBooleanPref(kCloudForceToolPref, true);
  registry->RegisterDoublePref(kCloudMaxUsdPref, 1.0);
  registry->RegisterBooleanPref(kCloudScreenshotsPref, true);
  registry->RegisterBooleanPref(kMascotPref, true);
  registry->RegisterStringPref(kMascotSkinPref, "zep");
  registry->RegisterStringPref(kMascotLensFillPref, "static");
  registry->RegisterBooleanPref(kMemoryPref, true);
  registry->RegisterDictionaryPref(kCloudKeysPref);
  registry->RegisterListPref(kCloudWorkspacesPref);
  registry->RegisterBooleanPref(kCloudWorkspacesCustomisedPref, false);
  registry->RegisterBooleanPref(kUseProvidedKeysPref, true);
  registry->RegisterBooleanPref(kHandsFreePref, false);
  registry->RegisterBooleanPref(kVoiceLockPref, true);
  registry->RegisterIntegerPref(kVoiceSensitivityPref, 1);
  registry->RegisterStringPref(kMicDevicePref, std::string());
  registry->RegisterBooleanPref(kVoiceOnboardingPref, false);
}

GURL DefaultBaseUrl(std::string_view kind) {
  if (kind == "anthropic") {
    return GURL("https://api.anthropic.com");
  }
  if (kind == "openai") {
    return GURL("https://api.openai.com/v1");
  }
  if (kind == "gemini") {
    return GURL("https://generativelanguage.googleapis.com");
  }
  return GURL();
}

std::optional<CloudModelConfig> ReadCloudModelConfig(const PrefService& prefs) {
  CloudModelConfig config;
  config.kind = prefs.GetString(kCloudKindPref);
  config.model = prefs.GetString(kCloudModelPref);
  if (UsesBundledKey(prefs, kBundledDefaultKind)) {
    // Provided keys are for one provider, so whatever else was chosen before
    // the switch went on does not apply while it is on. Nothing chosen: the
    // demo default, so the agent works the moment it is opened.
    if (config.kind != kBundledDefaultKind) {
      config.kind = kBundledDefaultKind;
      config.model.clear();
    }
    // The model is the build's choice while its keys are in use: the settings
    // field for it is disabled then, and a profile that saved an older default
    // must not keep spending the demo's credit on it.
    config.model = kBundledDefaultModel;
  }
  // A developer build may try another model with --zephyrus-agent-model=<id>,
  // for benchmarking a cheaper one. Ignored in a build without dev switches,
  // and it only changes the model NAME in the request: the demo proxy still
  // decides which models its token may use, and the key and address are chosen
  // as before.
  if (zephyrus::DevSwitchesEnabled()) {
    const base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
    if (command_line && command_line->HasSwitch(kDevModelSwitch)) {
      const std::string wanted = command_line->GetSwitchValueASCII(kDevModelSwitch);
      if (IsPlausibleModelId(wanted)) {
        config.model = wanted;
      }
    }
  }
  if (!IsKnownKind(config.kind) || config.model.empty()) {
    return std::nullopt;
  }
  // Only an OpenAI-compatible endpoint is the user's to choose. The other two
  // ignore whatever is stored, so a tampered pref cannot redirect them.
  const std::string stored_base = prefs.GetString(kCloudBaseUrlPref);
  config.base_url = config.kind == "openai" && !stored_base.empty()
                        ? GURL(stored_base)
                        : DefaultBaseUrl(config.kind);
  if (UsesBundledKey(prefs, config.kind) && config.kind == "anthropic") {
    // The demo token is not a provider key and only the proxy accepts it. The
    // decision is made from where the KEY comes from, so a key the user saved
    // can never be sent to the proxy, nor the token to the provider.
    config.base_url = GURL(BundledProxyUrl() + "/api/anthropic");
  }
  if (!config.base_url.is_valid()) {
    return std::nullopt;
  }
  config.force_tool = prefs.GetBoolean(kCloudForceToolPref);
  config.send_screenshots = prefs.GetBoolean(kCloudScreenshotsPref);
  config.max_usd_per_task =
      std::clamp(prefs.GetDouble(kCloudMaxUsdPref), kMinUsd, kMaxUsd);
  return config;
}

MascotLook ReadMascotLook(const PrefService& prefs) {
  if (prefs.GetString(kMascotSkinPref) != "lens") {
    return MascotLook::kZep;
  }
  return prefs.GetString(kMascotLensFillPref) == "fluid"
             ? MascotLook::kLensFluid
             : MascotLook::kLensStatic;
}

void WriteMascotLook(PrefService& prefs, MascotLook look) {
  prefs.SetString(kMascotSkinPref, look == MascotLook::kZep ? "zep" : "lens");
  // The fill is only meaningful for the lens; leave it as it was for Zep so
  // switching back to the lens remembers the last choice.
  if (look != MascotLook::kZep) {
    prefs.SetString(kMascotLensFillPref,
                    look == MascotLook::kLensFluid ? "fluid" : "static");
  }
}

void WriteCloudModelConfig(PrefService& prefs, const CloudModelConfig& config) {
  prefs.SetString(kCloudKindPref, config.kind);
  prefs.SetString(kCloudModelPref, config.model);
  prefs.SetString(kCloudBaseUrlPref,
                  config.kind == "openai" ? config.base_url.spec()
                                          : std::string());
  prefs.SetBoolean(kCloudForceToolPref, config.force_tool);
  prefs.SetBoolean(kCloudScreenshotsPref, config.send_screenshots);
  prefs.SetDouble(kCloudMaxUsdPref,
                  std::clamp(config.max_usd_per_task, kMinUsd, kMaxUsd));
}

bool StoreApiKey(PrefService& prefs,
                 const os_crypt_async::Encryptor& encryptor,
                 std::string_view kind,
                 const std::string& key) {
  // A key is stored for a model provider, or for AssemblyAI (voice commands);
  // "assemblyai" is never a model kind.
  if (!(IsKnownKind(kind) || kind == kVoiceKeyKind) || key.empty()) {
    return false;
  }
  std::optional<std::vector<uint8_t>> ciphertext = encryptor.EncryptString(key);
  if (!ciphertext) {
    // No keystore, no key. Storing it in the clear is not the fallback.
    return false;
  }
  ScopedDictPrefUpdate update(&prefs, kCloudKeysPref);
  update->Set(kind, base::Base64Encode(*ciphertext));
  return true;
}

std::optional<std::string> LoadApiKey(const PrefService& prefs,
                                      const os_crypt_async::Encryptor& encryptor,
                                      std::string_view kind) {
  // The switch decides whose key this is. On: the build's demo token. Off: only
  // what the user saved. Never a fallback from one to the other, so a saved key
  // that fails to decrypt cannot turn into the token (sent to a provider that
  // rejects it) and the token cannot be replaced by a key the user did not mean.
  if (UsesBundledKey(prefs, kind)) {
    return BundledKey(kind);
  }
  const std::string* stored = prefs.GetDict(kCloudKeysPref).FindString(kind);
  if (!stored) {
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> ciphertext = base::Base64Decode(*stored);
  if (!ciphertext) {
    return std::nullopt;
  }
  std::optional<std::string> key = encryptor.DecryptData(*ciphertext);
  if (!key || key->empty()) {
    return std::nullopt;
  }
  return key;
}

VoiceCredentials::VoiceCredentials() = default;
VoiceCredentials::VoiceCredentials(VoiceCredentials&&) = default;
VoiceCredentials& VoiceCredentials::operator=(VoiceCredentials&&) = default;
VoiceCredentials::~VoiceCredentials() = default;

std::optional<VoiceCredentials> LoadVoiceCredentials(
    const PrefService& prefs,
    const os_crypt_async::Encryptor& encryptor) {
  std::optional<std::string> key = LoadApiKey(prefs, encryptor, kVoiceKeyKind);
  if (!key) {
    return std::nullopt;
  }
  VoiceCredentials credentials;
  credentials.key = std::move(*key);
  // The build's token goes to its proxy; a key the user saved goes straight to
  // AssemblyAI. Decided by where the key came from, never by a setting.
  if (UsesBundledKey(prefs, kVoiceKeyKind)) {
    credentials.endpoint = BundledProxyUrl() + "/api/assemblyai/transcribe";
  }
  return credentials;
}

bool HasSavedKey(const PrefService& prefs, std::string_view kind) {
  return prefs.GetDict(kCloudKeysPref).FindString(kind) != nullptr;
}

bool UsesBundledKey(const PrefService& prefs, std::string_view kind) {
  return HasBundledKey(kind) && prefs.GetBoolean(kUseProvidedKeysPref);
}

bool HasApiKey(const PrefService& prefs, std::string_view kind) {
  return HasSavedKey(prefs, kind) || UsesBundledKey(prefs, kind);
}

void ForgetApiKey(PrefService& prefs, std::string_view kind) {
  ScopedDictPrefUpdate update(&prefs, kCloudKeysPref);
  update->Remove(kind);
}

bool IsCloudAllowedInWorkspace(const PrefService& prefs, int workspace_id) {
  // A build that ships its own key is a demo, and consent is not something an
  // evaluator should have to find first. Until anyone touches the per-workspace
  // switch every regular workspace is allowed; Private stays off regardless
  // (the controller refuses an off-the-record profile before it gets here).
  if (!prefs.GetBoolean(kCloudWorkspacesCustomisedPref) &&
      UsesBundledKey(prefs, kBundledDefaultKind)) {
    return true;
  }
  const base::ListValue& allowed = prefs.GetList(kCloudWorkspacesPref);
  return std::ranges::any_of(allowed, [&](const base::Value& entry) {
    return entry.is_int() && entry.GetInt() == workspace_id;
  });
}

void SetCloudAllowedInWorkspace(PrefService& prefs,
                                int workspace_id,
                                bool allowed) {
  prefs.SetBoolean(kCloudWorkspacesCustomisedPref, true);
  ScopedListPrefUpdate update(&prefs, kCloudWorkspacesPref);
  update->EraseIf([&](const base::Value& entry) {
    return entry.is_int() && entry.GetInt() == workspace_id;
  });
  if (allowed) {
    update->Append(workspace_id);
  }
}

std::optional<ModelPrices> KnownPrices(std::string_view model) {
  // US dollars per million tokens: input, output, cache read, cache write.
  // Checked 2026-09-23 against the providers' price pages. A stale price only
  // moves where the spending limit bites; it never removes the limit.
  struct Row {
    std::string_view model;
    ModelPrices prices;
  };
  static constexpr Row kTable[] = {
      {"claude-opus-5-5", {4.00, 20.00, 0.20, 5.00}},
      {"claude-opus-5", {5.00, 25.00, 0.50, 6.25}},
      // Checked 2026-10-08 against platform.claude.com/docs/en/about-claude/pricing.
      // (This row said $3 / $15 before, the Sonnet 4.x list price: it made the
      // spending limit bite at about two thirds of the real spend.) Cache read
      // is 5% of input on 5.5, not 10%.
      {"claude-sonnet-5-5", {2.00, 10.00, 0.10, 2.50}},
      // Haiku 5.5 launched 2026-10-07. These are its prices for prompts up to
      // 100,000 tokens, which every step of the agent is (a step is about 10-30k
      // tokens); a prompt over 100,000 tokens costs five times as much, and the
      // limit would then understate. About 20 times cheaper than Sonnet 5.5.
      {"claude-haiku-5-5", {0.10, 0.50, 0.01, 0.125}},
  };
  for (const Row& row : kTable) {
    if (row.model == model) {
      return row.prices;
    }
  }
  return std::nullopt;
}

}  // namespace zephyrus::agent

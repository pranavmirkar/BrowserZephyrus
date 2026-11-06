// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_EXTENSION_TOGGLE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_EXTENSION_TOGGLE_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/common/extension_id.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Profile;

namespace zephyrus {

// Whether the user may turn this extension on or off from the Extensions popup.
// An enterprise policy can pin an extension on (or off), and then the switch is
// shown but cannot be moved.
bool UserMayToggleExtension(Profile* profile,
                            const extensions::ExtensionId& id);

// Turns the extension on or off, a moment from now: the switch that asked for it
// is a child of a row the change is about to remove (turning off) or add
// (turning on), and it should be seen to move first. Never call the registrar
// from inside the switch's own callback.
void SetExtensionEnabledSoon(Profile* profile,
                             const extensions::ExtensionId& id,
                             bool enabled);

}  // namespace zephyrus

// The extensions the user has turned off, at the bottom of the Extensions
// popup, each with a switch to turn it back on. Without this, an extension
// turned off from the popup would vanish from it and could only be found again
// on the Extensions settings page.
//
// Only the ones the USER turned off: an extension disabled for another reason
// (blocklisted, waiting for a permission, managed) has a different way back.
class ZephyrusTurnedOffSection : public views::View,
                                 public extensions::ExtensionRegistryObserver {
  METADATA_HEADER(ZephyrusTurnedOffSection, views::View)

 public:
  explicit ZephyrusTurnedOffSection(Profile* profile);
  ZephyrusTurnedOffSection(const ZephyrusTurnedOffSection&) = delete;
  ZephyrusTurnedOffSection& operator=(const ZephyrusTurnedOffSection&) = delete;
  ~ZephyrusTurnedOffSection() override;

  // extensions::ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;

 private:
  void ScheduleRebuild();
  void Rebuild();

  raw_ptr<Profile> profile_;
  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  bool rebuild_pending_ = false;
  base::WeakPtrFactory<ZephyrusTurnedOffSection> weak_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_EXTENSION_TOGGLE_H_

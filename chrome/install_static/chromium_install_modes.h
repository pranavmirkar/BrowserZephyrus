// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Brand-specific types and constants for Chromium.

#ifndef CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_
#define CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_

#include <array>

#include "chrome/app/chrome_dll_resource.h"
#include "chrome/common/chrome_icon_resources_win.h"
#include "chrome/install_static/install_constants.h"

namespace install_static {

// The brand-specific company name to be included as a component of the install
// and user data directory paths. May be empty if no such dir is to be used.
inline constexpr wchar_t kCompanyPathName[] = L"";

// The brand-specific product name to be included as a component of the install
// and user data directory paths.
inline constexpr wchar_t kProductPathName[] = L"Zephyrus";

// The brand-specific safe browsing client name.
inline constexpr char kSafeBrowsingName[] = "chromium";

// Note: This list of indices must be kept in sync with the brand-specific
// resource strings in chrome/installer/util/prebuild/create_string_rc.
enum InstallConstantIndex {
  CHROMIUM_INDEX,
  NUM_INSTALL_MODES,
};

inline constexpr auto kInstallModes = std::to_array<InstallConstants>({
    // The primary (and only) install mode for Chromium.
    {
        .size = sizeof(InstallConstants),
        .index = CHROMIUM_INDEX,  // The one and only mode for Chromium.
        .install_switch =
            "",  // No install switch for the primary install mode.
        .install_suffix =
            L"",  // Empty install_suffix for the primary install mode.
        .logo_suffix = L"",  // No logo suffix for the primary install mode.
        .app_guid =
            L"",  // Empty app_guid since no integration with Google Update.
        .base_app_name = L"Zephyrus",              // A distinct base_app_name.
        .base_app_id = L"Zephyrus",                // A distinct base_app_id.
        .browser_prog_id_prefix = L"ZephyrusHTM",  // Browser ProgID prefix.
        .browser_prog_id_description =
            L"Zephyrus HTML Document",  // Browser ProgID description.
        .direct_launch_url_scheme = "zephyrus",
        .pdf_prog_id_prefix = L"ZephyrusPDF",  // PDF ProgID prefix.
        .pdf_prog_id_description =
            L"Zephyrus PDF Document",  // PDF ProgID description.
        .active_setup_guid =
            L"{EC280D0B-0B87-4A96-A568-BB6DEDC34139}",  // Active Setup
                                                        // GUID.
        .toast_activator_clsid = {0xC0B2F36D,
                                  0x4AA1,
                                  0x446E,
                                  {0x99, 0x4E, 0x4F, 0xA9, 0x91, 0xF6, 0x4E,
                                   0xCB}},  // Toast Activator CLSID.
        .elevator_clsid = {0xA43F5F93,
                           0xA17D,
                           0x4E9A,
                           {0xAB, 0x4D, 0x3C, 0x2D, 0x56, 0xBE, 0xE8,
                            0x5D}},  // Elevator CLSID.
        .elevator_iid = {0x99f58eaf,
                         0x1473,
                         0x4414,
                         {0x9c, 0xe2, 0x9b, 0x3f, 0x96, 0xc3, 0xaa,
                          0x41}},  // IElevator IID and TypeLib
        // {99F58EAF-1473-4414-9CE2-9B3F96C3AA41}.
        .tracing_service_clsid = {0xe83dbc77,
                                  0xa848,
                                  0x45f5,
                                  {0xbe, 0x8b, 0x72, 0x1b, 0x7a, 0xce, 0x15,
                                   0xa4}},  // SystemTraceSession CLSID.
        .tracing_service_iid = {0xa061a585,
                                0x15a2,
                                0x4692,
                                {0x8d, 0x1d, 0x28, 0xbc, 0x0d, 0x79, 0xdc,
                                 0x12}},  // ISystemTraceSessionChromium IID and
                                          // TypeLib
        .default_channel_name =
            L"",  // Empty default channel name since no update integration.
        .channel_strategy = ChannelStrategy::UNSUPPORTED,
        .supports_system_level = true,  // Supports system-level installs.
        .supports_set_as_default_browser =
            true,  // Supports in-product set as default browser UX.
        .app_icon_resource_index =
            icon_resources::kApplicationIndex,  // App icon resource index.
        .app_icon_resource_id = IDR_MAINFRAME,  // App icon resource id.
        .html_doc_icon_resource_index =
            icon_resources::kHtmlDocIndex,  // HTML doc icon resource index.
        .pdf_doc_icon_resource_index =
            icon_resources::kPDFDocIndex,  // PDF doc icon resource index.
        .sandbox_sid_prefix =
            L"S-1-15-2-3251537155-1984446955-2931258699-841473695-"
            L"1938553385-"
            L"924012148-",  // App container sid prefix for sandbox.
    },
});

}  // namespace install_static

#endif  // CHROME_INSTALL_STATIC_CHROMIUM_INSTALL_MODES_H_

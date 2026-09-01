// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_IMAGE_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_IMAGE_H_

#include <string>

#include "base/functional/callback_forward.h"
#include "ui/gfx/native_ui_types.h"

class Profile;

namespace gfx {
class ImageSkia;
}  // namespace gfx

// Zephyrus: custom photo icons for workspaces.
//
// A workspace can show a numeral, an emoji, or a PHOTO the user chose. This is
// the photo half.
//
// WHY THE ORIGINAL FILE IS NOT WHAT WE KEEP
// -----------------------------------------
// The obvious design is to remember the path the user picked and load it each
// time. It is also wrong twice over:
//
//   1. The file moves, gets renamed, or lives on a USB stick, and the icon
//      silently becomes a blank disc weeks later with no way to tell why.
//   2. It makes a decode of an ARBITRARY user-chosen file part of ordinary
//      startup, forever, rather than a one-time cost at the moment of picking.
//
// So picking copies: the image is decoded once, centre-cropped to a square,
// scaled down, and re-encoded as a small canonical PNG inside the profile
// directory. Only that file's basename is persisted. What we load afterwards is
// a file this code wrote, at a size this code chose.
//
// EVERY decode here still runs OUT OF PROCESS, via the data_decoder service.
// Image parsers are a classic memory-safety target, and "we wrote this PNG
// ourselves" is not a safety argument: the file sits in a user-writable
// directory and can be swapped between the write and the read.
namespace zephyrus {

// True if `name` is a filename this code could have produced.
//
// Stored names come from prefs, which is a plain user-writable file on disk.
// Anything read from it is untrusted input, and this one is about to be joined
// onto a directory path -- so it is checked against the shape we generate
// rather than merely scanned for "..". Reject-unless-recognised is the only
// version of this check that is not a guessing game.
bool IsValidWorkspaceImageName(const std::string& name);

// Runs the whole pick flow: file dialog, out-of-process decode, crop, scale,
// re-encode, write. `on_done` gets the new basename, or an empty string if the
// user cancelled or the file could not be read as an image.
//
// Self-owning -- the helper deletes itself once the dialog closes, so callers
// keep no handle. `on_done` must therefore be safe against the caller having
// gone away (bind it weakly).
void PickWorkspaceImage(Profile* profile,
                        int workspace_id,
                        gfx::NativeWindow parent,
                        base::OnceCallback<void(const std::string&)> on_done);

// Loads a stored image for display at `size_dip` device-independent pixels.
// `on_done` gets an empty ImageSkia if the file is missing or unreadable, which
// is a normal outcome (profile copied between machines, disk cleaned) and not
// an error worth surfacing.
void LoadWorkspaceImage(
    Profile* profile,
    const std::string& name,
    int size_dip,
    base::OnceCallback<void(const gfx::ImageSkia&)> on_done);

// Best-effort delete of a stored image. Used when a workspace is deleted or its
// icon replaced; failure is ignored, since a leftover file is cosmetic.
void DeleteWorkspaceImage(Profile* profile, const std::string& name);

}  // namespace zephyrus

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_ZEPHYRUS_WORKSPACE_IMAGE_H_

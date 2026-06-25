// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file folder_dialog.cpp
#include "platform/folder_dialog.h"
#include "core/logger.h"

#include <cstdlib>

#include "tinyfiledialogs.h"

namespace Vestige
{

std::string pickAssetFolder()
{
#ifndef _WIN32
    // Headless guard: with no display server, the underlying dialog tool
    // (zenity/kdialog) can hang or error rather than return cleanly. Skip it and
    // report "no folder" so the caller's fatal path fires promptly. (Windows
    // always has a desktop.)
    const char* dpy = std::getenv("DISPLAY");
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    if ((dpy == nullptr || dpy[0] == '\0') && (wl == nullptr || wl[0] == '\0'))
    {
        Logger::warning("pickAssetFolder: no display server "
                        "($DISPLAY/$WAYLAND_DISPLAY unset) — skipping folder picker");
        return {};
    }
#endif
    // nullptr default path = the tool's own default. Returns nullptr on cancel
    // or when no dialog backend is available.
    const char* selected =
        tinyfd_selectFolderDialog("Locate the Vestige assets folder", nullptr);
    return (selected != nullptr) ? std::string(selected) : std::string{};
}

} // namespace Vestige

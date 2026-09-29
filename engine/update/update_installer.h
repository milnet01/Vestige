// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_installer.h
/// @brief How this copy of Vestige was installed, and so whether and how it
///        can update itself in place (3D_E-0729, spec §4.6).
#pragma once

#include <string_view>

namespace Vestige::Update
{

/// @brief The release package this build came from.
enum class InstallKind
{
    None,        ///< Not a release build (a developer build): never installs.
    AppImage,    ///< Linux AppImage: replaced in place.
    WindowsZip,  ///< Windows zip: the install folder is swapped by a helper.
    Tarball,     ///< Linux tarball: notes and a download link only.
};

/// @brief Pure decision behind detectInstall().
/// @param packageStamp The VESTIGE_PACKAGE value compiled in: "linux",
///        "windows-zip", or empty for a build release.yml did not make.
/// @param appimageEnv  $APPIMAGE, or nullptr when unset.
/// @param appdirEnv    $APPDIR, or nullptr when unset.
/// @param selfExePath  Absolute path of the running executable.
///
/// A "linux" build is an AppImage only when the AppImage runtime's variables
/// are set AND this executable lies under $APPDIR: child processes inherit
/// both variables, so a tarball build started from inside another AppImage
/// must not take that app's path for its own.
InstallKind detectInstall(std::string_view packageStamp, const char* appimageEnv,
                          const char* appdirEnv, std::string_view selfExePath);

/// @brief detectInstall() for this process: the compiled-in stamp, the
///        environment and the running executable's path.
InstallKind detectInstall();

}  // namespace Vestige::Update

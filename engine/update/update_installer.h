// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_installer.h
/// @brief How this copy of Vestige was installed, and so whether and how it
///        can update itself in place (3D_E-0729, spec §4.6).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

/// @brief Where this copy lives and how to start it again.
struct InstallContext
{
    std::filesystem::path appimagePath;     ///< $APPIMAGE (AppImage only).
    std::filesystem::path installDir;       ///< Folder holding vestige.exe (Windows zip).
    std::vector<std::string> relaunchArgs;  ///< The user's own arguments.
};

/// @brief Puts a verified update in place and starts it.
class IInstaller
{
public:
    virtual ~IInstaller() = default;

    /// @brief AppImage: replaces the file. Windows: stages the new folder
    ///        beside the install. Returns an error, or empty on success; on
    ///        error the installed version is untouched (INV-5).
    virtual std::string apply(const std::vector<std::uint8_t>& verified,
                              const std::string& version) = 0;

    /// @brief Starts the new version. AppImage: execv, which does not return
    ///        on success. Windows: starts the swap helper; the caller then
    ///        exits. Returns an error if it could not.
    virtual std::string relaunch() = 0;
};

/// @brief The installer for @a kind; null for None and Tarball (INV-7).
std::unique_ptr<IInstaller> installerFor(InstallKind kind, InstallContext context);

/// @brief This process's context: $APPIMAGE, the executable's folder, and the
///        arguments recorded by setLaunchArguments().
InstallContext currentInstallContext();

/// @brief Records main()'s arguments so an update can relaunch with them.
void setLaunchArguments(int argc, char** argv);

/// @brief The user's own arguments: argv without the program name, and
///        without the `--assets <$APPDIR/...>` pair packaging/AppRun puts in
///        front (the new AppRun adds its own; the old path would point the new
///        build at the old build's assets).
std::vector<std::string> userArguments(const std::vector<std::string>& argv,
                                       const char* appdirEnv);

/// @brief Extracts a zip held in memory under @a dest. An entry whose path
///        would land outside @a dest is refused and nothing more is written.
///        Returns an error, or empty on success.
std::string extractZip(const std::vector<std::uint8_t>& zip, const std::filesystem::path& dest);

/// @brief Windows zip only: deletes `<install>.old`, left by the swap helper,
///        once this build has reached its first frame. No-op elsewhere.
void cleanupAfterUpdate(InstallKind kind, const std::filesystem::path& installDir);

}  // namespace Vestige::Update

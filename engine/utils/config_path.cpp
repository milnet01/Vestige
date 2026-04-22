// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file config_path.cpp
/// @brief Cross-platform user-config directory resolution.
#include "utils/config_path.h"

#include <cstdlib>

#ifdef _WIN32
  #include <windows.h>
  #include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace Vestige
{
namespace ConfigPath
{

namespace
{

constexpr const char* kAppDirName = "vestige";

#ifdef _WIN32

// Windows: %LOCALAPPDATA%/Vestige/
fs::path resolvePlatformConfigDir()
{
    PWSTR wpath = nullptr;
    HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &wpath);
    fs::path dir;
    if (SUCCEEDED(hr) && wpath != nullptr)
    {
        dir = fs::path(wpath) / "Vestige";
    }
    else
    {
        // Fallback to %USERPROFILE%\AppData\Local — covers environments
        // where the Known Folder API is unavailable (rare, but matches
        // the spirit of the Linux /tmp fallback).
        const char* userProfile = std::getenv("USERPROFILE");
        if (userProfile != nullptr && userProfile[0] != '\0')
        {
            dir = fs::path(userProfile) / "AppData" / "Local" / "Vestige";
        }
        else
        {
            dir = fs::temp_directory_path() / "Vestige";
        }
    }
    if (wpath != nullptr)
    {
        ::CoTaskMemFree(wpath);
    }
    return dir;
}

#else

// POSIX: $XDG_CONFIG_HOME/vestige → $HOME/.config/vestige → /tmp/vestige.
fs::path resolvePlatformConfigDir()
{
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfig != nullptr && xdgConfig[0] != '\0')
    {
        return fs::path(xdgConfig) / kAppDirName;
    }

    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0')
    {
        return fs::path(home) / ".config" / kAppDirName;
    }

    return fs::path("/tmp") / kAppDirName;
}

#endif

} // namespace

fs::path getConfigDir()
{
    return resolvePlatformConfigDir();
}

fs::path getConfigFile(const std::string& filename)
{
    return getConfigDir() / filename;
}

} // namespace ConfigPath
} // namespace Vestige

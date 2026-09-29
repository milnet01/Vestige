// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_installer.cpp
#include "update/update_installer.h"

#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef VESTIGE_PACKAGE
#define VESTIGE_PACKAGE ""
#endif

namespace Vestige::Update
{

namespace
{

/// The running executable's absolute path, or empty if it cannot be read.
std::string selfExecutablePath()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (n == 0 || n >= std::size(buffer))
    {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer, n)).string();
#else
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : p.string();
#endif
}

/// True when @a path is @a dir or lies beneath it, compared lexically after
/// normalising both.
bool isUnder(std::string_view path, std::string_view dir)
{
    namespace fs = std::filesystem;
    const fs::path p = fs::path(std::string(path)).lexically_normal();
    const fs::path d = fs::path(std::string(dir)).lexically_normal();
    auto pi = p.begin();
    for (auto di = d.begin(); di != d.end(); ++di, ++pi)
    {
        if (di->empty())
        {
            continue;  // a trailing separator on the directory
        }
        if (pi == p.end() || *pi != *di)
        {
            return false;
        }
    }
    return true;
}

}  // namespace

InstallKind detectInstall(std::string_view packageStamp, const char* appimageEnv,
                          const char* appdirEnv, std::string_view selfExePath)
{
    if (packageStamp == "windows-zip")
    {
        return InstallKind::WindowsZip;
    }
    if (packageStamp != "linux")
    {
        return InstallKind::None;
    }
    const bool runtimeSet = appimageEnv && *appimageEnv && appdirEnv && *appdirEnv;
    if (runtimeSet && !selfExePath.empty() && isUnder(selfExePath, appdirEnv))
    {
        return InstallKind::AppImage;
    }
    return InstallKind::Tarball;
}

InstallKind detectInstall()
{
    return detectInstall(VESTIGE_PACKAGE, std::getenv("APPIMAGE"), std::getenv("APPDIR"),
                         selfExecutablePath());
}

}  // namespace Vestige::Update

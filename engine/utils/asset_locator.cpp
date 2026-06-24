// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file asset_locator.cpp
#include "utils/asset_locator.h"

#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#  include <limits.h>
#endif

namespace Vestige
{

std::filesystem::path executableDir()
{
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
    {
        return {};
    }
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    std::filesystem::path self =
        std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
    {
        return {};
    }
    return self.parent_path();
#endif
}

std::vector<std::filesystem::path> assetSearchCandidates(
    const std::filesystem::path& exeDir,
    const std::filesystem::path& cwd)
{
    std::vector<std::filesystem::path> candidates;
    if (!exeDir.empty())
    {
        candidates.push_back(exeDir / "assets");
        // Installed / AppDir layout: <prefix>/bin/vestige + <prefix>/share/vestige/assets.
        candidates.push_back(exeDir / ".." / "share" / "vestige" / "assets");
    }
    candidates.push_back(cwd / "assets");   // back-compat: cwd-relative "assets"
    return candidates;
}

std::filesystem::path firstValidAssetDir(
    const std::vector<std::filesystem::path>& candidates,
    const std::function<bool(const std::filesystem::path&)>& isValid)
{
    for (const auto& c : candidates)
    {
        if (isValid(c))
        {
            return c;
        }
    }
    return {};
}

bool isAssetDir(const std::filesystem::path& dir)
{
    std::error_code ec;
    return std::filesystem::exists(dir / "shaders" / "scene.vert.glsl", ec);
}

std::string resolveAssetPath(const std::string& cliOverride)
{
    // 1. Explicit --assets wins, honoured as-is (do not second-guess the user;
    //    if it is wrong the engine surfaces a clear load error).
    if (!cliOverride.empty())
    {
        return cliOverride;
    }

    // 2. Environment override.
    if (const char* env = std::getenv("VESTIGE_ASSETS"))
    {
        if (env[0] != '\0')
        {
            return env;
        }
    }

    // 3–5. Validated auto-search (binary-relative first, then cwd).
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec)
    {
        cwd = ".";
    }
    auto candidates = assetSearchCandidates(executableDir(), cwd);
    auto found = firstValidAssetDir(candidates, isAssetDir);
    return found.empty() ? std::string() : found.lexically_normal().string();
}

} // namespace Vestige

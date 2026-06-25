// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file asset_locator.cpp
#include "utils/asset_locator.h"

#include "core/logger.h"
#include "utils/atomic_write.h"
#include "utils/config_path.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
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

// --- Part B: folder-picker fallback + remembered asset root -----------------

namespace
{
// The single config key + the user-config filename for the remembered root.
constexpr const char* kAssetPathKey = "assets.path";
constexpr const char* kConfigFile = "asset_root";
} // namespace

AssetRootChoice chooseAssetRoot(
    const std::string& autoResolved,
    const std::string& remembered,
    const std::function<bool(const std::string&)>& isValid,
    const std::function<std::string()>& pickFolder)
{
    if (!autoResolved.empty())
    {
        return {autoResolved, false};  // Part A already found (and validated) it
    }
    if (!remembered.empty() && isValid(remembered))
    {
        return {remembered, false};
    }
    // Picker loop: re-prompt past an invalid pick; cancel/unavailable ("") exits.
    for (;;)
    {
        std::string picked = pickFolder();
        if (picked.empty())
        {
            return {std::string{}, false};  // cancelled or no dialog available
        }
        if (isValid(picked))
        {
            return {picked, true};  // fresh, valid choice — persist it
        }
        // else: invalid folder, loop and prompt again.
    }
}

std::optional<std::string> parseAssetPathConfig(const std::string& contents)
{
    std::istringstream in(contents);
    std::string line;
    const std::string prefix = std::string(kAssetPathKey) + "=";
    while (std::getline(in, line))
    {
        auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;  // not a key=value line
        }
        if (line.compare(0, prefix.size(), prefix) != 0)
        {
            continue;  // some other key — skip (forward-compatible)
        }
        std::string value = line.substr(eq + 1);
        // Strip a trailing CR (Windows line ending); getline already dropped \n.
        if (!value.empty() && value.back() == '\r')
        {
            value.pop_back();
        }
        if (value.empty())
        {
            return std::nullopt;
        }
        return value;
    }
    return std::nullopt;
}

std::optional<std::string> readRememberedAssetPath()
{
    std::error_code ec;
    std::filesystem::path file = ConfigPath::getConfigFile(kConfigFile);
    if (!std::filesystem::exists(file, ec))
    {
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseAssetPathConfig(ss.str());
}

bool writeRememberedAssetPath(const std::string& path)
{
    // AtomicWrite::writeFile creates parent dirs + does the durable
    // tmp→fsync→rename. DirFsyncFailed means the file IS written (only crash
    // durability is unguaranteed) — treat it as success.
    std::string contents = std::string(kAssetPathKey) + "=" + path + "\n";
    AtomicWrite::Status st =
        AtomicWrite::writeFile(ConfigPath::getConfigFile(kConfigFile), contents);
    if (st == AtomicWrite::Status::Ok || st == AtomicWrite::Status::DirFsyncFailed)
    {
        return true;
    }
    Logger::warning(std::string("writeRememberedAssetPath: could not persist asset "
                                "root (") + AtomicWrite::describe(st) + ")");
    return false;
}

} // namespace Vestige

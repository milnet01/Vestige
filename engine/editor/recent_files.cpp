// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file recent_files.cpp
/// @brief Recent files manager implementation.
#include "editor/recent_files.h"
#include "core/logger.h"
#include "utils/config_path.h"
#include "utils/json_size_cap.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Vestige
{

void RecentFiles::load()
{
    m_paths.clear();

    fs::path storagePath = getStoragePath();
    if (!fs::exists(storagePath))
    {
        return;
    }

    // Recent-files list is tiny; cap at 1 MB to reject pathological inputs.
    auto parsed = JsonSizeCap::loadJsonWithSizeCap(
        storagePath.string(), "RecentFiles", 1ULL * 1024ULL * 1024ULL);
    if (!parsed)
    {
        return;
    }
    const json& data = *parsed;

    if (!data.contains("recent_files") || !data["recent_files"].is_array())
    {
        return;
    }

    for (const auto& entry : data["recent_files"])
    {
        if (!entry.is_string())
        {
            continue;
        }

        fs::path p(entry.get<std::string>());

        // Prune entries for files that no longer exist
        if (fs::exists(p))
        {
            m_paths.push_back(p);
        }
    }

    if (m_paths.size() > MAX_ENTRIES)
    {
        m_paths.resize(MAX_ENTRIES);
    }
}

void RecentFiles::save() const
{
    fs::path storagePath = getStoragePath();

    std::error_code ec;
    fs::create_directories(storagePath.parent_path(), ec);
    if (ec)
    {
        Logger::warning("RecentFiles: could not create directory "
                        + storagePath.parent_path().string());
        return;
    }

    json data;
    json paths = json::array();
    for (const auto& p : m_paths)
    {
        paths.push_back(p.string());
    }
    data["recent_files"] = paths;

    std::ofstream file(storagePath, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        Logger::warning("RecentFiles: could not write " + storagePath.string());
        return;
    }

    file << data.dump(2);
}

void RecentFiles::addPath(const std::filesystem::path& path)
{
    fs::path absPath = fs::absolute(path);

    // Remove existing duplicate
    m_paths.erase(
        std::remove(m_paths.begin(), m_paths.end(), absPath),
        m_paths.end());

    // Insert at front (most recent first)
    m_paths.insert(m_paths.begin(), absPath);

    if (m_paths.size() > MAX_ENTRIES)
    {
        m_paths.resize(MAX_ENTRIES);
    }

    save();
}

const std::vector<std::filesystem::path>& RecentFiles::getPaths() const
{
    return m_paths;
}

void RecentFiles::removePath(const std::filesystem::path& path)
{
    fs::path absPath = fs::absolute(path);
    m_paths.erase(
        std::remove(m_paths.begin(), m_paths.end(), absPath),
        m_paths.end());
    save();
}

void RecentFiles::clear()
{
    m_paths.clear();
    save();
}

fs::path RecentFiles::getConfigDir()
{
    // Thin forward to the shared helper (slice 13.1) so Settings,
    // save-games, and any other per-user persistence use one resolver.
    return ConfigPath::getConfigDir();
}

fs::path RecentFiles::getStoragePath()
{
    return getConfigDir() / "recent_files.json";
}

} // namespace Vestige

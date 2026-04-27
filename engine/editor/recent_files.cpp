// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file recent_files.cpp
/// @brief Recent files manager implementation.
#include "editor/recent_files.h"
#include "core/logger.h"
#include "utils/atomic_write.h"
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

    // Phase 10.9 Slice 12 Ed4 — route through the project's atomic-write
    // helper (write-tmp + fsync + rename + fsync-dir). Prevents a torn
    // recent-files list if the editor is killed mid-flush; the previous
    // truncate+stream pattern could leave an empty or half-written JSON
    // that the next load() would reject.
    const AtomicWrite::Status s =
        AtomicWrite::writeFile(storagePath, data.dump(2));
    if (s != AtomicWrite::Status::Ok)
    {
        Logger::warning(std::string("RecentFiles: ")
                        + AtomicWrite::describe(s)
                        + " for " + storagePath.string());
    }
}

void RecentFiles::addPath(const std::filesystem::path& path)
{
    // AUDIT Ed10 — `fs::absolute(path)` throws on invalid-UTF-8 input on
    // Windows and on a small handful of POSIX failures (e.g. retrieving
    // CWD when it has been deleted). The throw escapes the ImGui frame
    // and crashes the editor. Use the error_code overload and fall back
    // to the caller-supplied path.
    std::error_code ec;
    fs::path absPath = fs::absolute(path, ec);
    if (ec)
    {
        Logger::warning("RecentFiles: could not canonicalise path '"
            + path.string() + "': " + ec.message()
            + " — recording the path as-is");
        absPath = path;
    }

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

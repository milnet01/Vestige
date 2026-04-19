// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file file_watcher.cpp
/// @brief FileWatcher implementation — polls filesystem for asset changes.
#include "resource/file_watcher.h"
#include "core/logger.h"

#include <algorithm>

namespace Vestige
{

void FileWatcher::initialize(const std::string& rootPath,
                              const std::vector<std::string>& extensions)
{
    m_rootPath = rootPath;
    m_extensions = extensions;
    m_initialized = true;
    m_timeSinceLastPoll = 0.0f;

    // Initial scan to populate timestamps
    scanDirectory();

    Logger::info("FileWatcher: watching " + std::to_string(m_timestamps.size())
                 + " files in " + rootPath);
}

void FileWatcher::update(float deltaTime)
{
    if (!m_initialized)
    {
        return;
    }

    m_timeSinceLastPoll += deltaTime;
    if (m_timeSinceLastPoll < m_pollInterval)
    {
        return;
    }

    m_timeSinceLastPoll = 0.0f;
    rescan();
}

void FileWatcher::rescan()
{
    if (!m_initialized)
    {
        return;
    }

    namespace fs = std::filesystem;

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(m_rootPath, ec);
         it != fs::recursive_directory_iterator(); ++it)
    {
        if (ec)
        {
            break;
        }

        if (!it->is_regular_file(ec))
        {
            continue;
        }

        const std::string path = it->path().string();

        // Check extension filter
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool matches = m_extensions.empty(); // empty = watch all
        for (const auto& allowed : m_extensions)
        {
            if (ext == allowed)
            {
                matches = true;
                break;
            }
        }
        if (!matches)
        {
            continue;
        }

        auto writeTime = fs::last_write_time(it->path(), ec);
        if (ec)
        {
            continue;
        }

        auto existing = m_timestamps.find(path);
        if (existing == m_timestamps.end())
        {
            m_timestamps[path] = writeTime;
            Logger::debug("FileWatcher: new file " + path);
        }
        else if (writeTime != existing->second)
        {
            existing->second = writeTime;
            Logger::debug("FileWatcher: modified " + path);
        }
    }
}

void FileWatcher::scanDirectory()
{
    namespace fs = std::filesystem;

    m_timestamps.clear();

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(m_rootPath, ec);
         it != fs::recursive_directory_iterator(); ++it)
    {
        if (ec || !it->is_regular_file(ec))
        {
            continue;
        }

        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool matches = m_extensions.empty();
        for (const auto& allowed : m_extensions)
        {
            if (ext == allowed)
            {
                matches = true;
                break;
            }
        }
        if (!matches)
        {
            continue;
        }

        auto writeTime = fs::last_write_time(it->path(), ec);
        if (!ec)
        {
            m_timestamps[it->path().string()] = writeTime;
        }
    }
}

} // namespace Vestige

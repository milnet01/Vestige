// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file file_watcher.h
/// @brief Monitors asset directories for file changes and triggers reload callbacks.
#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Monitors a set of file paths for changes using filesystem polling.
///
/// Each tick (typically every 2 seconds), checks last_write_time against
/// stored timestamps. Fires callbacks when files are modified or added.
class FileWatcher
{
public:
    /// @brief Callback type: receives the path of the changed file.
    using ChangeCallback = std::function<void(const std::string& path)>;

    /// @brief Starts watching files in the given root directory.
    /// @param rootPath Directory to watch (e.g., "assets/").
    /// @param extensions File extensions to track (e.g., {".png", ".jpg", ".gltf"}).
    void initialize(const std::string& rootPath,
                    const std::vector<std::string>& extensions);

    /// @brief Polls the filesystem for changes. Call once per frame.
    /// Actual polling only occurs every m_pollInterval seconds.
    /// @param deltaTime Time since last frame.
    void update(float deltaTime);

    /// @brief Force an immediate rescan (ignores poll interval).
    void rescan();

private:
    void scanDirectory();

    std::string m_rootPath;
    std::vector<std::string> m_extensions;
    ChangeCallback m_onChanged;

    /// @brief Map of file path to last known write time.
    std::unordered_map<std::string, std::filesystem::file_time_type> m_timestamps;

    float m_timeSinceLastPoll = 0.0f;
    float m_pollInterval = 2.0f;  ///< Seconds between filesystem checks.
    bool m_initialized = false;
};

} // namespace Vestige

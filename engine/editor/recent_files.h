// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file recent_files.h
/// @brief Manages a persistent list of recently opened scene files.
#pragma once

#include <filesystem>
#include <vector>

namespace Vestige
{

/// @brief Manages a list of recently opened/saved scene file paths.
///
/// Persists the list to $XDG_CONFIG_HOME/vestige/recent_files.json (or
/// ~/.config/vestige/recent_files.json if XDG_CONFIG_HOME is unset).
/// Entries for files that no longer exist are pruned on load.
class RecentFiles
{
public:
    /// @brief Maximum number of recent file entries.
    static constexpr size_t MAX_ENTRIES = 10;

    /// @brief Loads the recent files list from disk.
    void load();

    /// @brief Saves the current list to disk.
    void save() const;

    /// @brief Adds a path to the front of the list (removes duplicates).
    void addPath(const std::filesystem::path& path);

    /// @brief Gets the list of recent file paths (most recent first).
    const std::vector<std::filesystem::path>& getPaths() const;

    /// @brief Removes a specific path from the list.
    void removePath(const std::filesystem::path& path);

    /// @brief Clears all recent file entries.
    void clear();

    /// @brief Returns the Vestige config directory ($XDG_CONFIG_HOME/vestige/).
    static std::filesystem::path getConfigDir();

private:
    /// @brief Returns the path to the recent files JSON storage.
    static std::filesystem::path getStoragePath();

    std::vector<std::filesystem::path> m_paths;
};

} // namespace Vestige

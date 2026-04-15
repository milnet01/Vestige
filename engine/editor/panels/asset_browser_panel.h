// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file asset_browser_panel.h
/// @brief Asset browser panel — folder navigation and thumbnail grid for project assets.
#pragma once

#include <glad/gl.h>

#include <chrono>
#include <filesystem>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

class ResourceManager;

/// @brief Asset type classification based on file extension.
enum class AssetType
{
    UNKNOWN,
    TEXTURE,
    MESH,
    PREFAB,
    SHADER,
    DIRECTORY
};

/// @brief A single entry in the asset browser (file or directory).
struct AssetEntry
{
    std::string name;
    std::string fullPath;
    std::string extension;
    AssetType type = AssetType::UNKNOWN;
    bool isDirectory = false;
    GLuint thumbnailId = 0;
    bool thumbnailLoaded = false;
};

/// @brief Draws a file/folder browser with thumbnail grid and drag-drop support.
class AssetBrowserPanel
{
public:
    /// @brief Initializes the asset browser.
    /// @param assetsPath Root assets directory path.
    /// @param resources ResourceManager for texture loading.
    void initialize(const std::string& assetsPath, ResourceManager& resources);

    /// @brief Draws the asset browser panel contents.
    void draw();

private:
    void drawSearchBar();
    void drawBreadcrumbs();
    void drawFolderTree(const std::string& path, const std::string& displayName);
    void drawGrid();
    void scanDirectory(const std::string& path);
    void processOneThumbnail();
    void pollFileChanges();

    static AssetType classifyExtension(const std::string& ext);

    // State
    std::string m_rootPath;
    std::string m_currentPath;
    char m_searchBuffer[256] = {};
    float m_thumbnailSize = 64.0f;
    bool m_initialized = false;

    // Directory entries for the current view
    std::vector<AssetEntry> m_entries;

    // Thumbnail loading queue
    std::queue<size_t> m_thumbnailQueue;  ///< Indices into m_entries needing thumbnails

    // Thumbnail cache: full path → GL texture ID
    std::unordered_map<std::string, GLuint> m_thumbnailCache;

    // File watching (polling)
    std::unordered_map<std::string, std::filesystem::file_time_type> m_fileTimestamps;
    std::chrono::steady_clock::time_point m_lastPoll;

    ResourceManager* m_resources = nullptr;

    // Double-click routing to asset viewers
    std::string m_pendingOpenPath;
    AssetType m_pendingOpenType = AssetType::UNKNOWN;

public:
    /// @brief Returns true if a double-clicked asset is pending for viewer open.
    bool hasPendingOpen() const { return !m_pendingOpenPath.empty(); }

    /// @brief Consumes the pending open request.
    /// @param outPath Set to the asset file path.
    /// @param outType Set to the asset type.
    void consumePendingOpen(std::string& outPath, AssetType& outType)
    {
        outPath = m_pendingOpenPath;
        outType = m_pendingOpenType;
        m_pendingOpenPath.clear();
        m_pendingOpenType = AssetType::UNKNOWN;
    }
};

} // namespace Vestige

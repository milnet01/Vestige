/// @file asset_browser_panel.cpp
/// @brief Asset browser panel implementation.
#include "editor/panels/asset_browser_panel.h"
#include "resource/resource_manager.h"
#include "renderer/texture.h"
#include "core/logger.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>

namespace Vestige
{

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void AssetBrowserPanel::initialize(const std::string& assetsPath,
                                   ResourceManager& resources)
{
    m_rootPath = assetsPath;
    m_currentPath = assetsPath;
    m_resources = &resources;
    m_initialized = true;

    scanDirectory(m_currentPath);
    Logger::info("AssetBrowserPanel initialized: " + assetsPath);
}

// ---------------------------------------------------------------------------
// Asset type classification
// ---------------------------------------------------------------------------

AssetType AssetBrowserPanel::classifyExtension(const std::string& ext)
{
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
        ext == ".bmp" || ext == ".tga" || ext == ".hdr" || ext == ".exr")
    {
        return AssetType::TEXTURE;
    }
    if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx")
    {
        return AssetType::MESH;
    }
    if (ext == ".json")
    {
        return AssetType::PREFAB;
    }
    if (ext == ".vert" || ext == ".frag" || ext == ".glsl" ||
        ext == ".comp" || ext == ".geom")
    {
        return AssetType::SHADER;
    }
    return AssetType::UNKNOWN;
}

// ---------------------------------------------------------------------------
// Directory scanning
// ---------------------------------------------------------------------------

void AssetBrowserPanel::scanDirectory(const std::string& path)
{
    m_entries.clear();
    // Clear the thumbnail queue for the new directory
    while (!m_thumbnailQueue.empty())
    {
        m_thumbnailQueue.pop();
    }

    namespace fs = std::filesystem;

    if (!fs::exists(path) || !fs::is_directory(path))
    {
        return;
    }

    for (const auto& entry : fs::directory_iterator(path))
    {
        AssetEntry ae;
        ae.fullPath = entry.path().string();
        ae.name = entry.path().filename().string();

        // Skip hidden files
        if (!ae.name.empty() && ae.name[0] == '.')
        {
            continue;
        }

        if (entry.is_directory())
        {
            ae.isDirectory = true;
            ae.type = AssetType::DIRECTORY;
        }
        else if (entry.is_regular_file())
        {
            ae.extension = entry.path().extension().string();
            // Lowercase the extension for comparison
            std::transform(ae.extension.begin(), ae.extension.end(),
                           ae.extension.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            ae.type = classifyExtension(ae.extension);

            // Check thumbnail cache
            auto it = m_thumbnailCache.find(ae.fullPath);
            if (it != m_thumbnailCache.end())
            {
                ae.thumbnailId = it->second;
                ae.thumbnailLoaded = true;
            }
        }

        m_entries.push_back(std::move(ae));
    }

    // Sort: directories first, then alphabetically
    std::sort(m_entries.begin(), m_entries.end(),
        [](const AssetEntry& a, const AssetEntry& b)
        {
            if (a.isDirectory != b.isDirectory)
            {
                return a.isDirectory;
            }
            return a.name < b.name;
        });

    // Queue texture thumbnails for loading
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (m_entries[i].type == AssetType::TEXTURE && !m_entries[i].thumbnailLoaded)
        {
            m_thumbnailQueue.push(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Thumbnail loading (one per frame)
// ---------------------------------------------------------------------------

void AssetBrowserPanel::processOneThumbnail()
{
    if (m_thumbnailQueue.empty() || !m_resources)
    {
        return;
    }

    size_t idx = m_thumbnailQueue.front();
    m_thumbnailQueue.pop();

    if (idx >= m_entries.size())
    {
        return;
    }

    auto& entry = m_entries[idx];
    if (entry.thumbnailLoaded || entry.type != AssetType::TEXTURE)
    {
        return;
    }

    // Load texture via ResourceManager (cached, so no duplicate loading)
    bool isLinear = false; // Display as sRGB for browser thumbnails
    auto tex = m_resources->loadTexture(entry.fullPath, isLinear);
    if (tex && tex->isLoaded())
    {
        entry.thumbnailId = tex->getId();
        entry.thumbnailLoaded = true;
        m_thumbnailCache[entry.fullPath] = tex->getId();
    }
}

// ---------------------------------------------------------------------------
// File change polling
// ---------------------------------------------------------------------------

void AssetBrowserPanel::pollFileChanges()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastPoll < std::chrono::seconds(2))
    {
        return;
    }
    m_lastPoll = now;

    namespace fs = std::filesystem;

    if (!fs::exists(m_currentPath))
    {
        return;
    }

    bool needsRescan = false;

    // Build current snapshot
    std::unordered_map<std::string, fs::file_time_type> current;
    for (const auto& entry : fs::directory_iterator(m_currentPath))
    {
        if (entry.is_regular_file() || entry.is_directory())
        {
            std::string path = entry.path().string();
            current[path] = entry.is_regular_file()
                ? entry.last_write_time()
                : fs::file_time_type{};
        }
    }

    // Check for changes
    if (current.size() != m_fileTimestamps.size())
    {
        needsRescan = true;
    }
    else
    {
        for (const auto& [path, time] : current)
        {
            auto it = m_fileTimestamps.find(path);
            if (it == m_fileTimestamps.end() || it->second != time)
            {
                needsRescan = true;
                break;
            }
        }
    }

    m_fileTimestamps = std::move(current);

    if (needsRescan)
    {
        scanDirectory(m_currentPath);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void AssetBrowserPanel::draw()
{
    if (!m_initialized)
    {
        ImGui::Text("Asset browser not initialized.");
        return;
    }

    // Process one thumbnail per frame
    processOneThumbnail();

    // Poll for file changes
    pollFileChanges();

    drawSearchBar();
    drawBreadcrumbs();

    ImGui::Separator();

    // Two-column layout: folder tree on left, grid on right
    float treeWidth = 150.0f;

    ImGui::BeginChild("##FolderTree", ImVec2(treeWidth, 0), true);
    drawFolderTree(m_rootPath, "assets");
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##AssetGrid", ImVec2(0, 0), true);
    drawGrid();
    ImGui::EndChild();
}

void AssetBrowserPanel::drawSearchBar()
{
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
    if (ImGui::InputTextWithHint("##Search", "Search assets...",
                                  m_searchBuffer, sizeof(m_searchBuffer)))
    {
        // Search filter is applied in drawGrid
    }
    ImGui::SameLine();

    // Thumbnail size slider
    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderFloat("##Size", &m_thumbnailSize, 32.0f, 128.0f, "%.0f");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Thumbnail size");
    }
}

void AssetBrowserPanel::drawBreadcrumbs()
{
    namespace fs = std::filesystem;

    // Build breadcrumb segments from root to current
    fs::path rootP(m_rootPath);
    fs::path currentP(m_currentPath);

    // Calculate relative path from root
    std::string relative;
    if (currentP.string().find(rootP.string()) == 0)
    {
        relative = currentP.string().substr(rootP.string().size());
    }

    // Root button
    if (ImGui::SmallButton("assets"))
    {
        m_currentPath = m_rootPath;
        scanDirectory(m_currentPath);
    }

    // Path segments
    if (!relative.empty())
    {
        fs::path relP(relative);
        std::string accumulated = m_rootPath;
        for (const auto& segment : relP)
        {
            std::string seg = segment.string();
            if (seg.empty() || seg == "/")
            {
                continue;
            }
            accumulated += "/" + seg;
            ImGui::SameLine();
            ImGui::TextDisabled(">");
            ImGui::SameLine();
            if (ImGui::SmallButton(seg.c_str()))
            {
                m_currentPath = accumulated;
                scanDirectory(m_currentPath);
            }
        }
    }
}

void AssetBrowserPanel::drawFolderTree(const std::string& path,
                                       const std::string& displayName)
{
    namespace fs = std::filesystem;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (path == m_currentPath)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // Check if directory has subdirectories
    bool hasSubdirs = false;
    if (fs::exists(path) && fs::is_directory(path))
    {
        for (const auto& entry : fs::directory_iterator(path))
        {
            if (entry.is_directory())
            {
                std::string name = entry.path().filename().string();
                if (!name.empty() && name[0] != '.')
                {
                    hasSubdirs = true;
                    break;
                }
            }
        }
    }

    if (!hasSubdirs)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    bool open = ImGui::TreeNodeEx(displayName.c_str(), flags);

    // Click to navigate
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        m_currentPath = path;
        scanDirectory(m_currentPath);
    }

    if (open)
    {
        if (fs::exists(path) && fs::is_directory(path))
        {
            // Collect and sort subdirectories
            std::vector<std::string> subdirs;
            for (const auto& entry : fs::directory_iterator(path))
            {
                if (entry.is_directory())
                {
                    std::string name = entry.path().filename().string();
                    if (!name.empty() && name[0] != '.')
                    {
                        subdirs.push_back(name);
                    }
                }
            }
            std::sort(subdirs.begin(), subdirs.end());

            for (const auto& subdir : subdirs)
            {
                drawFolderTree(path + "/" + subdir, subdir);
            }
        }

        ImGui::TreePop();
    }
}

void AssetBrowserPanel::drawGrid()
{
    float panelWidth = ImGui::GetContentRegionAvail().x;
    float cellSize = m_thumbnailSize + 16.0f;
    int columns = std::max(1, static_cast<int>(panelWidth / cellSize));

    // Back button if not at root
    if (m_currentPath != m_rootPath)
    {
        if (ImGui::Button("<- Back", ImVec2(cellSize, 24.0f)))
        {
            namespace fs = std::filesystem;
            m_currentPath = fs::path(m_currentPath).parent_path().string();
            scanDirectory(m_currentPath);
            return;
        }
    }

    // Filter check
    std::string searchStr(m_searchBuffer);
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    int col = 0;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const auto& entry = m_entries[i];

        // Apply search filter
        if (!searchStr.empty())
        {
            std::string lowerName = entry.name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lowerName.find(searchStr) == std::string::npos)
            {
                continue;
            }
        }

        ImGui::PushID(static_cast<int>(i));

        ImGui::BeginGroup();

        // Draw thumbnail or type label
        ImVec2 thumbSize(m_thumbnailSize, m_thumbnailSize);

        if (entry.isDirectory)
        {
            // Folder "icon" (colored button)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.25f, 0.15f, 1.0f));
            if (ImGui::Button("[DIR]", thumbSize))
            {
                m_currentPath = entry.fullPath;
                scanDirectory(m_currentPath);
                ImGui::PopStyleColor();
                ImGui::EndGroup();
                ImGui::PopID();
                return;
            }
            ImGui::PopStyleColor();
        }
        else if (entry.thumbnailLoaded && entry.thumbnailId != 0)
        {
            // Texture thumbnail
            ImTextureID texId = static_cast<ImTextureID>(
                static_cast<uintptr_t>(entry.thumbnailId));
            ImGui::Image(texId, thumbSize, ImVec2(0, 1), ImVec2(1, 0));

            // Double-click handling could go here in the future
        }
        else
        {
            // Type-based label button
            const char* typeLabel = "FILE";
            ImVec4 btnColor(0.2f, 0.2f, 0.2f, 1.0f);
            switch (entry.type)
            {
                case AssetType::TEXTURE: typeLabel = "TEX";  btnColor = ImVec4(0.2f, 0.35f, 0.2f, 1.0f); break;
                case AssetType::MESH:    typeLabel = "MESH"; btnColor = ImVec4(0.2f, 0.2f, 0.35f, 1.0f); break;
                case AssetType::PREFAB:  typeLabel = "PRE";  btnColor = ImVec4(0.35f, 0.2f, 0.35f, 1.0f); break;
                case AssetType::SHADER:  typeLabel = "GLSL"; btnColor = ImVec4(0.35f, 0.35f, 0.2f, 1.0f); break;
                default: break;
            }
            ImGui::PushStyleColor(ImGuiCol_Button, btnColor);
            ImGui::Button(typeLabel, thumbSize);
            ImGui::PopStyleColor();
        }

        // Drag-drop source for non-directory assets
        if (!entry.isDirectory && ImGui::BeginDragDropSource(
                ImGuiDragDropFlags_SourceAllowNullID))
        {
            const char* path = entry.fullPath.c_str();
            ImGui::SetDragDropPayload("ASSET_PATH", path,
                                      entry.fullPath.size() + 1);
            ImGui::Text("%s", entry.name.c_str());
            ImGui::EndDragDropSource();
        }

        // Double-click opens in appropriate viewer
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && !entry.isDirectory)
        {
            m_pendingOpenPath = entry.fullPath;
            m_pendingOpenType = entry.type;
        }

        // Tooltip with full filename
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", entry.name.c_str());
        }

        // Filename label (truncated to thumbnail width)
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + m_thumbnailSize);
        ImGui::TextWrapped("%s", entry.name.c_str());
        ImGui::PopTextWrapPos();

        ImGui::EndGroup();

        ImGui::PopID();

        // Grid wrapping
        col++;
        if (col < columns)
        {
            ImGui::SameLine();
        }
        else
        {
            col = 0;
        }
    }
}

} // namespace Vestige

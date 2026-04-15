// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file texture_viewer_panel.cpp
/// @brief Texture viewer panel implementation.
#include "editor/panels/texture_viewer_panel.h"
#include "core/logger.h"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace Vestige
{

// ============================================================================
// Constants
// ============================================================================

static constexpr float MIN_ZOOM = 0.1f;
static constexpr float MAX_ZOOM = 32.0f;
static constexpr int   MAX_FBO_SIZE = 2048;

// ============================================================================
// PBR suffix detection
// ============================================================================

/// Known PBR suffixes mapped to their role.
enum class PbrRole { ALBEDO, NORMAL, ROUGHNESS, METALLIC, AO, HEIGHT };

static const std::vector<std::pair<std::string, PbrRole>> PBR_SUFFIXES = {
    {"_albedo",     PbrRole::ALBEDO},
    {"_basecolor",  PbrRole::ALBEDO},
    {"_diffuse",    PbrRole::ALBEDO},
    {"_color",      PbrRole::ALBEDO},
    {"_normal",     PbrRole::NORMAL},
    {"_nor",        PbrRole::NORMAL},
    {"_roughness",  PbrRole::ROUGHNESS},
    {"_rough",      PbrRole::ROUGHNESS},
    {"_metallic",   PbrRole::METALLIC},
    {"_metal",      PbrRole::METALLIC},
    {"_metalness",  PbrRole::METALLIC},
    {"_ao",         PbrRole::AO},
    {"_occlusion",  PbrRole::AO},
    {"_height",     PbrRole::HEIGHT},
    {"_displacement", PbrRole::HEIGHT},
    {"_disp",       PbrRole::HEIGHT},
};

static const std::unordered_set<std::string> TEXTURE_EXTENSIONS = {
    ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".exr"
};

/// @brief Extracts the base name by removing known PBR suffixes.
/// Returns the suffix role if found, or empty string + no role.
static std::string extractBaseName(const std::string& stem, PbrRole& outRole)
{
    std::string lower = stem;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (const auto& [suffix, role] : PBR_SUFFIXES)
    {
        if (lower.size() > suffix.size() &&
            lower.substr(lower.size() - suffix.size()) == suffix)
        {
            outRole = role;
            return stem.substr(0, stem.size() - suffix.size());
        }
    }
    return {};
}

// ============================================================================
// Initialization
// ============================================================================

bool TextureViewerPanel::initialize(const std::string& assetPath)
{
    if (m_initialized)
    {
        return true;
    }

    m_assetPath = assetPath;

    std::string vertPath = assetPath + "/shaders/channel_view.vert.glsl";
    std::string fragPath = assetPath + "/shaders/channel_view.frag.glsl";
    if (!m_channelShader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("[TextureViewer] Failed to load channel_view shaders");
        return false;
    }

    // Create empty VAO for fullscreen triangle (no vertex attributes needed)
    glGenVertexArrays(1, &m_dummyVao);

    m_initialized = true;
    return true;
}

void TextureViewerPanel::cleanup()
{
    m_channelFbo.reset();
    m_texture.reset();
    if (m_dummyVao)
    {
        glDeleteVertexArrays(1, &m_dummyVao);
        m_dummyVao = 0;
    }
    m_initialized = false;
}

// ============================================================================
// Open texture
// ============================================================================

void TextureViewerPanel::openTexture(std::shared_ptr<Texture> texture,
                                      const std::string& path)
{
    m_texture = texture;
    m_texturePath = path;
    m_open = true;
    m_dirty = true;

    // Reset view state
    m_zoom = 1.0f;
    m_panOffset = glm::vec2(0.0f);
    m_channelMode = ChannelMode::RGB;
    m_mipLevel = -1;
    m_tileCount = 1;

    // Compute max mip level
    if (m_texture && m_texture->isLoaded())
    {
        int maxDim = std::max(m_texture->getWidth(), m_texture->getHeight());
        m_maxMipLevel = static_cast<int>(std::floor(std::log2(static_cast<float>(maxDim))));
    }
    else
    {
        m_maxMipLevel = 0;
    }

    // Detect PBR groups in same directory
    if (!path.empty())
    {
        std::filesystem::path fsPath(path);
        if (fsPath.has_parent_path())
        {
            m_pbrGroups = detectPbrGroups(fsPath.parent_path().string());
        }
    }
}

// ============================================================================
// Setters with clamping
// ============================================================================

void TextureViewerPanel::setChannelMode(ChannelMode mode)
{
    if (m_channelMode != mode)
    {
        m_channelMode = mode;
        m_dirty = true;
    }
}

void TextureViewerPanel::setZoom(float zoom)
{
    m_zoom = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
}

void TextureViewerPanel::setTileCount(int count)
{
    m_tileCount = std::clamp(count, 1, 3);
    m_dirty = true;
}

void TextureViewerPanel::setMipLevel(int level)
{
    m_mipLevel = std::clamp(level, -1, m_maxMipLevel);
    m_dirty = true;
}

// ============================================================================
// Draw
// ============================================================================

void TextureViewerPanel::draw()
{
    if (!m_open)
    {
        return;
    }

    ImGui::Begin("Texture Viewer", &m_open);

    if (!m_texture || !m_texture->isLoaded())
    {
        ImGui::TextDisabled("No texture loaded. Double-click a texture in the Asset Browser.");
        ImGui::End();
        return;
    }

    drawToolbar();
    ImGui::Separator();
    drawImageView();
    ImGui::Separator();
    drawMetadataSection();

    if (!m_pbrGroups.empty())
    {
        ImGui::Separator();
        drawPbrGroupSection();
    }

    ImGui::End();
}

// ============================================================================
// Toolbar
// ============================================================================

void TextureViewerPanel::drawToolbar()
{
    // Channel mode buttons
    ImGui::Text("Channel:");
    ImGui::SameLine();

    auto channelButton = [this](const char* label, ChannelMode mode)
    {
        bool selected = (m_channelMode == mode);
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        }
        if (ImGui::SmallButton(label))
        {
            setChannelMode(mode);
        }
        if (selected)
        {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
    };

    channelButton("RGB", ChannelMode::RGB);
    channelButton("R", ChannelMode::R);
    channelButton("G", ChannelMode::G);
    channelButton("B", ChannelMode::B);
    channelButton("A", ChannelMode::A);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Tiling
    ImGui::Text("Tile:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    int tile = m_tileCount;
    if (ImGui::SliderInt("##tile", &tile, 1, 3))
    {
        setTileCount(tile);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Mip level
    ImGui::Text("Mip:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    int mip = m_mipLevel;
    if (ImGui::SliderInt("##mip", &mip, -1, m_maxMipLevel,
                          mip < 0 ? "Auto" : "%d"))
    {
        setMipLevel(mip);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Zoom display
    ImGui::Text("Zoom: %.1fx", static_cast<double>(m_zoom));
    ImGui::SameLine();
    if (ImGui::SmallButton("1:1"))
    {
        m_zoom = 1.0f;
        m_panOffset = glm::vec2(0.0f);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit"))
    {
        // Zoom will be computed in drawImageView
        m_zoom = -1.0f;  // Signal for fit-to-window
        m_panOffset = glm::vec2(0.0f);
    }
}

// ============================================================================
// Image view with zoom/pan
// ============================================================================

void TextureViewerPanel::drawImageView()
{
    ImVec2 availSize = ImGui::GetContentRegionAvail();
    // Reserve space for metadata below
    float imageAreaHeight = availSize.y - 120.0f;
    if (imageAreaHeight < 100.0f)
    {
        imageAreaHeight = availSize.y * 0.6f;
    }

    int texW = m_texture->getWidth();
    int texH = m_texture->getHeight();

    // Handle "Fit" zoom
    if (m_zoom < 0.0f)
    {
        float fitX = availSize.x / static_cast<float>(texW);
        float fitY = imageAreaHeight / static_cast<float>(texH);
        m_zoom = std::min(fitX, fitY);
        m_zoom = std::clamp(m_zoom, MIN_ZOOM, MAX_ZOOM);
    }

    // Render the channel view to FBO if dirty
    if (m_dirty && m_initialized)
    {
        renderChannelView();
        m_dirty = false;
    }

    // Create interaction area
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize(availSize.x, imageAreaHeight);

    ImGui::InvisibleButton("##texview", canvasSize,
                            ImGuiButtonFlags_MouseButtonLeft |
                            ImGuiButtonFlags_MouseButtonMiddle);
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();

    // Handle zoom via scroll
    if (isHovered)
    {
        float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0.0f)
        {
            float oldZoom = m_zoom;
            m_zoom *= (scroll > 0.0f) ? 1.15f : (1.0f / 1.15f);
            m_zoom = std::clamp(m_zoom, MIN_ZOOM, MAX_ZOOM);

            // Zoom toward mouse position
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            float mx = mousePos.x - canvasPos.x - canvasSize.x * 0.5f;
            float my = mousePos.y - canvasPos.y - canvasSize.y * 0.5f;
            float zoomRatio = m_zoom / oldZoom;
            m_panOffset.x = mx - (mx - m_panOffset.x) * zoomRatio;
            m_panOffset.y = my - (my - m_panOffset.y) * zoomRatio;
        }
    }

    // Handle pan via middle-mouse drag
    if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_panOffset.x += delta.x;
        m_panOffset.y += delta.y;
    }

    // Compute display rect
    float displayW = static_cast<float>(texW) * m_zoom;
    float displayH = static_cast<float>(texH) * m_zoom;
    float cx = canvasPos.x + canvasSize.x * 0.5f + m_panOffset.x;
    float cy = canvasPos.y + canvasSize.y * 0.5f + m_panOffset.y;

    ImVec2 imgMin(cx - displayW * 0.5f, cy - displayH * 0.5f);
    ImVec2 imgMax(cx + displayW * 0.5f, cy + displayH * 0.5f);

    // Draw checkerboard background (for alpha visualization)
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasPos,
                            ImVec2(canvasPos.x + canvasSize.x,
                                   canvasPos.y + canvasSize.y), true);
    drawList->AddRectFilled(canvasPos,
                             ImVec2(canvasPos.x + canvasSize.x,
                                    canvasPos.y + canvasSize.y),
                             IM_COL32(40, 40, 40, 255));

    // Draw the texture
    GLuint displayTexId = 0;
    if (m_channelFbo)
    {
        displayTexId = m_channelFbo->getColorAttachmentId();
    }
    else if (m_channelMode == ChannelMode::RGB && m_tileCount == 1 && m_mipLevel < 0)
    {
        // Direct display when no processing needed
        displayTexId = m_texture->getId();
    }

    if (displayTexId != 0)
    {
        drawList->AddImage(
            static_cast<ImTextureID>(static_cast<uintptr_t>(displayTexId)),
            imgMin, imgMax,
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
    }

    drawList->PopClipRect();
}

// ============================================================================
// Channel view FBO rendering
// ============================================================================

void TextureViewerPanel::renderChannelView()
{
    if (!m_texture || !m_texture->isLoaded() || !m_initialized)
    {
        return;
    }

    int texW = m_texture->getWidth();
    int texH = m_texture->getHeight();

    // Determine FBO size (capped for performance)
    int fboW = std::min(texW, MAX_FBO_SIZE);
    int fboH = std::min(texH, MAX_FBO_SIZE);

    // Create or resize FBO
    if (!m_channelFbo || m_channelFbo->getWidth() != fboW ||
        m_channelFbo->getHeight() != fboH)
    {
        FramebufferConfig config;
        config.width = fboW;
        config.height = fboH;
        config.samples = 1;
        config.hasColorAttachment = true;
        config.hasDepthAttachment = false;
        config.isFloatingPoint = false;
        m_channelFbo = std::make_unique<Framebuffer>(config);
    }

    // Save GL state
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // Bind FBO
    m_channelFbo->bind();
    glViewport(0, 0, fboW, fboH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    // Set texture wrap mode for tiling
    GLuint texId = m_texture->getId();
    glBindTexture(GL_TEXTURE_2D, texId);
    GLint prevWrapS, prevWrapT;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &prevWrapS);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &prevWrapT);
    if (m_tileCount > 1)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    // Bind shader and set uniforms
    m_channelShader.use();
    m_channelShader.setInt("u_channelMode", static_cast<int>(m_channelMode));
    m_channelShader.setFloat("u_mipLevel", static_cast<float>(m_mipLevel));
    m_channelShader.setInt("u_tileCount", m_tileCount);
    m_channelShader.setFloat("u_exposure", 0.0f);
    m_channelShader.setBool("u_isHdr", false);

    // Check if this is an HDR texture by file extension
    std::string ext;
    if (!m_texturePath.empty())
    {
        ext = std::filesystem::path(m_texturePath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    if (ext == ".hdr" || ext == ".exr")
    {
        m_channelShader.setBool("u_isHdr", true);
    }

    m_texture->bind(0);
    m_channelShader.setInt("u_texture", 0);

    // Draw fullscreen triangle
    glBindVertexArray(m_dummyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Restore texture wrap mode
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, prevWrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, prevWrapT);

    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1],
               prevViewport[2], prevViewport[3]);
}

// ============================================================================
// Metadata
// ============================================================================

void TextureViewerPanel::drawMetadataSection()
{
    if (ImGui::CollapsingHeader("Metadata", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int w = m_texture->getWidth();
        int h = m_texture->getHeight();

        ImGui::Text("Resolution: %d x %d", w, h);

        // Query internal format
        GLuint texId = m_texture->getId();
        glBindTexture(GL_TEXTURE_2D, texId);
        GLint internalFormat = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                                  &internalFormat);

        // Determine bytes per pixel from format
        int bpp = 4;  // default RGBA8
        const char* formatName = "RGBA8";
        switch (internalFormat)
        {
            case GL_SRGB8_ALPHA8: bpp = 4; formatName = "sRGB8_A8"; break;
            case GL_RGBA8:        bpp = 4; formatName = "RGBA8"; break;
            case GL_RGB8:         bpp = 3; formatName = "RGB8"; break;
            case GL_SRGB8:        bpp = 3; formatName = "sRGB8"; break;
            case GL_RGBA16F:      bpp = 8; formatName = "RGBA16F"; break;
            case GL_RGB16F:       bpp = 6; formatName = "RGB16F"; break;
            case GL_RGBA32F:      bpp = 16; formatName = "RGBA32F"; break;
            case GL_RGB32F:       bpp = 12; formatName = "RGB32F"; break;
            case GL_R8:           bpp = 1; formatName = "R8"; break;
            case GL_RG8:          bpp = 2; formatName = "RG8"; break;
            default:              bpp = 4; formatName = "Unknown"; break;
        }

        ImGui::Text("Format: %s", formatName);

        // Estimated VRAM (with mipmaps: sum of geometric series ~1.33x)
        float baseSize = static_cast<float>(w * h * bpp);
        float totalSize = baseSize * 1.33f;  // mipmap overhead
        if (totalSize > 1024.0f * 1024.0f)
        {
            ImGui::Text("Est. VRAM: %.1f MB", static_cast<double>(totalSize / (1024.0f * 1024.0f)));
        }
        else
        {
            ImGui::Text("Est. VRAM: %.0f KB", static_cast<double>(totalSize / 1024.0f));
        }

        ImGui::Text("Mip Levels: %d", m_maxMipLevel + 1);

        // File path
        if (!m_texturePath.empty())
        {
            std::string filename = std::filesystem::path(m_texturePath).filename().string();
            ImGui::Text("File: %s", filename.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", m_texturePath.c_str());
            }
        }
    }
}

// ============================================================================
// PBR texture grouping
// ============================================================================

std::vector<PbrTextureGroup> TextureViewerPanel::detectPbrGroups(
    const std::string& directoryPath)
{
    std::unordered_map<std::string, PbrTextureGroup> groups;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directoryPath, ec))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (TEXTURE_EXTENSIONS.find(ext) == TEXTURE_EXTENSIONS.end())
        {
            continue;
        }

        std::string stem = entry.path().stem().string();
        PbrRole role;
        std::string baseName = extractBaseName(stem, role);

        if (baseName.empty())
        {
            continue;
        }

        auto& group = groups[baseName];
        group.baseName = baseName;
        std::string fullPath = entry.path().string();

        switch (role)
        {
            case PbrRole::ALBEDO:    group.albedoPath = fullPath; break;
            case PbrRole::NORMAL:    group.normalPath = fullPath; break;
            case PbrRole::ROUGHNESS: group.roughnessPath = fullPath; break;
            case PbrRole::METALLIC:  group.metallicPath = fullPath; break;
            case PbrRole::AO:        group.aoPath = fullPath; break;
            case PbrRole::HEIGHT:    group.heightPath = fullPath; break;
        }
    }

    // Only keep groups with at least 2 textures
    std::vector<PbrTextureGroup> result;
    for (auto& [name, group] : groups)
    {
        int count = 0;
        if (!group.albedoPath.empty()) ++count;
        if (!group.normalPath.empty()) ++count;
        if (!group.roughnessPath.empty()) ++count;
        if (!group.metallicPath.empty()) ++count;
        if (!group.aoPath.empty()) ++count;
        if (!group.heightPath.empty()) ++count;

        if (count >= 2)
        {
            result.push_back(std::move(group));
        }
    }

    // Sort by base name
    std::sort(result.begin(), result.end(),
              [](const PbrTextureGroup& a, const PbrTextureGroup& b)
              { return a.baseName < b.baseName; });

    return result;
}

void TextureViewerPanel::drawPbrGroupSection()
{
    if (ImGui::CollapsingHeader("PBR Texture Sets"))
    {
        for (const auto& group : m_pbrGroups)
        {
            if (ImGui::TreeNode(group.baseName.c_str()))
            {
                auto showEntry = [](const char* label, const std::string& path)
                {
                    if (!path.empty())
                    {
                        std::string filename =
                            std::filesystem::path(path).filename().string();
                        ImGui::BulletText("%s: %s", label, filename.c_str());
                    }
                };

                showEntry("Albedo",    group.albedoPath);
                showEntry("Normal",    group.normalPath);
                showEntry("Roughness", group.roughnessPath);
                showEntry("Metallic",  group.metallicPath);
                showEntry("AO",        group.aoPath);
                showEntry("Height",    group.heightPath);

                ImGui::TreePop();
            }
        }
    }
}

} // namespace Vestige

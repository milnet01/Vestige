// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file texture_viewer_panel.h
/// @brief Texture viewer panel — full-resolution display with zoom/pan, channel isolation,
///        mipmap visualization, tiling preview, and PBR texture set grouping.
#pragma once

#include "editor/panels/i_panel.h"
#include "renderer/framebuffer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Channel display mode for the texture viewer.
enum class ChannelMode
{
    RGB,
    R,
    G,
    B,
    A
};

/// @brief A group of PBR textures sharing a common base name.
struct PbrTextureGroup
{
    std::string baseName;
    std::string albedoPath;
    std::string normalPath;
    std::string roughnessPath;
    std::string metallicPath;
    std::string aoPath;
    std::string heightPath;
};

/// @brief Panel for viewing textures with channel isolation, zoom/pan, mip levels,
///        tiling preview, metadata, and PBR texture set detection.
class TextureViewerPanel : public IPanel
{
public:
    const char* displayName() const override { return "Texture Viewer"; }

    /// @brief Initializes shaders and FBO for channel rendering.
    /// @param assetPath Base path for shader loading.
    /// @return True if initialization succeeded.
    bool initialize(const std::string& assetPath);

    /// @brief Draws the texture viewer panel.
    void draw();

    /// @brief Opens a texture for viewing.
    /// @param texture Shared pointer to the loaded texture.
    /// @param path File path of the texture (for metadata display).
    void openTexture(std::shared_ptr<Texture> texture, const std::string& path);

    /// @brief Releases GPU resources.
    void cleanup();

    bool isOpen() const override { return m_open; }
    void setOpen(bool open) override { m_open = open; }

    // --- Accessors for testing ---

    ChannelMode getChannelMode() const { return m_channelMode; }
    void setChannelMode(ChannelMode mode);

    float getZoom() const { return m_zoom; }
    void setZoom(float zoom);

    int getTileCount() const { return m_tileCount; }
    void setTileCount(int count);

    int getMipLevel() const { return m_mipLevel; }
    void setMipLevel(int level);

    int getMaxMipLevel() const { return m_maxMipLevel; }

    const glm::vec2& getPanOffset() const { return m_panOffset; }

    /// @brief Detects PBR texture groups in a directory.
    /// @param directoryPath Path to scan for related textures.
    /// @return Detected PBR texture groups.
    static std::vector<PbrTextureGroup> detectPbrGroups(const std::string& directoryPath);

private:
    void drawToolbar();
    void drawImageView();
    void drawMetadataSection();
    void drawPbrGroupSection();
    void renderChannelView();

    bool m_open = false;
    bool m_initialized = false;
    bool m_dirty = true;

    // Texture being viewed
    std::shared_ptr<Texture> m_texture;
    std::string m_texturePath;

    // View state
    float m_zoom = 1.0f;
    glm::vec2 m_panOffset = glm::vec2(0.0f);

    // Channel mode
    ChannelMode m_channelMode = ChannelMode::RGB;

    // Mip visualization
    int m_mipLevel = -1;  ///< -1 = auto, 0+ = explicit level
    int m_maxMipLevel = 0;

    // Tiling preview
    int m_tileCount = 1;  ///< 1 = no tiling, 2 = 2x2, 3 = 3x3

    // Channel isolation FBO + shader
    std::unique_ptr<Framebuffer> m_channelFbo;
    Shader m_channelShader;
    GLuint m_dummyVao = 0;  ///< Empty VAO for fullscreen triangle

    // PBR grouping
    std::vector<PbrTextureGroup> m_pbrGroups;

    std::string m_assetPath;
};

} // namespace Vestige

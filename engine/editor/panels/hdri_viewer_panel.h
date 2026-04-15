// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file hdri_viewer_panel.h
/// @brief HDRI viewer panel — equirectangular preview, skybox mini-viewport,
///        exposure control, and one-click scene environment setting.
#pragma once

#include "renderer/framebuffer.h"
#include "renderer/shader.h"
#include "renderer/skybox.h"
#include "renderer/texture.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Vestige
{

class Renderer;
class ResourceManager;

/// @brief Panel for previewing HDRI environment maps with exposure control,
///        skybox mini-viewport, and one-click scene environment setting.
class HdriViewerPanel
{
public:
    /// @brief Initializes shaders and FBO for the HDRI viewer.
    /// @param assetPath Base path for shader loading.
    /// @return True if initialization succeeded.
    bool initialize(const std::string& assetPath);

    /// @brief Draws the HDRI viewer panel.
    /// @param renderer Renderer for "Set as Environment" functionality. May be nullptr.
    void draw(Renderer* renderer);

    /// @brief Opens an HDRI file for viewing.
    /// @param path File path to the equirectangular HDRI image.
    /// @param resources ResourceManager for texture loading.
    void openHdri(const std::string& path, ResourceManager* resources);

    /// @brief Releases GPU resources.
    void cleanup();

    bool isOpen() const { return m_open; }
    void setOpen(bool open) { m_open = open; }

    // --- Accessors for testing ---

    float getExposure() const { return m_exposure; }
    void setExposure(float ev);

    float getPreviewYaw() const { return m_previewYaw; }
    float getPreviewPitch() const { return m_previewPitch; }
    void setPreviewAngles(float yaw, float pitch);

private:
    void drawEquirectPreview();
    void drawSkyboxPreview();
    void drawControls(Renderer* renderer);
    void renderSkyboxFbo();

    bool m_open = false;
    bool m_initialized = false;
    bool m_skyboxDirty = true;

    // HDRI source
    std::string m_hdriPath;
    std::shared_ptr<Texture> m_equirectTexture;

    // Skybox preview
    std::unique_ptr<Skybox> m_previewSkybox;
    std::unique_ptr<Framebuffer> m_skyboxFbo;
    std::unique_ptr<Framebuffer> m_equirectFbo;  ///< FBO for equirect preview with exposure
    Shader m_skyboxShader;
    int m_previewSize = 256;

    // Equirectangular preview shader
    Shader m_equirectShader;
    GLuint m_dummyVao = 0;

    // View state
    float m_exposure = 0.0f;       ///< EV stops
    float m_previewYaw = 0.0f;     ///< Degrees
    float m_previewPitch = 0.0f;   ///< Degrees

    // Equirect view pan
    float m_equirectYaw = 0.0f;    ///< Radians
    float m_equirectPitch = 0.0f;  ///< Radians

    std::string m_assetPath;
};

} // namespace Vestige

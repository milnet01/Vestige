// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file water_renderer.h
/// @brief Water surface renderer with reflections, refractions, and wave displacement.
#pragma once

#include "renderer/shader.h"
#include "renderer/camera.h"
#include "scene/water_surface.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <vector>

namespace Vestige
{

/// @brief Collected water surface data for rendering.
struct WaterRenderItem
{
    const WaterSurfaceComponent* component;
    glm::mat4 worldMatrix;
};

/// @brief Renders water surfaces with planar reflections, refractions, and wave animation.
class WaterRenderer
{
public:
    WaterRenderer() = default;
    ~WaterRenderer();

    // Non-copyable
    WaterRenderer(const WaterRenderer&) = delete;
    WaterRenderer& operator=(const WaterRenderer&) = delete;

    /// @brief Initializes shaders and default textures.
    /// @param assetPath Path to the assets directory.
    /// @return True if initialization succeeded.
    bool init(const std::string& assetPath);

    /// @brief Releases all GPU resources.
    void shutdown();

    /// @brief Renders all water surface meshes.
    /// @param waterItems Water surfaces to render.
    /// @param camera The main camera.
    /// @param aspectRatio Viewport aspect ratio.
    /// @param time Current elapsed time for wave animation.
    /// @param lightDir Directional light direction (world space).
    /// @param lightColor Directional light color.
    /// @param environmentCubemap Skybox cubemap texture ID (0 = none).
    /// @param reflectionTex Reflection texture from WaterFbo (0 = use cubemap fallback).
    /// @param refractionTex Refraction texture from WaterFbo (0 = disabled).
    /// @param refractionDepthTex Refraction depth texture for Beer's law (0 = disabled).
    /// @param cameraNear Near plane distance for depth linearization.
    void render(const std::vector<WaterRenderItem>& waterItems,
                const Camera& camera,
                float aspectRatio,
                float time,
                const glm::vec3& lightDir,
                const glm::vec3& lightColor,
                GLuint environmentCubemap,
                GLuint reflectionTex = 0,
                GLuint refractionTex = 0,
                GLuint refractionDepthTex = 0,
                float cameraNear = 0.1f);

    /// @brief Set water surface quality tier (0=Full, 1=Approximate, 2=Simple).
    void setWaterQualityTier(int tier) { m_waterQualityTier = tier; }

private:
    void generateDefaultNormalMap();
    void generateDefaultDudvMap();
    void generateDefaultFoamTexture();

    Shader m_waterShader;

    // Default procedural textures
    GLuint m_defaultNormalMap = 0;
    GLuint m_defaultDudvMap = 0;
    GLuint m_defaultFoamTexture = 0;
    GLuint m_fallbackCubemap = 0;  ///< 1x1 black cubemap for Mesa AMD sampler safety

    int m_waterQualityTier = 1;  // 0=Full, 1=Approximate, 2=Simple (default Approximate)
    bool m_initialized = false;
};

} // namespace Vestige

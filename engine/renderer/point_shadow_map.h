// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file point_shadow_map.h
/// @brief Omnidirectional shadow mapping using a depth cubemap for point lights.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "renderer/light.h"

#include <vector>

namespace Vestige
{

/// @brief Configuration for point light shadow mapping.
struct PointShadowConfig
{
    int resolution = 1024;         // Cubemap face size (square)
    float nearPlane = 0.1f;
    float farPlane = 25.0f;
};

/// @brief Maximum number of point lights that can cast shadows simultaneously.
constexpr int MAX_POINT_SHADOW_LIGHTS = 2;

/// @brief Selects up to maxCasters shadow-casting lights from a list, returning
///        their indices. Pure function — no GL state, testable headlessly.
inline std::vector<int> selectShadowCastersFromLights(
    const std::vector<PointLight>& lights, int maxCasters)
{
    std::vector<int> result;
    for (int i = 0; i < static_cast<int>(lights.size()); ++i)
    {
        if (lights[static_cast<size_t>(i)].castsShadow
            && static_cast<int>(result.size()) < maxCasters)
        {
            result.push_back(i);
        }
    }
    return result;
}

/// @brief Manages an omnidirectional shadow map (depth cubemap FBO) for a point light.
class PointShadowMap
{
public:
    /// @brief Creates a point shadow map with the given configuration.
    /// @param config Shadow map settings.
    explicit PointShadowMap(const PointShadowConfig& config = PointShadowConfig());
    ~PointShadowMap();

    // Non-copyable
    PointShadowMap(const PointShadowMap&) = delete;
    PointShadowMap& operator=(const PointShadowMap&) = delete;

    PointShadowMap(PointShadowMap&& other) noexcept;
    PointShadowMap& operator=(PointShadowMap&& other) noexcept;

    /// @brief Computes the 6 light-space matrices for a given light position.
    /// @param lightPos World-space position of the point light.
    void update(const glm::vec3& lightPos);

    /// @brief Begins rendering to a specific cubemap face.
    /// @param face Cubemap face index (0-5: +X, -X, +Y, -Y, +Z, -Z).
    void beginFace(int face);

    /// @brief Ends rendering to the current face.
    void endFace();

    /// @brief Binds the shadow cubemap texture for sampling.
    /// @param textureUnit The texture unit to bind to.
    void bindShadowTexture(int textureUnit) const;

    /// @brief Gets the light-space matrix for a specific face.
    /// @param face Cubemap face index (0-5).
    const glm::mat4& getLightSpaceMatrix(int face) const;

    /// @brief Gets the configuration.
    const PointShadowConfig& getConfig() const;

    /// @brief Raw RSM flux cubemap handle (RGBA16F). Each texel holds
    ///        `albedo · light-radiance · max(0,N·L) · attenuation` for the
    ///        surface closest to the light — the reflective-shadow-map term the
    ///        world-space GI inject pass scatters into the probe grid
    ///        (Phase 13 G1). RGB = flux, A = 1 where geometry wrote flux.
    GLuint fluxCubemap() const { return m_fluxCubemap; }

private:
    PointShadowConfig m_config;
    GLuint m_fbo = 0;
    GLuint m_depthCubemap = 0;
    GLuint m_fluxCubemap = 0;   ///< RSM flux colour attachment (RGBA16F, Phase 13 G1)
    glm::mat4 m_lightSpaceMatrices[6];
    glm::mat4 m_shadowProjection;
};

} // namespace Vestige

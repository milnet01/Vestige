// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file shadow_map.h
/// @brief Directional light shadow mapping with depth FBO and PCF filtering.
#pragma once

#include "renderer/framebuffer.h"
#include "renderer/light.h"

#include <glm/glm.hpp>

#include <memory>

namespace Vestige
{

/// @brief Configuration for directional shadow mapping.
struct ShadowConfig
{
    int resolution = 2048;         // Shadow map texture size (square)
    float orthoSize = 15.0f;       // Half-size of the orthographic projection
    float nearPlane = 1.0f;
    float farPlane = 50.0f;
};

/// @brief Manages a directional light shadow map (depth-only FBO + light-space matrix).
class ShadowMap
{
public:
    /// @brief Creates a shadow map with the given configuration.
    /// @param config Shadow map settings.
    explicit ShadowMap(const ShadowConfig& config = ShadowConfig());

    // Non-copyable
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    /// @brief Calculates the light-space matrix for a directional light.
    /// @param light The directional light source.
    /// @param sceneCenter The center of the scene (shadow map focuses around this point).
    void update(const DirectionalLight& light, const glm::vec3& sceneCenter);

    /// @brief Binds the shadow FBO and prepares for the shadow depth pass.
    void beginShadowPass();

    /// @brief Ends the shadow pass and restores normal rendering state.
    void endShadowPass();

    /// @brief Binds the shadow depth texture for sampling in the lighting pass.
    /// @param textureUnit The texture unit to bind to.
    void bindShadowTexture(int textureUnit);

    /// @brief Gets the light-space matrix (view * projection from the light's perspective).
    const glm::mat4& getLightSpaceMatrix() const;

    /// @brief Gets the shadow configuration for runtime adjustment.
    ShadowConfig& getConfig();

private:
    ShadowConfig m_config;
    std::unique_ptr<Framebuffer> m_depthFbo;
    glm::mat4 m_lightSpaceMatrix = glm::mat4(1.0f);
};

} // namespace Vestige

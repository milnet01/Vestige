// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file light_probe.h
/// @brief Local IBL light probe — captures and stores environment lighting at a specific position.
#pragma once

#include "renderer/shader.h"
#include "utils/aabb.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>

namespace Vestige
{

/// @brief A light probe that captures local environment lighting into irradiance and prefilter cubemaps.
///
/// Used to provide correct IBL for indoor/enclosed areas where the global sky IBL is inappropriate.
/// The probe captures the scene from its position into a cubemap, then convolves it into
/// irradiance (diffuse ambient) and prefilter (specular reflections) maps.
class LightProbe
{
public:
    LightProbe();
    ~LightProbe();

    // Non-copyable (owns GPU resources)
    LightProbe(const LightProbe&) = delete;
    LightProbe& operator=(const LightProbe&) = delete;

    LightProbe(LightProbe&& other) noexcept;
    LightProbe& operator=(LightProbe&& other) noexcept;

    // --- Configuration ---

    void setPosition(const glm::vec3& position);
    glm::vec3 getPosition() const;

    /// @brief Sets the axis-aligned bounding box defining this probe's influence region.
    void setInfluenceAABB(const AABB& bounds);
    AABB getInfluenceAABB() const;

    /// @brief Distance from the AABB edge over which the probe blends with global IBL.
    void setFadeDistance(float distance);
    float getFadeDistance() const;

    // --- Capture & Convolution ---

    /// @brief Allocates GPU resources for capture. Call once before capture().
    /// @param irradianceShader Shared irradiance convolution shader.
    /// @param prefilterShader Shared prefilter shader.
    bool initialize(const Shader& irradianceShader, const Shader& prefilterShader);

    /// @brief Captures the environment from this probe's position into the cubemap.
    /// @param capturedCubemap A pre-rendered cubemap of the scene from this probe's position.
    ///        The caller is responsible for rendering the scene into this cubemap.
    void generateFromCubemap(GLuint capturedCubemap);

    // --- Binding ---

    void bindIrradiance(int unit) const;
    void bindPrefilter(int unit) const;

    // --- Query ---

    /// @brief Tests if a world-space point is inside this probe's influence volume.
    bool containsPoint(const glm::vec3& point) const;

    /// @brief Returns the blend weight for a point (1.0 = center, fading to 0.0 at boundary edge).
    float getBlendWeight(const glm::vec3& point) const;

    bool isReady() const;

    // --- Constants ---
    static constexpr int CAPTURE_RESOLUTION = 128;
    static constexpr int IRRADIANCE_RESOLUTION = 32;
    static constexpr int PREFILTER_RESOLUTION = 128;
    static constexpr int MAX_MIP_LEVELS = 5;

private:
    void generateIrradiance(GLuint envCubemap);
    void generatePrefilter(GLuint envCubemap);
    void renderCube() const;

    glm::vec3 m_position = glm::vec3(0.0f);
    AABB m_influenceAABB;
    float m_fadeDistance = 2.0f;

    // Shared shaders (non-owning — managed by LightProbeManager)
    const Shader* m_irradianceShader = nullptr;
    const Shader* m_prefilterShader = nullptr;

    // Cube geometry for rendering to cubemap faces
    GLuint m_cubeVao = 0;
    GLuint m_cubeVbo = 0;

    // Capture FBO (reused for irradiance + prefilter face renders)
    GLuint m_captureFbo = 0;
    GLuint m_captureRbo = 0;

    // Output IBL textures
    GLuint m_irradianceMap = 0;
    GLuint m_prefilterMap = 0;

    bool m_ready = false;
};

} // namespace Vestige

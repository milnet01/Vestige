// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_renderer.h
/// @brief Instanced billboard renderer for particle systems.
#pragma once

#include "renderer/shader.h"
#include "renderer/texture.h"
#include "renderer/camera.h"
#include "scene/particle_emitter.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige { class GPUParticleEmitter; }  // Forward declaration

namespace Vestige
{

/// @brief Renders particles as camera-facing billboards using instanced drawing.
///
/// Manages a static quad VAO and dynamic per-instance data buffers (position,
/// color, size, normalizedAge). Supports texture binding, soft particles via
/// depth comparison, and per-emitter blend modes.
class ParticleRenderer
{
public:
    ParticleRenderer();
    ~ParticleRenderer();

    // Non-copyable
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    /// @brief Loads the particle shader.
    /// @param assetPath Base path to the assets directory.
    /// @return True if shader loaded successfully.
    bool init(const std::string& assetPath);

    /// @brief Cleans up GPU resources.
    void shutdown();

    /// @brief Renders all CPU particle emitters in the scene.
    /// @param emitters List of (emitter, worldMatrix) pairs to render.
    /// @param camera Current camera for billboard orientation and sorting.
    /// @param viewProjection The view-projection matrix.
    /// @param depthTexture Scene depth texture for soft particles (0 = disabled).
    /// @param screenWidth Viewport width for soft particle UV calculation.
    /// @param screenHeight Viewport height for soft particle UV calculation.
    /// @param cameraNear Near plane distance for depth linearization.
    void render(const std::vector<std::pair<const ParticleEmitterComponent*, glm::mat4>>& emitters,
                const Camera& camera,
                const glm::mat4& viewProjection,
                GLuint depthTexture = 0,
                int screenWidth = 0,
                int screenHeight = 0,
                float cameraNear = 0.1f);

    /// @brief Renders GPU particle emitters using compute-driven indirect draw.
    /// @param emitters List of GPU emitter pointers to render.
    /// @param camera Current camera for billboard orientation and sorting.
    /// @param viewProjection The view-projection matrix.
    /// @param depthTexture Scene depth texture for soft particles (0 = disabled).
    /// @param screenWidth Viewport width for soft particle UV calculation.
    /// @param screenHeight Viewport height for soft particle UV calculation.
    /// @param cameraNear Near plane distance for depth linearization.
    void renderGPU(const std::vector<const GPUParticleEmitter*>& emitters,
                   const Camera& camera,
                   const glm::mat4& viewProjection,
                   GLuint depthTexture = 0,
                   int screenWidth = 0,
                   int screenHeight = 0,
                   float cameraNear = 0.1f);

private:
    void createQuadVao();
    void ensureInstanceBufferCapacity(int count);
    GLuint getOrLoadTexture(const std::string& path);

    Shader m_shader;
    Shader m_gpuShader;    ///< GPU particle billboard shader (reads from SSBO)
    std::string m_assetPath;

    // GPU particle rendering
    GLuint m_gpuVao = 0;   ///< Empty VAO for GPU particle rendering (vertices generated in shader)

    // Static quad geometry (2 triangles)
    GLuint m_quadVao = 0;
    GLuint m_quadVbo = 0;

    // Per-instance data buffers
    GLuint m_instancePositionVbo = 0;
    GLuint m_instanceColorVbo = 0;
    GLuint m_instanceSizeVbo = 0;
    GLuint m_instanceAgeVbo = 0;    // normalizedAge for soft particles
    int m_instanceBufferCapacity = 0;

    // Texture cache (path → GL texture ID)
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_textureCache;
};

} // namespace Vestige

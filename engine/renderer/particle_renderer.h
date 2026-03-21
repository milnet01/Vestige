/// @file particle_renderer.h
/// @brief Instanced billboard renderer for particle systems.
#pragma once

#include "renderer/shader.h"
#include "renderer/camera.h"
#include "scene/particle_emitter.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

class ResourceManager;

/// @brief Renders particles as camera-facing billboards using instanced drawing.
///
/// Manages a static quad VAO and dynamic per-instance data buffers (position,
/// color, size). Each frame, live particle data is uploaded and drawn in a
/// single instanced draw call per emitter.
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

    /// @brief Renders all particle emitters in the scene.
    /// @param emitters List of (emitter, worldMatrix) pairs to render.
    /// @param camera Current camera for billboard orientation and sorting.
    /// @param viewProjection The view-projection matrix.
    void render(const std::vector<std::pair<const ParticleEmitterComponent*, glm::mat4>>& emitters,
                const Camera& camera,
                const glm::mat4& viewProjection);

private:
    void createQuadVao();
    void ensureInstanceBufferCapacity(int count);

    Shader m_shader;

    // Static quad geometry (2 triangles)
    GLuint m_quadVao = 0;
    GLuint m_quadVbo = 0;

    // Per-instance data buffers
    GLuint m_instancePositionVbo = 0;
    GLuint m_instanceColorVbo = 0;
    GLuint m_instanceSizeVbo = 0;
    int m_instanceBufferCapacity = 0;

    // CPU-side staging buffers (avoid per-frame allocation)
    std::vector<glm::vec3> m_stagingPositions;
    std::vector<glm::vec4> m_stagingColors;
    std::vector<float> m_stagingSizes;
};

} // namespace Vestige

/// @file foliage_renderer.h
/// @brief Instanced renderer for grass and ground cover using star-mesh geometry.
#pragma once

#include "renderer/shader.h"
#include "renderer/camera.h"
#include "environment/foliage_chunk.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <vector>

namespace Vestige
{

/// @brief Renders foliage instances using instanced 3-quad star meshes with wind animation.
class FoliageRenderer
{
public:
    FoliageRenderer() = default;
    ~FoliageRenderer();

    // Non-copyable
    FoliageRenderer(const FoliageRenderer&) = delete;
    FoliageRenderer& operator=(const FoliageRenderer&) = delete;

    /// @brief Initializes the foliage renderer (shaders, star mesh geometry).
    /// @param assetPath Path to the assets directory.
    /// @return True if initialization succeeded.
    bool init(const std::string& assetPath);

    /// @brief Releases all GPU resources.
    void shutdown();

    /// @brief Renders visible foliage instances from frustum-culled chunks.
    /// @param chunks Visible chunks from FoliageManager::getVisibleChunks().
    /// @param camera Current camera.
    /// @param viewProjection Combined VP matrix.
    /// @param time Elapsed time for wind animation.
    /// @param maxDistance Distance beyond which foliage is not rendered.
    void render(const std::vector<const FoliageChunk*>& chunks,
                const Camera& camera,
                const glm::mat4& viewProjection,
                float time,
                float maxDistance = 100.0f);

    /// @brief Wind direction (normalized XZ).
    glm::vec3 windDirection{1.0f, 0.0f, 0.3f};

    /// @brief Wind amplitude (sway distance in meters).
    float windAmplitude = 0.08f;

    /// @brief Wind frequency (oscillation speed).
    float windFrequency = 2.0f;

private:
    /// @brief Creates the 3-quad intersecting star mesh (6 triangles, 12 vertices).
    void createStarMesh();

    /// @brief Generates a simple procedural grass blade texture.
    void generateDefaultTexture();

    /// @brief Uploads foliage instances to the GPU instance buffer.
    void uploadInstances(const std::vector<FoliageInstance>& instances);

    Shader m_shader;
    GLuint m_starVao = 0;
    GLuint m_starVbo = 0;
    GLuint m_instanceVbo = 0;
    int m_instanceCapacity = 0;

    // Default procedural grass texture
    GLuint m_defaultTexture = 0;

    // CPU staging buffer (reused each frame to avoid allocation)
    std::vector<FoliageInstance> m_visibleInstances;

    bool m_initialized = false;
};

} // namespace Vestige

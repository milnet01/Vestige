/// @file water_renderer.h
/// @brief Water surface renderer with environment map reflections and wave displacement.
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

/// @brief Renders water surfaces with environment map reflections and wave animation.
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
    void render(const std::vector<WaterRenderItem>& waterItems,
                const Camera& camera,
                float aspectRatio,
                float time,
                const glm::vec3& lightDir,
                const glm::vec3& lightColor,
                GLuint environmentCubemap);

private:
    void generateDefaultNormalMap();
    void generateDefaultDudvMap();

    Shader m_waterShader;

    // Default procedural textures
    GLuint m_defaultNormalMap = 0;
    GLuint m_defaultDudvMap = 0;

    bool m_initialized = false;
};

} // namespace Vestige

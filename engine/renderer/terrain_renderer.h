/// @file terrain_renderer.h
/// @brief CDLOD terrain renderer with instanced grid mesh and splatmap texturing.
#pragma once

#include "environment/terrain.h"
#include "renderer/shader.h"
#include "renderer/camera.h"
#include "scene/scene.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Vestige
{

class CascadedShadowMap;

/// @brief Configuration for a single terrain texture layer.
struct TerrainTextureLayer
{
    std::string name = "Untitled";
    float tiling = 10.0f;         ///< UV tiling scale for this layer.
};

/// @brief Renders terrain using CDLOD with a single reusable grid mesh.
class TerrainRenderer
{
public:
    TerrainRenderer() = default;
    ~TerrainRenderer();

    // Non-copyable
    TerrainRenderer(const TerrainRenderer&) = delete;
    TerrainRenderer& operator=(const TerrainRenderer&) = delete;

    /// @brief Initializes shaders and creates the grid mesh.
    /// @param assetPath Path to the assets directory.
    /// @return True if initialization succeeded.
    bool init(const std::string& assetPath);

    /// @brief Releases all GPU resources.
    void shutdown();

    /// @brief Renders the terrain with lighting and shadows.
    /// @param terrain The terrain data source.
    /// @param camera Current camera.
    /// @param aspectRatio Viewport aspect ratio.
    /// @param sceneData Scene data for lighting information.
    /// @param csm Cascaded shadow map for shadow receiving (nullptr = no shadows).
    void render(const Terrain& terrain,
                const Camera& camera,
                float aspectRatio,
                const SceneRenderData& sceneData,
                CascadedShadowMap* csm = nullptr,
                const glm::vec4& clipPlane = glm::vec4(0.0f));

    /// @brief Renders the terrain into a shadow map.
    /// @param terrain The terrain data source.
    /// @param camera Current camera (for node selection).
    /// @param aspectRatio Viewport aspect ratio.
    /// @param lightSpaceMatrix Light's view-projection matrix.
    void renderShadow(const Terrain& terrain,
                      const Camera& camera,
                      float aspectRatio,
                      const glm::mat4& lightSpaceMatrix);

    /// @brief Gets the number of draw calls from the last frame.
    int getLastDrawCallCount() const { return m_lastDrawCallCount; }

    /// @brief Gets the number of triangles from the last frame.
    int getLastTriangleCount() const { return m_lastTriangleCount; }

private:
    /// @brief Creates the shared grid mesh (gridResolution x gridResolution vertices).
    void createGridMesh(int resolution);

    /// @brief Generates a simple green default texture.
    void generateDefaultTextures();

    Shader m_terrainShader;
    Shader m_shadowShader;

    // Shared grid mesh (reused for every CDLOD node)
    GLuint m_gridVao = 0;
    GLuint m_gridVbo = 0;
    GLuint m_gridEbo = 0;
    int m_gridIndexCount = 0;
    int m_gridResolution = 0;

    // Default textures (flat green terrain)
    GLuint m_defaultAlbedo = 0;
    GLuint m_defaultNormal = 0;

    // Per-frame staging
    std::vector<TerrainDrawNode> m_drawNodes;

    // Stats
    int m_lastDrawCallCount = 0;
    int m_lastTriangleCount = 0;

    bool m_initialized = false;
};

} // namespace Vestige

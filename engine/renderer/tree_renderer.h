/// @file tree_renderer.h
/// @brief Renders trees with mesh LOD and billboard crossfade at distance.
#pragma once

#include "renderer/shader.h"
#include "renderer/camera.h"
#include "environment/foliage_chunk.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <vector>

namespace Vestige
{

/// @brief Renders trees from foliage chunks with LOD selection.
///
/// LOD0: Full mesh (instanced) for nearby trees.
/// LOD1: Billboard quad facing the camera for distant trees.
/// Crossfade via alpha blending in the transition zone.
class TreeRenderer
{
public:
    TreeRenderer() = default;
    ~TreeRenderer();

    // Non-copyable
    TreeRenderer(const TreeRenderer&) = delete;
    TreeRenderer& operator=(const TreeRenderer&) = delete;

    /// @brief Initializes shaders and placeholder geometry.
    /// @param assetPath Path to the assets directory.
    /// @return True if initialization succeeded.
    bool init(const std::string& assetPath);

    /// @brief Releases all GPU resources.
    void shutdown();

    /// @brief Renders trees from visible chunks with LOD selection.
    /// @param chunks Visible chunks from FoliageManager.
    /// @param camera Current camera.
    /// @param viewProjection VP matrix.
    /// @param time Elapsed time for wind sway.
    void render(const std::vector<const FoliageChunk*>& chunks,
                const Camera& camera,
                const glm::mat4& viewProjection,
                float time,
                const glm::vec4& clipPlane = glm::vec4(0.0f));

    /// @brief Distance at which LOD0 -> LOD1 transition begins.
    float lodDistance = 50.0f;

    /// @brief Range over which LOD transition crossfades.
    float fadeRange = 10.0f;

    /// @brief Maximum render distance for trees.
    float maxDistance = 200.0f;

private:
    /// @brief Creates a simple placeholder tree mesh (trunk + crown).
    void createPlaceholderTree();

    /// @brief Creates a billboard quad for LOD1.
    void createBillboardQuad();

    /// @brief Generates a procedural tree billboard texture.
    void generateBillboardTexture();

    // LOD0: Simple tree mesh
    Shader m_meshShader;
    GLuint m_treeVao = 0;
    GLuint m_treeVbo = 0;
    GLuint m_treeEbo = 0;
    int m_treeIndexCount = 0;
    GLuint m_treeInstanceVbo = 0;
    int m_treeInstanceCapacity = 0;

    // LOD1: Billboard
    Shader m_billboardShader;
    GLuint m_billboardVao = 0;
    GLuint m_billboardVbo = 0;
    GLuint m_billboardInstanceVbo = 0;
    int m_billboardInstanceCapacity = 0;
    GLuint m_billboardTexture = 0;

    // Per-frame staging buffers
    struct TreeDrawInstance
    {
        glm::vec3 position;
        float rotation;
        float scale;
        float alpha;  ///< Crossfade alpha (1.0 = fully visible).
    };

    std::vector<TreeDrawInstance> m_lod0Instances;
    std::vector<TreeDrawInstance> m_lod1Instances;

    bool m_initialized = false;
};

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file foliage_renderer.h
/// @brief Instanced renderer for grass and ground cover using star-mesh geometry.
#pragma once

#include "renderer/shader.h"
#include "renderer/camera.h"
#include "renderer/light.h"
#include "environment/foliage_chunk.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <unordered_map>
#include <vector>

namespace Vestige
{

class CascadedShadowMap;

/// @brief Pure decision for `setTypeTexture` (3D_E-0038 B1, design §4.1/§7):
///        given whether the path was empty, whether `stbi_load` returned null,
///        and the GL handle `uploadRGBA8` produced (0 on GL failure), decide
///        whether to ADOPT the freshly-loaded texture. Otherwise the caller keeps
///        the existing (procedural) texture. Factored out so the fallback +
///        upload-failure ordering contract is unit-testable without a GL context.
///        `uploadedHandle` is a `GLuint` value, taken as `unsigned` here so the
///        predicate carries no GL dependency.
inline bool foliageAdoptLoadedTexture(bool pathEmpty, bool decodeNull,
                                      unsigned uploadedHandle)
{
    return !pathEmpty && !decodeNull && uploadedHandle != 0u;
}

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
    /// @param csm Cascaded shadow map for shadow receiving (nullptr = no shadows).
    /// @param dirLight Directional light for diffuse/translucency (nullptr = unlit).
    void render(const std::vector<const FoliageChunk*>& chunks,
                const Camera& camera,
                const glm::mat4& viewProjection,
                float time,
                float maxDistance = 100.0f,
                CascadedShadowMap* csm = nullptr,
                const DirectionalLight* dirLight = nullptr,
                const glm::vec4& clipPlane = glm::vec4(0.0f));

    /// @brief Renders foliage into a shadow map with alpha testing.
    /// Only renders instances within shadowMaxDistance for performance.
    /// @param chunks Visible chunks from FoliageManager::getVisibleChunks().
    /// @param camera Current camera (for distance culling).
    /// @param lightSpaceMatrix Light's view-projection matrix.
    /// @param time Elapsed time for wind animation (must match main pass).
    /// @param lightRadiance Directional light radiance (colour × intensity) for
    ///        the RSM flux term (Phase 13 G1). Pass black to write zero flux.
    /// @param lightDir Direction the light travels (`DirectionalLight::direction`).
    void renderShadow(const std::vector<const FoliageChunk*>& chunks,
                      const Camera& camera,
                      const glm::mat4& lightSpaceMatrix,
                      float time,
                      const glm::vec3& lightRadiance,
                      const glm::vec3& lightDir);

    /// @brief Loads a real alpha texture for a foliage type from disk, replacing
    ///        the procedural default. On an empty path (= "no override") or a
    ///        decode/GL-upload failure, the current texture is kept (fallback) and
    ///        — for a non-empty path that fails — a warning is logged. Must be
    ///        called after `init()` so a procedural default already exists to keep.
    ///        (3D_E-0038 B1, design §4.1.)
    void setTypeTexture(uint32_t typeId, const std::string& path);

    /// @brief Maximum distance for grass shadow casting (cascade 0 range).
    float shadowMaxDistance = 30.0f;

    /// @brief Wind direction (normalized XZ).
    glm::vec3 windDirection{1.0f, 0.0f, 0.3f};

    /// @brief Wind amplitude (sway distance in meters).
    float windAmplitude = 0.08f;

    /// @brief Wind frequency (oscillation speed).
    float windFrequency = 2.0f;

private:
    /// @brief Creates the 3-quad intersecting star mesh (6 triangles, 12 vertices).
    void createStarMesh();

    /// @brief Generates procedural textures for each foliage type.
    void generateTypeTextures();

    /// @brief Generates a single procedural texture for a foliage type.
    GLuint generateProceduralTexture(uint32_t typeId);

    /// @brief Uploads a straight-RGBA8 pixel buffer as an immutable-storage 2D
    ///        texture with a full mip chain (LINEAR_MIPMAP_LINEAR / CLAMP_TO_EDGE).
    ///        Shared by the procedural generator and `setTypeTexture` so the GL
    ///        setup lives in one place (Rule 3). Returns 0 on GL failure.
    GLuint uploadRGBA8(const uint8_t* pixels, int w, int h);

    /// @brief Uploads foliage instances to the GPU instance buffer.
    void uploadInstances(const std::vector<FoliageInstance>& instances);

    /// @brief Pre-allocated instance-buffer capacity. Phase 10.9 Pe3:
    ///        sized to cover a typical chunk's visible foliage so the
    ///        first `uploadInstances()` call doesn't have to delete +
    ///        recreate the buffer mid-frame. 16 384 × 32 B = 512 KB,
    ///        well within VRAM budget for a single foliage renderer.
    static constexpr int INITIAL_INSTANCE_CAPACITY = 16384;

    Shader m_shader;
    Shader m_shadowShader;
    GLuint m_starVao = 0;
    GLuint m_starVbo = 0;
    GLuint m_instanceVbo = 0;
    int m_instanceCapacity = 0;

    // Per-type procedural textures (typeId → GL texture handle)
    std::unordered_map<uint32_t, GLuint> m_typeTextures;

    // Per-type staging buffers (reused each frame to avoid allocation)
    std::unordered_map<uint32_t, std::vector<FoliageInstance>> m_visibleByType;

    bool m_initialized = false;
};

} // namespace Vestige

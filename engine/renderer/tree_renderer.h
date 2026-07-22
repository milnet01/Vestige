// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tree_renderer.h
/// @brief Renders real artist trees with 3-bucket mesh LOD + billboard crossfade.
#pragma once

#include "renderer/shader.h"
#include "renderer/camera.h"
#include "renderer/light.h"
#include "environment/foliage_chunk.h"
#include "environment/foliage_instance.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <memory>
#include <string>
#include <vector>

namespace Vestige
{

class CascadedShadowMap;
class ResourceManager;
class Model;
class Mesh;
class Material;

/// @brief Renders trees placed in FoliageManager chunks (design §4, 3D_E-0033).
///
/// Each species carries three artist LOD tiers loaded from glTF via
/// ResourceManager (shared, path-cached): a near mesh (LOD0), a mid mesh (LOD2),
/// and a flat billboard card (far). Per frame, every tree is bucketed by camera
/// distance into one of the three tiers with a complementary-alpha crossfade in
/// the two transition bands. Mesh tiers are drawn per-material (bark opaque +
/// leaf alpha-cutout), instanced, with wind sway + directional light + CSM
/// shadow receive. The billboard tier is a camera-facing (yaw) quad textured
/// with the artist's card, oriented from a per-pass camera-right uniform (so the
/// same code faces the pond-reflection camera too, §4.3 D8).
class TreeRenderer
{
public:
    TreeRenderer() = default;
    ~TreeRenderer();

    // Non-copyable
    TreeRenderer(const TreeRenderer&) = delete;
    TreeRenderer& operator=(const TreeRenderer&) = delete;

    /// @brief Loads shaders + the shared billboard quad. Species are loaded
    ///        separately via loadSpecies() once the meadow knows its tree set.
    /// @param assetPath Path to the assets directory.
    bool init(const std::string& assetPath);

    /// @brief Builds the runtime species table from authored configs (§4.1).
    ///        For each config: `meshPath` = LOD0 glTF; the mid tier is the same
    ///        path with the `_lod0` stem swapped for `_lod2` (nullptr if absent);
    ///        `billboardTexturePath` = the billboard glTF (nullptr if absent).
    ///        All three load through @a resources (path-cached, shared). Species
    ///        are appended in order — `TreeInstance::speciesIndex` indexes here.
    ///        Safe to call once after init(); replaces any prior table.
    void loadSpecies(const std::vector<TreeSpeciesConfig>& configs,
                     ResourceManager& resources);

    /// @brief Number of loaded species (for placement index validation).
    size_t getSpeciesCount() const { return m_species.size(); }

    /// @brief Releases all GPU resources.
    void shutdown();

    /// @brief Renders trees from visible chunks with LOD selection.
    /// @param chunks Visible chunks from FoliageManager::getVisibleChunks().
    /// @param camera Current (or reflection) camera.
    /// @param viewProjection Combined VP matrix for the pass.
    /// @param time Elapsed time for wind sway.
    /// @param csm Cascaded shadow map for shadow receiving (nullptr = no shadows).
    /// @param dirLight Directional light for diffuse/ambient (nullptr = unlit).
    /// @param clipPlane Water clip plane for the reflection pass (0 = disabled).
    void render(const std::vector<const FoliageChunk*>& chunks,
                const Camera& camera,
                const glm::mat4& viewProjection,
                float time,
                CascadedShadowMap* csm = nullptr,
                const DirectionalLight* dirLight = nullptr,
                const glm::vec4& clipPlane = glm::vec4(0.0f));

    /// @brief Distance at which LOD0 → mid transition begins (m).
    float lodDistance = 40.0f;

    /// @brief Distance at which mid → billboard transition begins (m).
    float billboardDistance = 90.0f;

    /// @brief Range over which each LOD transition crossfades (m).
    float fadeRange = 12.0f;

    /// @brief Maximum render distance for trees (m).
    float maxDistance = 200.0f;

    /// @brief Wind direction (world XZ), synced from EnvironmentForces per frame.
    glm::vec3 windDirection{1.0f, 0.0f, 0.3f};

    /// @brief Wind sway amplitude (metres at the canopy), synced per frame.
    float windAmplitude = 0.15f;

    /// @brief Wind oscillation frequency.
    float windFrequency = 1.2f;

private:
    /// @brief One primitive to draw for a tier: its mesh VAO, the node's baked
    ///        world matrix (§4.4), and its material (alpha mode / albedo).
    struct PrimDraw
    {
        const Mesh* mesh = nullptr;
        glm::mat4 nodeMatrix{1.0f};
        const Material* material = nullptr;
    };

    /// @brief Runtime holder for one species (design §4.1). Owns nothing GPU;
    ///        the Models (and their VAOs/textures) are shared via ResourceManager.
    struct TreeSpecies
    {
        std::shared_ptr<Model> lod0;       ///< Near mesh.
        std::shared_ptr<Model> lodMid;     ///< Mid mesh (null → collapse to billboard).
        std::shared_ptr<Model> billboard;  ///< Far card model (null → mid holds to maxDistance).
        std::vector<PrimDraw> lod0Prims;   ///< Flattened draw list for LOD0.
        std::vector<PrimDraw> midPrims;    ///< Flattened draw list for the mid tier.
        GLuint billboardTexture = 0;       ///< Artist billboard card diffuse (GL id, not owned).
        float billboardHalfWidth = 1.0f;   ///< Card half-width in metres (from model bounds).
        float billboardHeight = 2.0f;      ///< Card height in metres (from model bounds).
    };

    /// @brief A tree resolved to one tier with its crossfade alpha.
    struct Bucketed
    {
        glm::mat4 model;   ///< translate · rotateY · scale.
        float alpha;       ///< Crossfade weight (1 = fully this tier).
    };

    /// @brief Walks a model's node graph, composing world matrices, and emits a
    ///        flat {mesh, nodeMatrix, material} draw list (design §4.4).
    void buildDrawList(const Model& model, std::vector<PrimDraw>& out) const;

    /// @brief (Re)binds the shared instance + alpha VBOs to every species-tier
    ///        mesh VAO (binding 1 = model mat4 @6–9, binding 3 = alpha @12).
    void bindInstanceBuffers();

    /// @brief Grows the shared instance/alpha VBOs to hold @a count instances,
    ///        rebinding all VAOs if a reallocation happened.
    void ensureInstanceCapacity(int count);

    /// @brief Uploads a bucket's model matrices + alphas and draws every prim in
    ///        @a prims instanced, honouring per-material alpha mode.
    void drawMeshTier(const std::vector<PrimDraw>& prims,
                      const std::vector<Bucketed>& bucket);

    /// @brief Creates the shared unit billboard quad (offset ±1 x, 0..1 y).
    void createBillboardQuad();

    /// @brief Sets the shared CSM + directional-light uniforms on @a shader
    ///        (Mesa fallback binds a sampler2DArray to unit 3 when unshadowed).
    void setLightingUniforms(Shader& shader, CascadedShadowMap* csm,
                             const DirectionalLight* dirLight);

    // Shaders
    Shader m_meshShader;
    Shader m_billboardShader;

    // Species table (indexed by TreeInstance::speciesIndex)
    std::vector<TreeSpecies> m_species;

    // Shared instanced-mesh buffers (model mat4 @ binding 1, alpha @ binding 3)
    GLuint m_meshInstanceVbo = 0;   ///< Per-instance model mat4.
    GLuint m_meshAlphaVbo = 0;      ///< Per-instance crossfade alpha (float).
    int m_meshInstanceCapacity = 0;

    // Shared billboard quad + its per-instance stream (pos/rot/scale/alpha)
    GLuint m_billboardVao = 0;
    GLuint m_billboardVbo = 0;
    GLuint m_billboardInstanceVbo = 0;
    int m_billboardInstanceCapacity = 0;

    /// @brief Per-instance billboard record (matches the billboard VAO layout).
    struct BillboardInstance
    {
        glm::vec3 position;  ///< Trunk-base world position.
        float scale;         ///< Per-tree uniform scale.
        float alpha;         ///< Crossfade weight.
        float halfWidth;     ///< Species card half-width (metres).
        float height;        ///< Species card height (metres).
    };

    // Per-frame staging (reused to avoid per-frame allocation). Mesh + billboard
    // draws are grouped per species (distinct VAOs / card texture per species),
    // so each outer vector is indexed by species like m_species.
    std::vector<std::vector<Bucketed>> m_lod0BySpecies;
    std::vector<std::vector<Bucketed>> m_midBySpecies;
    std::vector<std::vector<BillboardInstance>> m_bbBySpecies;
    std::vector<glm::mat4> m_matScratch;    ///< Flat mat4 upload staging.
    std::vector<float> m_alphaScratch;      ///< Flat alpha upload staging.

    bool m_initialized = false;
};

} // namespace Vestige

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
/// and a far impostor (LOD3). Per frame, every tree is bucketed by camera
/// distance into one of the three tiers with a complementary-alpha crossfade in
/// the two transition bands. ALL THREE tiers are real meshes drawn through the
/// same instanced mesh path, per-material (bark opaque + leaf alpha-cutout),
/// with wind sway + directional light + CSM shadow receive. The far tier is the
/// artist's LOD3 billboard glTF — a small 3-D cloud of leaf cards with the atlas
/// UVs baked in (NOT a flat card), forced to alpha-cutout since its material is
/// BLEND. Rendering the impostor mesh directly is view-independent, so the same
/// code serves the pond-reflection pass too.
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

    /// @brief Renders LOD0 + mid tree meshes into one cascade's depth so trees
    ///        cast ground shadows (design §4.4/T4). Mirrors the foliage caster:
    ///        called once per cascade from the renderer's shadow pass with that
    ///        cascade's light-space matrix. The far impostor tier does NOT cast
    ///        (D4) — a distant flat card's cast is ill-defined and negligible.
    ///        Casters are bucketed into the SAME tier the viewer sees (LOD0 near,
    ///        mid beyond) so the shadow tracks the drawn mesh. Wind/time uniforms
    ///        come from this renderer's public wind fields + @a time, matching
    ///        the main pass so shadows don't detach from the swaying canopy.
    /// @param chunks Vegetation chunks (FoliageManager::getAllChunks — trees only).
    /// @param camera Camera whose position drives the distance/tier cull.
    /// @param lightSpaceMatrix This cascade's light-space view-projection.
    /// @param time Elapsed time for wind sway (synced to the main pass clock).
    /// @param lightRadiance Directional radiance (colour × intensity) for RSM flux.
    /// @param lightDir Directional light travel direction (RSM flux cosine).
    void renderShadow(const std::vector<const FoliageChunk*>& chunks,
                      const Camera& camera,
                      const glm::mat4& lightSpaceMatrix,
                      float time,
                      const glm::vec3& lightRadiance,
                      const glm::vec3& lightDir);

    /// @brief Distance at which LOD0 → mid transition begins (m).
    float lodDistance = 45.0f;

    /// @brief Distance at which mid → far-impostor transition begins (m). Held
    ///        well out: the solid mid mesh looks better than the sparse impostor,
    ///        so only swap to the impostor where fog already hides the detail.
    float billboardDistance = 180.0f;

    /// @brief Range over which each LOD transition crossfades (m).
    float fadeRange = 15.0f;

    /// @brief Maximum render distance for trees (m). Held far out so the impostor
    ///        tier sits on the horizon (under fog) and never visibly pops in.
    float maxDistance = 350.0f;

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
        std::shared_ptr<Model> lodMid;     ///< Mid mesh (null → collapse to far tier).
        std::shared_ptr<Model> billboard;  ///< Far impostor (LOD3) mesh (null → mid holds to maxDistance).
        std::vector<PrimDraw> lod0Prims;   ///< Flattened draw list for LOD0.
        std::vector<PrimDraw> midPrims;    ///< Flattened draw list for the mid tier.
        std::vector<PrimDraw> billboardPrims;  ///< Flattened draw list for the far impostor.
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
    ///        @a prims instanced, honouring per-material alpha mode. The far
    ///        impostor tier passes @a forceAlphaTest to cut out its BLEND atlas
    ///        as an order-independent cutout (@a forceCutoff threshold).
    void drawMeshTier(const std::vector<PrimDraw>& prims,
                      const std::vector<Bucketed>& bucket,
                      bool forceAlphaTest = false,
                      float forceCutoff = 0.5f);

    /// @brief Depth-only sibling of drawMeshTier for the shadow pass: uploads a
    ///        bucket's model matrices (no crossfade alpha) and draws every prim
    ///        instanced through m_shadowShader, honouring per-material
    ///        alpha-cutout + double-sided cull for correct leaf shadows.
    void drawShadowTier(const std::vector<PrimDraw>& prims,
                        const std::vector<Bucketed>& bucket);

    /// @brief Sets the shared CSM + directional-light uniforms on @a shader
    ///        (Mesa fallback binds a sampler2DArray to unit 3 when unshadowed).
    void setLightingUniforms(Shader& shader, CascadedShadowMap* csm,
                             const DirectionalLight* dirLight);

    // Shaders (all three LOD tiers use the one instanced mesh shader)
    Shader m_meshShader;
    // Depth+flux caster for the shadow pass (LOD0 + mid only, D4).
    Shader m_shadowShader;

    // Species table (indexed by TreeInstance::speciesIndex)
    std::vector<TreeSpecies> m_species;

    // Shared instanced-mesh buffers (model mat4 @ binding 1, alpha @ binding 3)
    GLuint m_meshInstanceVbo = 0;   ///< Per-instance model mat4.
    GLuint m_meshAlphaVbo = 0;      ///< Per-instance crossfade alpha (float).
    int m_meshInstanceCapacity = 0;

    // Per-frame staging (reused to avoid per-frame allocation). All three tiers
    // are meshes drawn per species (distinct VAOs), so each outer vector is
    // indexed by species like m_species.
    std::vector<std::vector<Bucketed>> m_lod0BySpecies;
    std::vector<std::vector<Bucketed>> m_midBySpecies;
    std::vector<std::vector<Bucketed>> m_bbBySpecies;
    // Shadow-pass caster buckets (LOD0 + mid only; refilled per cascade).
    std::vector<std::vector<Bucketed>> m_shadowLod0BySpecies;
    std::vector<std::vector<Bucketed>> m_shadowMidBySpecies;
    std::vector<glm::mat4> m_matScratch;    ///< Flat mat4 upload staging.
    std::vector<float> m_alphaScratch;      ///< Flat alpha upload staging.

    bool m_initialized = false;
};

} // namespace Vestige

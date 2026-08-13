// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_renderer.h
/// @brief GPU procedural grass — a field of real 3-D Bézier-blade geometry generated in
///        the vertex shader from per-blade seeds in an SSBO. Replaces the billboard-grass
///        path in the meadow (flowers stay on FoliageRenderer).
///        Design: docs/phases/phase_10_meadow_gpu_grass_design.md.
///
/// Slice status: **G3** — LOD + per-chunk frustum cull. `render` culls each chunk's
/// clump-padded AABB against the view frustum, then picks a distance LOD from the chunk's
/// nearest point: a per-chunk segment tier (2N+1 strip verts) plus a per-blade blade-count
/// fade (`grass_lod.h` `grassKeptFraction`) evaluated identically on the CPU (instanceCount)
/// and in the VS (each blade tapers to zero over distance), so the field thins with no pop.
/// Built on G2's shared SSBO + per-chunk descriptors. Shading + wind + shadow-receive (G4)
/// and quality tiers + meadow wire-up (G5) build on this.
#pragma once

#include "environment/grass_blade.h"
#include "environment/grass_config.h"
#include "environment/grass_lod.h"
#include "renderer/light.h"
#include "renderer/shader.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <string>
#include <vector>

namespace Vestige
{

class Camera;
class Terrain;
class CascadedShadowMap;

/// @brief GPU-grass quality tier (3D_E-0039 G5), driven by the graphics `QualityPreset`
///        via `RendererQualitySink` — the same path `FoliageQuality` /
///        `TerrainGroundQuality` use. Free-standing (not nested) so `settings_apply.h`
///        can forward-declare it without including this header. The tier only dials
///        CPU-side LOD distance + blade-fraction aggressiveness (`GrassLodBands`), so its
///        integer values carry no wire contract and are free to change.
enum class GrassQuality
{
    Low,     ///< Short draw distance, aggressive thinning — reduced-motion / low-end path.
    Medium,  ///< Mid draw distance.
    High     ///< Full draw distance + density (the §5.3 `GrassLodBands` defaults).
};

/// @brief Renders the GPU grass field from one shared blade SSBO. Per-chunk descriptors
///        (base offset into the shared buffer + blade count + AABB) drive the per-chunk
///        draw now and the frustum cull / LOD in G3.
class GrassRenderer
{
public:
    GrassRenderer() = default;
    ~GrassRenderer();

    GrassRenderer(const GrassRenderer&) = delete;
    GrassRenderer& operator=(const GrassRenderer&) = delete;

    /// @brief Loads the grass shaders and creates the empty draw VAO.
    /// @param assetPath Path to the assets directory.
    /// @return True on success; false if the shaders fail to load.
    bool init(const std::string& assetPath);

    /// @brief Releases all GPU resources.
    void shutdown();

    /// @brief Scatter the grass field across the terrain (§5.2): per-chunk jittered grid,
    ///        seated on terrain height, gated on grass-splat weight + slope + the pond
    ///        exclusion disc, seeds shuffled per chunk for the G3 LOD prefix property. Fills
    ///        the one shared SSBO + per-chunk descriptors. Rebuild only when the terrain
    ///        changes.
    void buildField(const Terrain& terrain, const GrassConfig& config);

    /// @brief Draws the field: per-chunk frustum cull, then a distance LOD (segment tier +
    ///        faded blade fraction) from each visible chunk's nearest point (§5.3), shaded
    ///        with directional half-Lambert + backlit translucency + height AO + CSM shadow
    ///        RECEIVE, and wind-swayed in the vertex shader (§5.4). No-op until a field is
    ///        built.
    /// @param viewProjection Combined VP for the frustum planes + clip transform.
    /// @param view View matrix — supplies the view-space depth that picks a shadow cascade.
    /// @param cameraPos World camera position — the per-chunk/-blade LOD distance origin
    ///        and the view direction for translucency.
    /// @param time Elapsed seconds — animates the wind sway.
    /// @param dirLight Directional light for diffuse/translucency (nullptr = unlit).
    /// @param csm Cascaded shadow map for shadow receiving (nullptr = no shadows).
    void render(const glm::mat4& viewProjection,
                const glm::mat4& view,
                const glm::vec3& cameraPos,
                float time,
                const DirectionalLight* dirLight,
                CascadedShadowMap* csm);

    /// @brief Draws the field into one shadow cascade (3D_E-0042). Same blade geometry as
    ///        `render` — the shadow program links the very same `grass.vert.glsl`, driven
    ///        with @p lightSpaceMatrix in place of the view-projection — so a blade's shadow
    ///        cannot drift from its silhouette. Chunks are culled against the LIGHT frustum,
    ///        but the distance LOD still keys on the CAMERA, so each cast blade is the blade
    ///        the viewer sees. No-op until a field is built.
    ///
    ///        Cascade 0 only, capped at @p maxCastDistance: grass shadows read as contact
    ///        shadows and are invisible in the far cascades, and the whole field × 4 cascades
    ///        is not affordable (docs/research/grass_shadows_research.md §2.3/§7.4).
    /// @param camera Viewer — supplies the LOD origin and the view matrix.
    /// @param lightSpaceMatrix This cascade's light-space matrix (clip transform + cull).
    /// @param time Elapsed seconds — MUST match the visible pass so the swayed blade and its
    ///        shadow move together.
    /// @param lightRadiance Directional radiance (colour × intensity) for the RSM flux term.
    /// @param lightDir Directional light travel direction for the RSM flux term.
    /// @param maxCastDistance Camera-space cull radius; pass cascade 0's far split so the
    ///        cast stops exactly where receivers stop sampling cascade 0.
    void renderShadow(const Camera& camera,
                      const glm::mat4& lightSpaceMatrix,
                      float time,
                      const glm::vec3& lightRadiance,
                      const glm::vec3& lightDir,
                      float maxCastDistance);

    /// @brief Normalised world-XZ wind direction (from the shared EnvironmentForces).
    ///        Public and set per frame by the engine, like `FoliageRenderer::windDirection`
    ///        and `TreeRenderer::windDirection` — the shadow pass runs before the visible
    ///        pass, so the sway cannot be a `render` argument if both are to agree.
    glm::vec2 windDir{0.0f, 1.0f};

    /// @brief Wind speed (m/s); 0 leaves the field dead calm (meadow default).
    float windStrength = 0.0f;

    /// @brief Whether a blade field is currently populated.
    bool hasField() const { return m_bladeCount > 0; }

    /// @brief Total stored blade count across all chunks.
    GLsizei bladeCount() const { return m_bladeCount; }

    /// @brief Chunk count (visible in logs / for the G3 drawn-chunk instrument).
    std::size_t chunkCount() const { return m_chunks.size(); }

    /// @brief Chunks actually drawn last `render` (survived frustum + distance cull) — the
    ///        §10 G3 drawn-chunk instrument.
    std::size_t drawnChunkCount() const { return m_drawnChunks; }

    /// @brief Distance-LOD thresholds/levels (§5.3). Public so G5's quality tier can dial
    ///        draw distance + LOD aggressiveness without touching the renderer internals.
    GrassLodBands& lodBands() { return m_lodBands; }
    const GrassLodBands& lodBands() const { return m_lodBands; }

    /// @brief Applies a quality tier (§10.5): dials the LOD draw distance + blade-fraction
    ///        aggressiveness so weaker GPUs shorten the field. High = the §5.3 defaults;
    ///        Medium/Low pull the cull distance in and thin the mid/far bands harder. The
    ///        no-pop band width is held ≥ the chunk diagonal on every tier.
    void setQuality(GrassQuality quality);

    /// @brief Near-LOD segment count (N). An N-segment blade is a GL_TRIANGLE_STRIP of
    ///        `2N+1` verts (design §5.1). G2 uses this single tier; G3 adds mid/far.
    static constexpr int SEGMENTS = 7;

private:
    /// @brief One terrain chunk's slice of the shared blade buffer + its bounds.
    struct GrassChunk
    {
        glm::vec3 aabbMin{0.0f};   ///< Clump-max-padded world AABB (§5.2a) — drives G3 cull/LOD.
        glm::vec3 aabbMax{0.0f};
        std::uint32_t baseOffset = 0;  ///< First blade's index in the shared SSBO.
        std::uint32_t count = 0;       ///< Blades in this chunk.
    };

    void uploadBlades(const std::vector<GrassBlade>& blades);

    /// @brief Sets the blade-generation uniforms and draws every surviving chunk. The ONE
    ///        draw path both passes go through (3D_E-0042), so the visible blade and its
    ///        shadow are generated by identical uniforms on identical geometry.
    /// @param shader Program to drive — `m_shader` (visible) or `m_shadowShader` (caster).
    ///        Both link the same `grass.vert.glsl`; only the fragment stage differs.
    /// @param clipFromWorld Clip transform for `u_viewProjection`, AND the matrix whose
    ///        frustum culls the chunks — view-projection for the viewer, light-space for a
    ///        cascade.
    /// @param view View matrix (the VS's cascade-select depth; unread by the shadow FS).
    /// @param cameraPos LOD origin — always the real camera, in both passes.
    /// @param time Elapsed seconds for the wind sway.
    /// @param maxDistance Extra camera-distance cull on top of the LOD bands (shadow cast
    ///        radius); `FLT_MAX` for the unbounded visible pass.
    /// @return Chunks actually drawn.
    std::size_t drawField(Shader& shader,
                          const glm::mat4& clipFromWorld,
                          const glm::mat4& view,
                          const glm::vec3& cameraPos,
                          float time,
                          float maxDistance);

    Shader m_shader;
    Shader m_shadowShader;     ///< grass.vert.glsl + grass_shadow.frag.glsl (3D_E-0042).
    GLuint m_vao = 0;          ///< Empty VAO — an attribute-less draw needs a non-zero VAO
                               ///< bound in a core profile (design §5.1).
    GLuint m_bladeSSBO = 0;    ///< Shared seed buffer, bound at SSBO binding 0.
    GLsizei m_bladeCount = 0;
    std::vector<GrassChunk> m_chunks;
    GrassConfig m_config;      ///< Kept for the draw (clump cell size + strength uniforms).
    GrassLodBands m_lodBands;  ///< Distance-LOD thresholds/levels (§5.3).
    std::size_t m_drawnChunks = 0;  ///< Chunks drawn last frame (drawn-chunk instrument).
    bool m_initialized = false;
};

} // namespace Vestige

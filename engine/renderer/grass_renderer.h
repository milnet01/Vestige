// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_renderer.h
/// @brief GPU procedural grass — a field of real 3-D Bézier-blade geometry generated in
///        the vertex shader from per-blade seeds in an SSBO. Replaces the billboard-grass
///        path in the meadow (flowers stay on FoliageRenderer).
///        Design: docs/phases/phase_10_meadow_gpu_grass_design.md.
///
/// Slice status: **G2** — CPU per-chunk placement + PCG gating + clumping. `buildField`
/// scatters blade seeds across the meadow (terrain-seated, grass-splat/slope/pond gated),
/// stores them in ONE shared SSBO with per-chunk descriptors (base offset + count +
/// clump-padded AABB), and the VS conforms each blade toward its clump (§5.2a) so the field
/// reads as tussocks. LOD + per-chunk frustum cull (G3), shading + wind + shadow-receive
/// (G4), and quality tiers + meadow wire-up (G5) build on the descriptors laid down here.
#pragma once

#include "environment/grass_blade.h"
#include "environment/grass_config.h"
#include "renderer/shader.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <string>
#include <vector>

namespace Vestige
{

class Terrain;

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

    /// @brief Draws the current blade field, one instanced draw per chunk. No-op until a
    ///        field is built.
    void render(const glm::mat4& viewProjection);

    /// @brief Whether a blade field is currently populated.
    bool hasField() const { return m_bladeCount > 0; }

    /// @brief Total stored blade count across all chunks.
    GLsizei bladeCount() const { return m_bladeCount; }

    /// @brief Chunk count (visible in logs / for a G3 drawn-chunk instrument).
    std::size_t chunkCount() const { return m_chunks.size(); }

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

    Shader m_shader;
    GLuint m_vao = 0;          ///< Empty VAO — an attribute-less draw needs a non-zero VAO
                               ///< bound in a core profile (design §5.1).
    GLuint m_bladeSSBO = 0;    ///< Shared seed buffer, bound at SSBO binding 0.
    GLsizei m_bladeCount = 0;
    std::vector<GrassChunk> m_chunks;
    GrassConfig m_config;      ///< Kept for the draw (clump cell size + strength uniforms).
    bool m_initialized = false;
};

} // namespace Vestige

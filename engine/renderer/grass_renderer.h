// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file grass_renderer.h
/// @brief GPU procedural grass — a field of real 3-D Bézier-blade geometry generated in
///        the vertex shader from per-blade seeds in an SSBO. Replaces the billboard-grass
///        path in the meadow (flowers stay on FoliageRenderer).
///        Design: docs/phases/phase_10_meadow_gpu_grass_design.md.
///
/// Slice status: **G1** — skeleton. Loads `grass.{vert,frag}.glsl`, builds ONE hard-coded
/// test patch of instanced blades into the shared seed SSBO, binds an empty VAO, and draws
/// the field flat-lit — enough to prove the VS blade generator + SSBO + attribute-less
/// draw are GL-error-free. Real PCG placement (G2), LOD + frustum cull (G3), shading +
/// wind + shadow-receive (G4), and quality tiers + meadow wire-up (G5) build on this.
#pragma once

#include "environment/grass_blade.h"
#include "renderer/shader.h"

#include <glm/glm.hpp>
#include <glad/gl.h>

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Renders the GPU grass field. G1 owns a single shared blade SSBO + an empty VAO;
///        G2 adds per-chunk descriptors + base offsets into the same shared buffer.
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

    /// @brief G1 bring-up — (re)builds a hard-coded jittered patch of blades seated at
    ///        `center` (world space) and uploads it to the seed SSBO. G2 replaces this
    ///        with real terrain-gated placement (`buildField`).
    void seatTestPatchAt(const glm::vec3& center);

    /// @brief Draws the current blade field. No-op until a field is built.
    void render(const glm::mat4& viewProjection);

    /// @brief Whether a blade field is currently populated.
    bool hasField() const { return m_bladeCount > 0; }

    /// @brief Near-LOD segment count (N). An N-segment blade is a GL_TRIANGLE_STRIP of
    ///        `2N+1` verts (design §5.1). G1 uses this single tier; G3 adds mid/far.
    static constexpr int SEGMENTS = 7;

private:
    void uploadBlades(const std::vector<GrassBlade>& blades);

    Shader m_shader;
    GLuint m_vao = 0;          ///< Empty VAO — an attribute-less draw needs a non-zero VAO
                               ///< bound in a core profile (design §5.1).
    GLuint m_bladeSSBO = 0;    ///< Shared seed buffer, bound at SSBO binding 0.
    GLsizei m_bladeCount = 0;
    bool m_initialized = false;
};

} // namespace Vestige

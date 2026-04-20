// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_renderer.h
/// @brief SpriteRenderer — instance-rate batched 2D sprite pass (Phase 9F-1).
///
/// Separate from the UI's `SpriteBatchRenderer`:
/// - UI batcher uses 4 vertices per quad (CPU merges mixed primitives).
/// - Game sprites pack one affine + one UV rect + one tint per *instance*
///   and issue a single `glDrawArraysInstanced` per (atlas, blend) group.
///
/// The corner quad is a static 4-vertex VBO; per-sprite data lives in a
/// second VBO with `glVertexAttribDivisor = 1`. Vertex-per-sprite cost is
/// 80 bytes (two mat3 rows + UV rect + RGBA + depth).
///
/// Pipeline state:
/// - Depth test on, LEQUAL. Depth write on for the opaque pass, off for
///   the transparent pass.
/// - Blend off for opaque, `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` for transparent.
/// - Backface culling is left alone — sprites are always camera-facing quads.
#pragma once

#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Vestige
{

class SpriteAtlas;

/// @brief One sprite draw request. Populated by SpriteSystem per frame.
///
/// Transform is stored as two rows of a 3x3 affine matrix (row-major: the
/// third row is implicitly (0,0,1)), so the GPU reconstructs the 2D model
/// matrix without needing the full 16-float mat4.
struct SpriteInstance
{
    glm::vec4 transformRow0;   ///< (a, b, tx, _unused)
    glm::vec4 transformRow1;   ///< (c, d, ty, _unused)
    glm::vec4 uvRect;          ///< (u0, v0, u1, v1)
    glm::vec4 tint;            ///< RGBA
    float     depth;           ///< [0..1] — derived from sort key by SpriteSystem.
};

/// @brief Instance-rate sprite renderer.
class SpriteRenderer
{
public:
    SpriteRenderer();
    ~SpriteRenderer();

    // Non-copyable — owns GL resources.
    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;

    /// @brief Initializes GL resources.
    /// @param assetPath Base path containing `shaders/sprite.vert.glsl` etc.
    /// @return True on success.
    bool initialize(const std::string& assetPath);

    /// @brief Releases GL resources. Safe to call repeatedly.
    void shutdown();

    /// @brief Whether initialize() succeeded.
    bool isInitialized() const { return m_initialized; }

    /// @brief Begin a frame. Uploads view-projection for subsequent draws.
    /// @param viewProj 4x4 combined view-projection (orthographic for 2D
    /// but any projection is accepted).
    void begin(const glm::mat4& viewProj);

    /// @brief Queue a batch of sprites bound to a single atlas texture.
    /// Instances should already be in draw order; the renderer does not
    /// re-sort them.
    /// @param atlas Atlas whose texture id is bound during the draw.
    /// @param instances Contiguous instance data.
    /// @param isTransparent Selects blend / depth-write state.
    void drawBatch(const SpriteAtlas& atlas,
                   const std::vector<SpriteInstance>& instances,
                   bool isTransparent);

    /// @brief End a frame. Restores GL state modified by the renderer
    /// (depth mask / blend / active-shader binding).
    void end();

    /// @brief Maximum number of instances the internal VBO accommodates
    /// without a resize. Larger batches are auto-split across multiple
    /// draw calls so a pathological scene never crashes.
    static constexpr std::size_t MAX_INSTANCES_PER_DRAW = 10000;

private:
    /// @brief Lazy-grows the instance VBO to accommodate at least `count`
    /// instances. Returns the current capacity afterwards.
    std::size_t ensureInstanceCapacity(std::size_t count);

    GLuint m_vao           = 0;
    GLuint m_cornerVbo     = 0;    ///< 4-vertex corner quad (static).
    GLuint m_instanceVbo   = 0;    ///< Per-sprite data (dynamic).
    std::size_t m_instanceCapacity = 0;

    Shader m_shader;
    glm::mat4 m_viewProj = glm::mat4(1.0f);

    // GL state we restore on end().
    GLboolean m_savedDepthMask = GL_TRUE;
    GLboolean m_savedBlend     = GL_FALSE;
    GLint     m_savedBlendSrc  = GL_ONE;
    GLint     m_savedBlendDst  = GL_ZERO;
    bool      m_frameActive    = false;
    bool      m_initialized    = false;
};

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file smaa.h
/// @brief SMAA (Subpixel Morphological Anti-Aliasing) implementation.
#pragma once

#include "renderer/framebuffer.h"
#include "renderer/shader.h"

#include <glad/gl.h>

#include <memory>

namespace Vestige
{

/// @brief Manages SMAA state: lookup textures, FBOs, and shader passes.
///
/// Implements SMAA 1x at the HIGH quality preset. The three passes
/// (smaa_edge, smaa_blend, smaa_neighborhood .glsl) carry the reference
/// SMAA code unchanged, and the lookup tables are the reference AreaTex and
/// SearchTex bytes (external/smaa, MIT). tests/test_smaa.cpp pins both.
/// - Luma edge detection with local contrast adaptation
/// - Orthogonal search (16 steps), diagonal search (8 steps), corner rounding
/// - Area and search table lookups for the blend weights
/// - Neighborhood blending for the final output
class Smaa
{
public:
    /// @brief Creates SMAA resources at the given resolution.
    Smaa(int width, int height);
    ~Smaa();

    // Non-copyable
    Smaa(const Smaa&) = delete;
    Smaa& operator=(const Smaa&) = delete;

    /// @brief Resizes all SMAA framebuffers.
    void resize(int width, int height);

    /// @brief Gets the edge detection FBO.
    Framebuffer& getEdgeFbo();

    /// @brief Gets the blend weight FBO.
    Framebuffer& getBlendFbo();

    /// @brief Gets the area lookup texture ID.
    GLuint getAreaTexture() const;

    /// @brief Gets the search lookup texture ID.
    GLuint getSearchTexture() const;

private:
    void createAreaTexture();
    void createSearchTexture();

    std::unique_ptr<Framebuffer> m_edgeFbo;   // RG8 edge detection output
    std::unique_ptr<Framebuffer> m_blendFbo;  // RGBA8 blend weight output

    GLuint m_areaTexture = 0;    // 160x560 RG8 lookup (AreaTex)
    GLuint m_searchTexture = 0;  // 64x16 R8 lookup (SearchTex)

    int m_width;
    int m_height;
};

} // namespace Vestige

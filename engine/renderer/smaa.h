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
/// Implements SMAA 1x at HIGH quality preset:
/// - Luma-based edge detection with local contrast adaptation
/// - Orthogonal pattern search (16 steps max)
/// - Area texture lookup for accurate blend weights
/// - Neighborhood blending for final output
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
    void generateAreaTexture();
    void generateSearchTexture();

    std::unique_ptr<Framebuffer> m_edgeFbo;   // RG8 edge detection output
    std::unique_ptr<Framebuffer> m_blendFbo;  // RGBA8 blend weight output

    GLuint m_areaTexture = 0;    // 160x560 RG8 lookup
    GLuint m_searchTexture = 0;  // 64x16 R8 lookup

    int m_width;
    int m_height;

    // SMAA constants
    static constexpr int AREATEX_WIDTH = 160;
    static constexpr int AREATEX_HEIGHT = 560;
    static constexpr int SEARCHTEX_WIDTH = 64;
    static constexpr int SEARCHTEX_HEIGHT = 16;
    static constexpr int AREATEX_MAX_DISTANCE = 16;
    static constexpr int AREATEX_SUBTEX_COUNT = 7;
};

} // namespace Vestige

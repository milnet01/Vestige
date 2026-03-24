/// @file smaa.cpp
/// @brief SMAA implementation with runtime lookup texture generation.
#include "renderer/smaa.h"
#include "core/logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace Vestige
{

// ============================================================================
// Area texture generation (orthogonal edge patterns)
// ============================================================================

/// Compute the area under a reconstructed edge line within a pixel.
/// The edge spans from (-d1, h1) to (d2+1, h2) where h1/h2 are the
/// crossing heights at each end (0 or 1 relative to pixel height).
/// Returns the fractional area of the pixel covered by each side.
static void computeOrthogonalArea(float d1, float d2, float e1, float e2,
                                   float& areaR, float& areaG)
{
    // Total length of the edge segment (in pixels)
    float totalLen = d1 + d2 + 1.0f;

    if (totalLen < 1.0f)
    {
        areaR = 0.0f;
        areaG = 0.0f;
        return;
    }

    // The crossing heights at each end determine the edge shape.
    // e1 encodes whether there's a perpendicular crossing at the left end.
    // e2 encodes the same for the right end.
    // Values are 0 (flat termination) or 1 (corner/crossing).
    float h1 = e1;
    float h2 = e2;

    // If both ends are the same height, no transition → no blending
    if (std::abs(h1 - h2) < 0.01f)
    {
        areaR = 0.0f;
        areaG = 0.0f;
        return;
    }

    // Position of our pixel's center relative to the left end
    float pos = d1 + 0.5f;

    // Crossing height at our pixel (linear interpolation along the edge)
    float crossing = h1 + (h2 - h1) * (pos / totalLen);
    crossing = std::clamp(crossing, 0.0f, 1.0f);

    // The area represents blend weights for the two sides.
    // For a horizontal edge: R = area below, G = area above.
    // The blend weight tells us how much to mix with the neighbor pixel.
    // At the midpoint of a long edge: crossing ≈ 0.5, equal blend.
    // Near the start: crossing near h1, biased blend.
    areaR = crossing;
    areaG = 1.0f - crossing;

    // Scale down areas for very short edges (less prominent blending)
    if (totalLen <= 2.0f)
    {
        float scale = totalLen / 2.0f;
        areaR *= scale;
        areaG *= scale;
    }
}

/// Generate the 160×560 area lookup texture.
static void generateAreaTextureData(std::vector<unsigned char>& data,
                                     int width, int height, int maxDist, int subtexCount)
{
    data.resize(static_cast<size_t>(width * height * 2), 0);

    int blockSize = maxDist + 1;  // 17 pixels per block (d = 0..16)
    int orthoWidth = width / 2;   // 80 pixels for orthogonal

    for (int subtex = 0; subtex < subtexCount; subtex++)
    {
        int baseY = subtex * (orthoWidth);  // Each layer is 80 rows

        for (int e2Block = 0; e2Block < 5; e2Block++)
        {
            for (int e1Block = 0; e1Block < 5; e1Block++)
            {
                float e1 = static_cast<float>(e1Block) / 4.0f;
                float e2 = static_cast<float>(e2Block) / 4.0f;

                for (int d2 = 0; d2 < blockSize && d2 < maxDist + 1; d2++)
                {
                    for (int d1 = 0; d1 < blockSize && d1 < maxDist + 1; d1++)
                    {
                        float areaR = 0.0f;
                        float areaG = 0.0f;
                        computeOrthogonalArea(
                            static_cast<float>(d1), static_cast<float>(d2),
                            e1, e2, areaR, areaG);

                        int x = e1Block * maxDist + d1;
                        int y = baseY + e2Block * maxDist + d2;

                        if (x >= orthoWidth || y >= height)
                        {
                            continue;
                        }

                        size_t idx = static_cast<size_t>((y * width + x) * 2);
                        data[idx + 0] = static_cast<unsigned char>(
                            std::clamp(areaR * 255.0f, 0.0f, 255.0f));
                        data[idx + 1] = static_cast<unsigned char>(
                            std::clamp(areaG * 255.0f, 0.0f, 255.0f));

                        // Mirror to diagonal half (simplified — same data for now)
                        int dx = orthoWidth + x;
                        if (dx < width)
                        {
                            size_t dIdx = static_cast<size_t>((y * width + dx) * 2);
                            data[dIdx + 0] = data[idx + 0];
                            data[dIdx + 1] = data[idx + 1];
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Search texture generation
// ============================================================================

/// The search texture encodes how many extra pixels to advance during
/// the edge search. For each bit pattern of edges, it stores a value
/// that tells the shader whether to continue searching or stop.
static void generateSearchTextureData(std::vector<unsigned char>& data,
                                       int width, int height)
{
    data.resize(static_cast<size_t>(width * height), 0);

    // The search texture is indexed by a 7-bit pattern encoding:
    // - Whether there's an edge at each of 7 sample positions along the search direction
    // - The pattern determines how far the search should jump in its last step
    //
    // For simplicity, we fill it with a uniform "continue searching" pattern:
    // - 0x00 = stop (no more edges in this direction)
    // - 0x7F = half step (edge might end soon)
    // - 0xFE = full step (edge continues)
    //
    // This generates a basic but functional search pattern:
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            // Decode the bit pattern from (x, y)
            int pattern = y * width + x;

            unsigned char value = 0x00;  // Default: stop

            // If any edge bits are set, allow searching
            if (pattern > 0)
            {
                // Check if the pattern has continuous edges
                int bits = pattern & 0x7F;
                if (bits != 0)
                {
                    // Count trailing set bits
                    int trailing = 0;
                    int tmp = bits;
                    while ((tmp & 1) != 0)
                    {
                        trailing++;
                        tmp >>= 1;
                    }

                    if (trailing >= 3)
                    {
                        value = 0xFE;  // Long edge — full advance
                    }
                    else if (trailing >= 1)
                    {
                        value = 0x7F;  // Short edge — half advance
                    }
                }
            }

            data[static_cast<size_t>(y * width + x)] = value;
        }
    }
}

// ============================================================================
// SMAA class implementation
// ============================================================================

Smaa::Smaa(int width, int height)
    : m_width(width)
    , m_height(height)
{
    // Edge detection FBO (RG8 — stores horizontal and vertical edges)
    FramebufferConfig edgeConfig;
    edgeConfig.width = width;
    edgeConfig.height = height;
    edgeConfig.samples = 1;
    edgeConfig.hasColorAttachment = true;
    edgeConfig.hasDepthAttachment = false;
    edgeConfig.isFloatingPoint = false;  // RG8 is sufficient
    m_edgeFbo = std::make_unique<Framebuffer>(edgeConfig);

    // Blend weight FBO (RGBA8 — stores blend weights for 4 directions)
    FramebufferConfig blendConfig;
    blendConfig.width = width;
    blendConfig.height = height;
    blendConfig.samples = 1;
    blendConfig.hasColorAttachment = true;
    blendConfig.hasDepthAttachment = false;
    blendConfig.isFloatingPoint = false;  // RGBA8
    m_blendFbo = std::make_unique<Framebuffer>(blendConfig);

    // Generate lookup textures
    generateAreaTexture();
    generateSearchTexture();

    Logger::info("SMAA initialized: " + std::to_string(width) + "x" + std::to_string(height));
}

Smaa::~Smaa()
{
    if (m_areaTexture != 0)
    {
        glDeleteTextures(1, &m_areaTexture);
    }
    if (m_searchTexture != 0)
    {
        glDeleteTextures(1, &m_searchTexture);
    }
}

void Smaa::resize(int width, int height)
{
    m_width = width;
    m_height = height;
    m_edgeFbo->resize(width, height);
    m_blendFbo->resize(width, height);
}

Framebuffer& Smaa::getEdgeFbo()
{
    return *m_edgeFbo;
}

Framebuffer& Smaa::getBlendFbo()
{
    return *m_blendFbo;
}

GLuint Smaa::getAreaTexture() const
{
    return m_areaTexture;
}

GLuint Smaa::getSearchTexture() const
{
    return m_searchTexture;
}

void Smaa::generateAreaTexture()
{
    std::vector<unsigned char> data;
    generateAreaTextureData(data, AREATEX_WIDTH, AREATEX_HEIGHT,
                            AREATEX_MAX_DISTANCE, AREATEX_SUBTEX_COUNT);

    glCreateTextures(GL_TEXTURE_2D, 1, &m_areaTexture);
    glTextureStorage2D(m_areaTexture, 1, GL_RG8, AREATEX_WIDTH, AREATEX_HEIGHT);
    glTextureSubImage2D(m_areaTexture, 0, 0, 0,
                        AREATEX_WIDTH, AREATEX_HEIGHT,
                        GL_RG, GL_UNSIGNED_BYTE, data.data());
    glTextureParameteri(m_areaTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_areaTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_areaTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_areaTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Smaa::generateSearchTexture()
{
    std::vector<unsigned char> data;
    generateSearchTextureData(data, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);

    glCreateTextures(GL_TEXTURE_2D, 1, &m_searchTexture);
    glTextureStorage2D(m_searchTexture, 1, GL_R8, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
    glTextureSubImage2D(m_searchTexture, 0, 0, 0,
                        SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT,
                        GL_RED, GL_UNSIGNED_BYTE, data.data());
    // Search texture MUST use nearest filtering
    glTextureParameteri(m_searchTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_searchTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_searchTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_searchTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace Vestige

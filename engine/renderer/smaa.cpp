// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file smaa.cpp
/// @brief SMAA resources: the two FBOs and the reference lookup tables.
#include "renderer/smaa.h"
#include "core/logger.h"

// The reference SMAA lookup tables (external/smaa, MIT). Each is a fixed
// byte array, stored top row first; it is uploaded unchanged (see the
// orientation note in smaa_blend.frag.glsl).
#include <AreaTex.h>
#include <SearchTex.h>

#include <string>

namespace Vestige
{

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

    // Upload the reference lookup tables
    createAreaTexture();
    createSearchTexture();

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

void Smaa::createAreaTexture()
{
    static_assert(sizeof(areaTexBytes) == AREATEX_SIZE,
                  "AreaTex.h does not match its declared 160x560 RG8 size");

    // AreaTex rows are 160 * 2 = 320 bytes, a multiple of 4, so the default
    // GL_UNPACK_ALIGNMENT of 4 reads them correctly.
    glCreateTextures(GL_TEXTURE_2D, 1, &m_areaTexture);
    glTextureStorage2D(m_areaTexture, 1, GL_RG8, AREATEX_WIDTH, AREATEX_HEIGHT);
    glTextureSubImage2D(m_areaTexture, 0, 0, 0,
                        AREATEX_WIDTH, AREATEX_HEIGHT,
                        GL_RG, GL_UNSIGNED_BYTE, areaTexBytes);
    glTextureParameteri(m_areaTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_areaTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_areaTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_areaTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Smaa::createSearchTexture()
{
    static_assert(sizeof(searchTexBytes) == SEARCHTEX_SIZE,
                  "SearchTex.h does not match its declared 64x16 R8 size");

    // SearchTex rows are 64 bytes, a multiple of 4 (default unpack alignment).
    glCreateTextures(GL_TEXTURE_2D, 1, &m_searchTexture);
    glTextureStorage2D(m_searchTexture, 1, GL_R8, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
    glTextureSubImage2D(m_searchTexture, 0, 0, 0,
                        SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT,
                        GL_RED, GL_UNSIGNED_BYTE, searchTexBytes);
    // Search texture MUST use nearest filtering
    glTextureParameteri(m_searchTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_searchTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(m_searchTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_searchTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace Vestige

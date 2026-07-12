// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_material_set.cpp
/// @brief TerrainMaterialSet — decodes the ground layers and builds the 2D arrays.
#include "environment/terrain_material_set.h"
#include "core/logger.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Vestige
{

namespace
{

/// @brief Mip-level count for a W×H texture (mirrors texture.cpp's helper).
GLsizei computeMipLevels(int width, int height)
{
    int maxDim = std::max(width, height);
    return static_cast<GLsizei>(std::floor(std::log2(maxDim))) + 1;
}

/// @brief One decoded source map, forced to a fixed channel count.
struct DecodedImage
{
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    bool ok = false;
};

/// @brief Decodes an image forcing `desiredChannels` (deterministic upload layout).
/// Returns `ok == false` on a missing/corrupt file or out-of-range dimensions.
DecodedImage decodeForced(const std::string& path, int desiredChannels)
{
    DecodedImage img;
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, desiredChannels);
    if (!data)
    {
        Logger::error("TerrainMaterialSet: failed to decode " + path + " — "
                      + stbi_failure_reason());
        return img;
    }
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
    {
        Logger::error("TerrainMaterialSet: dimensions invalid or too large: " + path);
        stbi_image_free(data);
        return img;
    }
    img.width = w;
    img.height = h;
    img.pixels.assign(data,
                      data + static_cast<size_t>(w) * static_cast<size_t>(h)
                          * static_cast<size_t>(desiredChannels));
    stbi_image_free(data);
    img.ok = true;
    return img;
}

/// @brief Builds one `GL_TEXTURE_2D_ARRAY` (4 layers) with trilinear + anisotropic
/// filtering, REPEAT wrap, and a full mip chain. Caller brackets uploads with
/// `GL_UNPACK_ALIGNMENT = 1` (design §4.2 banding guarantee).
GLuint buildArray(GLenum internalFormat, GLenum dataFormat, int width, int height,
                  const std::array<DecodedImage, 4>& imgs)
{
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);

    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // GL_TEXTURE_MAX_ANISOTROPY is core in GL 4.6; used unguarded elsewhere in the
    // engine (texture.cpp) — inherits that assumption, design §4.1.
    glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);

    GLsizei mipLevels = computeMipLevels(width, height);
    glTextureStorage3D(tex, mipLevels, internalFormat, width, height, 4);

    for (int layer = 0; layer < 4; ++layer)
    {
        glTextureSubImage3D(tex, 0, 0, 0, layer, width, height, 1,
                            dataFormat, GL_UNSIGNED_BYTE,
                            imgs[static_cast<size_t>(layer)].pixels.data());
    }
    glGenerateTextureMipmap(tex);
    return tex;
}

}  // namespace

TerrainMaterialSet::~TerrainMaterialSet()
{
    release();
}

TerrainMaterialSet::TerrainMaterialSet(TerrainMaterialSet&& other) noexcept
    : m_albedoArray(other.m_albedoArray)
    , m_normalArray(other.m_normalArray)
    , m_materialArray(other.m_materialArray)
    , m_tilings(other.m_tilings)
    , m_valid(other.m_valid)
{
    other.m_albedoArray = 0;
    other.m_normalArray = 0;
    other.m_materialArray = 0;
    other.m_valid = false;
}

TerrainMaterialSet& TerrainMaterialSet::operator=(TerrainMaterialSet&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_albedoArray = other.m_albedoArray;
        m_normalArray = other.m_normalArray;
        m_materialArray = other.m_materialArray;
        m_tilings = other.m_tilings;
        m_valid = other.m_valid;
        other.m_albedoArray = 0;
        other.m_normalArray = 0;
        other.m_materialArray = 0;
        other.m_valid = false;
    }
    return *this;
}

void TerrainMaterialSet::release()
{
    // Guard each handle: a never-loaded set (handles 0) must not touch GL — it may be
    // destroyed in a context-free unit test.
    if (m_albedoArray != 0)
    {
        glDeleteTextures(1, &m_albedoArray);
        m_albedoArray = 0;
    }
    if (m_normalArray != 0)
    {
        glDeleteTextures(1, &m_normalArray);
        m_normalArray = 0;
    }
    if (m_materialArray != 0)
    {
        glDeleteTextures(1, &m_materialArray);
        m_materialArray = 0;
    }
    m_valid = false;
}

bool TerrainMaterialSet::load(const std::array<TerrainLayerDesc, 4>& layers)
{
    release();

    stbi_set_flip_vertically_on_load_thread(true);

    // Decode all 12 source maps up front (albedo forced RGBA, normal/material RGB).
    // Decode-first ordering means any failure aborts before a single GL texture is
    // created, so no partial cleanup is needed.
    std::array<DecodedImage, 4> albedo;
    std::array<DecodedImage, 4> normal;
    std::array<DecodedImage, 4> material;
    for (std::size_t i = 0; i < 4; ++i)
    {
        albedo[i] = decodeForced(layers[i].albedoPath, 4);
        normal[i] = decodeForced(layers[i].normalPath, 3);
        material[i] = decodeForced(layers[i].materialPath, 3);
        if (!albedo[i].ok || !normal[i].ok || !material[i].ok)
        {
            Logger::error("TerrainMaterialSet: layer " + std::to_string(i)
                          + " failed to decode — using flat-colour fallback");
            return false;
        }
    }

    // All layers and all three map types must share one width×height (array requirement).
    const int width = albedo[0].width;
    const int height = albedo[0].height;
    auto dimsMatch = [&](const std::array<DecodedImage, 4>& imgs)
    {
        return std::all_of(imgs.begin(), imgs.end(), [&](const DecodedImage& img)
                           { return img.width == width && img.height == height; });
    };
    if (!dimsMatch(albedo) || !dimsMatch(normal) || !dimsMatch(material))
    {
        Logger::error("TerrainMaterialSet: layer dimensions mismatch (all maps must be "
                      + std::to_string(width) + "x" + std::to_string(height)
                      + ") — using flat-colour fallback");
        return false;
    }

    // Belt-and-suspenders row alignment for the 3-component (RGB8) normal/material
    // uploads: a non-4-aligned row width shears into diagonal bands (recorded engine
    // lesson, design §4.2). Committed textures are power-of-two width, but set 1 anyway.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    m_albedoArray = buildArray(GL_SRGB8_ALPHA8, GL_RGBA, width, height, albedo);
    m_normalArray = buildArray(GL_RGB8, GL_RGB, width, height, normal);
    m_materialArray = buildArray(GL_RGB8, GL_RGB, width, height, material);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    for (std::size_t i = 0; i < 4; ++i)
    {
        m_tilings[i] = layers[i].tiling;
    }

    m_valid = true;
    Logger::info("TerrainMaterialSet: loaded 4 ground layers ("
                 + std::to_string(width) + "x" + std::to_string(height) + ")");
    return true;
}

void TerrainMaterialSet::bind(int albedoUnit, int normalUnit, int materialUnit) const
{
    glBindTextureUnit(static_cast<GLuint>(albedoUnit), m_albedoArray);
    glBindTextureUnit(static_cast<GLuint>(normalUnit), m_normalArray);
    glBindTextureUnit(static_cast<GLuint>(materialUnit), m_materialArray);
}

}  // namespace Vestige

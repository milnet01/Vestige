// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file texture.cpp
/// @brief Texture implementation using stb_image and tinyexr.
#include "renderer/texture.h"
#include "profiler/run_inputs.h"
#include "core/logger.h"

#include <stb_image.h>
#include <tinyexr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace Vestige
{

/// @brief Computes number of mip levels for a 2D texture.
static GLsizei computeMipLevels(int width, int height)
{
    int maxDim = std::max(width, height);
    return static_cast<GLsizei>(std::floor(std::log2(maxDim))) + 1;
}

/// @brief Checks if a file path ends with the given extension (case-insensitive).
/// Largest width or height any load path accepts.
static constexpr int kMaxTextureSize = 16384;

static bool validTextureSize(int width, int height)
{
    return width > 0 && height > 0 && width <= kMaxTextureSize && height <= kMaxTextureSize;
}

/// The upload format for 8-bit data with this many channels (1, 3 or 4).
static GLenum dataFormatForChannels(int channels)
{
    if (channels == 4)
    {
        return GL_RGBA;
    }
    return channels == 1 ? GL_RED : GL_RGB;
}

static bool hasExtension(const std::string& path, const std::string& ext)
{
    if (path.size() < ext.size())
    {
        return false;
    }
    std::string ending = path.substr(path.size() - ext.size());
    std::transform(ending.begin(), ending.end(), ending.begin(), ::tolower);
    return ending == ext;
}

Texture::Texture()
    : m_textureId(0)
    , m_width(0)
    , m_height(0)
{
}

Texture::~Texture()
{
    if (m_textureId != 0)
    {
        glDeleteTextures(1, &m_textureId);
    }
}

Texture::Texture(Texture&& other) noexcept
    : m_textureId(other.m_textureId)
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_filterMode(other.m_filterMode)
{
    other.m_textureId = 0;
    other.m_width = 0;
    other.m_height = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other)
    {
        if (m_textureId != 0)
        {
            glDeleteTextures(1, &m_textureId);
        }
        m_textureId = other.m_textureId;
        m_width = other.m_width;
        m_height = other.m_height;
        m_filterMode = other.m_filterMode;
        other.m_textureId = 0;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

GLenum Texture::selectInternalFormat(int channels, bool linear)
{
    if (channels == 4)
    {
        return linear ? GL_RGBA8 : GL_SRGB8_ALPHA8;
    }
    else if (channels == 3)
    {
        return linear ? GL_RGB8 : GL_SRGB8;
    }
    else
    {
        // Single-channel textures are always linear (no sRGB variant for GL_R8)
        return GL_R8;
    }
}

void Texture::releaseGpuTexture()
{
    if (m_textureId != 0)
    {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_width = 0;
    m_height = 0;
}

void Texture::createAndUpload(GLenum internalFormat, GLenum dataFormat, GLenum dataType,
                              const void* pixels, std::size_t rowBytes)
{
    // Create texture with DSA (immutable storage)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_textureId);

    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTextureStorage2D(m_textureId, computeMipLevels(m_width, m_height), internalFormat,
                       m_width, m_height);

    // Rows that are not a multiple of 4 bytes (3- or 1-channel data at most
    // widths) are misread at the default GL_UNPACK_ALIGNMENT of 4, which
    // shears the image into diagonal bands — the terrain "corduroy" artefact
    // fixed in 82d0738.
    const bool unalignedRows = rowBytes % 4 != 0;
    if (unalignedRows)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }
    glTextureSubImage2D(m_textureId, 0, 0, 0, m_width, m_height, dataFormat, dataType, pixels);
    if (unalignedRows)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    glGenerateTextureMipmap(m_textureId);

    // A new texture starts trilinear; put back the mode chosen for this one.
    applyFilterMode();
}

bool Texture::loadFromFile(const std::string& filePath, bool linear)
{
    releaseGpuTexture();

    // EXR files use tinyexr (float HDR data — always linear)
    if (hasExtension(filePath, ".exr"))
    {
        return loadFromExr(filePath);
    }

    // All other formats use stb_image (unsigned byte data)
    stbi_set_flip_vertically_on_load_thread(true);

    int channels = 0;
    noteRunInput(filePath);
    unsigned char* data = stbi_load(filePath.c_str(), &m_width, &m_height, &channels, 0);
    if (data && channels == 2)
    {
        // Grey + alpha has no upload format below; expand to RGBA so the
        // upload reads the w*h*4 bytes it is told to (GL_RGB would read
        // w*h*3 from a w*h*2 buffer and drop the alpha).
        stbi_image_free(data);
        data = stbi_load(filePath.c_str(), &m_width, &m_height, &channels, 4);
        channels = 4;
    }
    if (!data)
    {
        Logger::error("Failed to load texture: " + filePath + " — " + stbi_failure_reason());
        return false;
    }

    // Validate dimensions
    if (!validTextureSize(m_width, m_height))
    {
        Logger::error("Texture dimensions invalid or too large: " + filePath);
        stbi_image_free(data);
        m_width = 0;
        m_height = 0;
        return false;
    }

    createAndUpload(selectInternalFormat(channels, linear), dataFormatForChannels(channels),
                    GL_UNSIGNED_BYTE, data,
                    static_cast<std::size_t>(m_width) * static_cast<std::size_t>(channels));

    stbi_image_free(data);

    Logger::debug("Texture loaded: " + filePath + " ("
        + std::to_string(m_width) + "x" + std::to_string(m_height)
        + ", " + std::to_string(channels) + " channels"
        + (linear ? ", linear" : ", sRGB") + ")");

    return true;
}

bool Texture::loadFromMemory(const unsigned char* compressedData, size_t dataSize,
                              bool linear)
{
    releaseGpuTexture();

    if (!compressedData || dataSize == 0)
    {
        Logger::error("Texture::loadFromMemory: null or empty compressed data");
        return false;
    }

    stbi_set_flip_vertically_on_load_thread(true);

    // Guard against size_t -> int truncation (stbi uses int for size)
    if (dataSize > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        Logger::error("Texture::loadFromMemory: data size exceeds INT_MAX");
        return false;
    }

    int channels = 0;
    unsigned char* data = stbi_load_from_memory(
        compressedData, static_cast<int>(dataSize),
        &m_width, &m_height, &channels, 0);
    if (data && channels == 2)
    {
        // Grey + alpha: expand to RGBA, as in loadFromFile.
        stbi_image_free(data);
        data = stbi_load_from_memory(
            compressedData, static_cast<int>(dataSize),
            &m_width, &m_height, &channels, 4);
        channels = 4;
    }
    if (!data)
    {
        Logger::error("Failed to decode texture from memory — " + std::string(stbi_failure_reason()));
        return false;
    }

    if (!validTextureSize(m_width, m_height))
    {
        Logger::error("Texture from memory: dimensions invalid or too large");
        stbi_image_free(data);
        m_width = 0;
        m_height = 0;
        return false;
    }

    createAndUpload(selectInternalFormat(channels, linear), dataFormatForChannels(channels),
                    GL_UNSIGNED_BYTE, data,
                    static_cast<std::size_t>(m_width) * static_cast<std::size_t>(channels));

    stbi_image_free(data);

    Logger::debug("Texture loaded from memory ("
        + std::to_string(m_width) + "x" + std::to_string(m_height)
        + ", " + std::to_string(channels) + " channels"
        + (linear ? ", linear" : ", sRGB") + ")");

    return true;
}

bool Texture::loadFromMemory(const unsigned char* rawData, int width, int height,
                              int channels, bool linear)
{
    releaseGpuTexture();

    if (!rawData || !validTextureSize(width, height))
    {
        Logger::error("Texture::loadFromMemory: invalid raw data or dimensions");
        return false;
    }

    if (channels != 1 && channels != 3 && channels != 4)
    {
        Logger::error("Texture::loadFromMemory: unsupported channel count " + std::to_string(channels));
        return false;
    }

    m_width = width;
    m_height = height;

    // Flip vertically to match OpenGL convention (bottom-to-top)
    int rowBytes = width * channels;
    std::vector<unsigned char> flipped(static_cast<size_t>(rowBytes) * static_cast<size_t>(height));
    for (int y = 0; y < height; y++)
    {
        const unsigned char* srcRow = rawData + static_cast<size_t>(y) * static_cast<size_t>(rowBytes);
        unsigned char* dstRow = flipped.data()
            + static_cast<size_t>(height - 1 - y) * static_cast<size_t>(rowBytes);
        std::memcpy(dstRow, srcRow, static_cast<size_t>(rowBytes));
    }

    createAndUpload(selectInternalFormat(channels, linear), dataFormatForChannels(channels),
                    GL_UNSIGNED_BYTE, flipped.data(), static_cast<std::size_t>(rowBytes));

    Logger::debug("Texture loaded from raw data ("
        + std::to_string(m_width) + "x" + std::to_string(m_height)
        + ", " + std::to_string(channels) + " channels"
        + (linear ? ", linear" : ", sRGB") + ")");

    return true;
}

bool Texture::decodeExr(const std::string& filePath, int& width, int& height,
                        std::vector<float>& rgba)
{
    float* data = nullptr;
    const char* err = nullptr;
    noteRunInput(filePath);
    int ret = LoadEXR(&data, &width, &height, filePath.c_str(), &err);

    if (ret != TINYEXR_SUCCESS)
    {
        std::string errMsg = err ? err : "unknown error";
        Logger::error("Failed to load EXR texture: " + filePath + " — " + errMsg);
        FreeEXRErrorMessage(err);
        return false;
    }

    // Validate dimensions
    if (!validTextureSize(width, height))
    {
        Logger::error("EXR texture dimensions invalid or too large: " + filePath);
        free(data);
        return false;
    }

    // LoadEXR returns RGBA float data.
    // EXR images are stored top-to-bottom; flip vertically to match OpenGL convention.
    const size_t rowSize = static_cast<size_t>(width) * 4;
    rgba.resize(rowSize * static_cast<size_t>(height));
    for (int y = 0; y < height; y++)
    {
        std::memcpy(rgba.data() + static_cast<size_t>(height - 1 - y) * rowSize,
                    data + static_cast<size_t>(y) * rowSize, rowSize * sizeof(float));
    }
    free(data);
    return true;
}

bool Texture::loadFromExr(const std::string& filePath)
{
    // Note: releaseGpuTexture() already called by loadFromFile() caller
    std::vector<float> data;
    if (!decodeExr(filePath, m_width, m_height, data))
        return false;

    // HDR float RGBA: rows are always 4-byte aligned.
    createAndUpload(GL_RGBA16F, GL_RGBA, GL_FLOAT, data.data(),
                    static_cast<std::size_t>(m_width) * 4 * sizeof(float));

    Logger::debug("EXR texture loaded: " + filePath + " ("
        + std::to_string(m_width) + "x" + std::to_string(m_height)
        + ", RGBA float → RGBA16F)");

    return true;
}

void Texture::createSolidColor(unsigned char r, unsigned char g, unsigned char b)
{
    releaseGpuTexture();
    unsigned char pixel[3] = {r, g, b};

    // Create 1x1 texture with DSA (immutable storage)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_textureId);

    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureStorage2D(m_textureId, 1, GL_RGB8, 1, 1);
    glTextureSubImage2D(m_textureId, 0, 0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    m_width = 1;
    m_height = 1;
}

void Texture::bind(unsigned int unit) const
{
    glBindTextureUnit(unit, m_textureId);
}

void Texture::unbind(unsigned int unit)
{
    glBindTextureUnit(unit, 0);
}

GLuint Texture::getId() const
{
    return m_textureId;
}

int Texture::getWidth() const
{
    return m_width;
}

int Texture::getHeight() const
{
    return m_height;
}

bool Texture::isLoaded() const
{
    return m_textureId != 0;
}

std::shared_ptr<Texture> Texture::generateNormalFromHeight(
    const std::string& heightMapPath, float strength)
{
    // Read the height map top row first, as stored: loadFromMemory below flips
    // the generated rows into OpenGL's bottom-up order, the same single flip
    // loadFromFile gives the albedo. Flipping here as well put the bumps upside
    // down against the albedo (3D_E-0874).
    stbi_set_flip_vertically_on_load_thread(false);
    int w = 0;
    int h = 0;
    int channels = 0;
    noteRunInput(heightMapPath);
    unsigned char* heightData = stbi_load(heightMapPath.c_str(), &w, &h, &channels, 1);
    if (!heightData)
    {
        Logger::error("Failed to load height map for normal generation: " + heightMapPath);
        return nullptr;
    }

    // Refuse an oversize map here: past the limit, w * h * 3 overflows an int.
    if (!validTextureSize(w, h))
    {
        Logger::error("Height map dimensions invalid or too large: " + heightMapPath);
        stbi_image_free(heightData);
        return nullptr;
    }

    // Generate normal map using Sobel filter on height gradients
    std::vector<unsigned char> normalData(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            // Sample heights with wrapping (for tileable textures)
            auto sample = [&](int sx, int sy) -> float
            {
                sx = ((sx % w) + w) % w;
                sy = ((sy % h) + h) % h;
                return static_cast<float>(heightData[sy * w + sx]) / 255.0f;
            };

            // Sobel filter for X and Y gradients
            float tl = sample(x - 1, y - 1);
            float t  = sample(x,     y - 1);
            float tr = sample(x + 1, y - 1);
            float l  = sample(x - 1, y    );
            float r  = sample(x + 1, y    );
            float bl = sample(x - 1, y + 1);
            float b  = sample(x,     y + 1);
            float br = sample(x + 1, y + 1);

            float dx = (tr + 2.0f * r + br) - (tl + 2.0f * l + bl);
            float dy = (bl + 2.0f * b + br) - (tl + 2.0f * t + tr);

            // Tangent-space normal (-dh/du, -dh/dv, 1), OpenGL convention
            // (green = +v = up the image). Row y + 1 is DOWN the image, so dy
            // is -dh/dv and enters with a plus sign.
            float nx = -dx * strength;
            float ny = dy * strength;
            float nz = 1.0f;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;

            // Encode from [-1,1] to [0,255]
            size_t idx = static_cast<size_t>((y * w + x) * 3);
            normalData[idx + 0] = static_cast<unsigned char>((nx * 0.5f + 0.5f) * 255.0f);
            normalData[idx + 1] = static_cast<unsigned char>((ny * 0.5f + 0.5f) * 255.0f);
            normalData[idx + 2] = static_cast<unsigned char>((nz * 0.5f + 0.5f) * 255.0f);
        }
    }

    stbi_image_free(heightData);

    // Create the texture from the generated normal data
    auto normalTex = std::make_shared<Texture>();
    if (!normalTex->loadFromMemory(normalData.data(), w, h, 3, true))
    {
        Logger::error("Failed to create normal map texture from height: " + heightMapPath);
        return nullptr;
    }

    Logger::info("Generated normal map from height: " + heightMapPath
        + " (" + std::to_string(w) + "x" + std::to_string(h) + ", strength=" + std::to_string(strength) + ")");
    return normalTex;
}

void Texture::setFilterMode(TextureFilterMode mode)
{
    // Kept even with nothing loaded: the next upload applies it.
    m_filterMode = mode;
    if (m_textureId != 0)
    {
        applyFilterMode();
    }
}

void Texture::applyFilterMode()
{
    switch (m_filterMode)
    {
        case TextureFilterMode::NEAREST:
            glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameterf(m_textureId, GL_TEXTURE_MAX_ANISOTROPY, 1.0f);
            break;
        case TextureFilterMode::LINEAR:
            glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
            glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameterf(m_textureId, GL_TEXTURE_MAX_ANISOTROPY, 1.0f);
            break;
        case TextureFilterMode::TRILINEAR:
            glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameterf(m_textureId, GL_TEXTURE_MAX_ANISOTROPY, 1.0f);
            break;
        case TextureFilterMode::ANISOTROPIC_4X:
            glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameterf(m_textureId, GL_TEXTURE_MAX_ANISOTROPY, 4.0f);
            break;
        case TextureFilterMode::ANISOTROPIC_8X:
            glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameterf(m_textureId, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
            break;
        case TextureFilterMode::ANISOTROPIC_16X:
            glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameterf(m_textureId, GL_TEXTURE_MAX_ANISOTROPY, 16.0f);
            break;
    }
}

TextureFilterMode Texture::getFilterMode() const
{
    return m_filterMode;
}

} // namespace Vestige

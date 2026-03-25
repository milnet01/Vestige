/// @file texture.cpp
/// @brief Texture implementation using stb_image and tinyexr.
#include "renderer/texture.h"
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
    unsigned char* data = stbi_load(filePath.c_str(), &m_width, &m_height, &channels, 0);
    if (!data)
    {
        Logger::error("Failed to load texture: " + filePath + " — " + stbi_failure_reason());
        return false;
    }

    // Validate dimensions
    if (m_width <= 0 || m_height <= 0 || m_width > 16384 || m_height > 16384)
    {
        Logger::error("Texture dimensions invalid or too large: " + filePath);
        stbi_image_free(data);
        return false;
    }

    GLenum internalFormat = selectInternalFormat(channels, linear);
    GLenum dataFormat = GL_RGB;
    if (channels == 4)
    {
        dataFormat = GL_RGBA;
    }
    else if (channels == 1)
    {
        dataFormat = GL_RED;
    }

    // Create texture with DSA (immutable storage)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_textureId);

    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLsizei mipLevels = computeMipLevels(m_width, m_height);
    glTextureStorage2D(m_textureId, mipLevels, internalFormat, m_width, m_height);

    // Set alignment for non-4-byte-aligned rows (RGB=3 bytes, RED=1 byte)
    if (channels != 4)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    glTextureSubImage2D(m_textureId, 0, 0, 0, m_width, m_height,
                        dataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateTextureMipmap(m_textureId);

    if (channels != 4)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

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
    if (!data)
    {
        Logger::error("Failed to decode texture from memory — " + std::string(stbi_failure_reason()));
        return false;
    }

    if (m_width <= 0 || m_height <= 0 || m_width > 16384 || m_height > 16384)
    {
        Logger::error("Texture from memory: dimensions invalid or too large");
        stbi_image_free(data);
        return false;
    }

    GLenum internalFormat = selectInternalFormat(channels, linear);
    GLenum dataFormat = GL_RGB;
    if (channels == 4)
    {
        dataFormat = GL_RGBA;
    }
    else if (channels == 1)
    {
        dataFormat = GL_RED;
    }

    // Create texture with DSA (immutable storage)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_textureId);

    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLsizei mipLevels = computeMipLevels(m_width, m_height);
    glTextureStorage2D(m_textureId, mipLevels, internalFormat, m_width, m_height);

    if (channels != 4)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }

    glTextureSubImage2D(m_textureId, 0, 0, 0, m_width, m_height,
                        dataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateTextureMipmap(m_textureId);

    if (channels != 4)
    {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

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

    if (!rawData || width <= 0 || height <= 0 || width > 16384 || height > 16384)
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

    GLenum internalFormat = selectInternalFormat(channels, linear);
    GLenum dataFormat = GL_RGB;
    if (channels == 4)
    {
        dataFormat = GL_RGBA;
    }
    else if (channels == 1)
    {
        dataFormat = GL_RED;
    }

    // Create texture with DSA (immutable storage)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_textureId);

    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLsizei mipLevels = computeMipLevels(m_width, m_height);
    glTextureStorage2D(m_textureId, mipLevels, internalFormat, m_width, m_height);

    glTextureSubImage2D(m_textureId, 0, 0, 0, m_width, m_height,
                        dataFormat, GL_UNSIGNED_BYTE, flipped.data());
    glGenerateTextureMipmap(m_textureId);

    Logger::debug("Texture loaded from raw data ("
        + std::to_string(m_width) + "x" + std::to_string(m_height)
        + ", " + std::to_string(channels) + " channels"
        + (linear ? ", linear" : ", sRGB") + ")");

    return true;
}

bool Texture::loadFromExr(const std::string& filePath)
{
    // Note: releaseGpuTexture() already called by loadFromFile() caller
    float* data = nullptr;
    const char* err = nullptr;
    int ret = LoadEXR(&data, &m_width, &m_height, filePath.c_str(), &err);

    if (ret != TINYEXR_SUCCESS)
    {
        std::string errMsg = err ? err : "unknown error";
        Logger::error("Failed to load EXR texture: " + filePath + " — " + errMsg);
        FreeEXRErrorMessage(err);
        return false;
    }

    // Validate dimensions
    if (m_width <= 0 || m_height <= 0 || m_width > 16384 || m_height > 16384)
    {
        Logger::error("EXR texture dimensions invalid or too large: " + filePath);
        free(data);
        return false;
    }

    // LoadEXR returns RGBA float data.
    // EXR images are stored top-to-bottom; flip vertically to match OpenGL convention.
    int rowSize = m_width * 4;
    for (int y = 0; y < m_height / 2; y++)
    {
        int topRow = y * rowSize;
        int bottomRow = (m_height - 1 - y) * rowSize;
        for (int x = 0; x < rowSize; x++)
        {
            std::swap(data[topRow + x], data[bottomRow + x]);
        }
    }

    // Create texture with DSA (immutable storage, HDR float)
    glCreateTextures(GL_TEXTURE_2D, 1, &m_textureId);

    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(m_textureId, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_textureId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLsizei mipLevels = computeMipLevels(m_width, m_height);
    glTextureStorage2D(m_textureId, mipLevels, GL_RGBA16F, m_width, m_height);

    glTextureSubImage2D(m_textureId, 0, 0, 0, m_width, m_height,
                        GL_RGBA, GL_FLOAT, data);
    glGenerateTextureMipmap(m_textureId);

    free(data);

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
    // Load the height map as a greyscale image (flip to match OpenGL Y-up convention)
    stbi_set_flip_vertically_on_load_thread(true);
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* heightData = stbi_load(heightMapPath.c_str(), &w, &h, &channels, 1);
    if (!heightData)
    {
        Logger::error("Failed to load height map for normal generation: " + heightMapPath);
        return nullptr;
    }

    // Generate normal map using Sobel filter on height gradients
    std::vector<unsigned char> normalData(static_cast<size_t>(w * h * 3));

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

            // Construct normal vector: (-dx * strength, -dy * strength, 1.0), normalized
            float nx = -dx * strength;
            float ny = -dy * strength;
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

} // namespace Vestige

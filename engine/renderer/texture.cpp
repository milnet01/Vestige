/// @file texture.cpp
/// @brief Texture implementation using stb_image.
#include "renderer/texture.h"
#include "core/logger.h"

#include <stb_image.h>

namespace Vestige
{

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

bool Texture::loadFromFile(const std::string& filePath)
{
    // Validate file size as a basic security check
    stbi_set_flip_vertically_on_load(true);

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

    GLenum internalFormat = GL_RGB8;
    GLenum dataFormat = GL_RGB;
    if (channels == 4)
    {
        internalFormat = GL_RGBA8;
        dataFormat = GL_RGBA;
    }
    else if (channels == 1)
    {
        internalFormat = GL_R8;
        dataFormat = GL_RED;
    }

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    // Texture filtering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload to GPU
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat),
        m_width, m_height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    Logger::debug("Texture loaded: " + filePath + " ("
        + std::to_string(m_width) + "x" + std::to_string(m_height)
        + ", " + std::to_string(channels) + " channels)");

    return true;
}

void Texture::createSolidColor(unsigned char r, unsigned char g, unsigned char b)
{
    unsigned char pixel[3] = {r, g, b};

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, pixel);

    glBindTexture(GL_TEXTURE_2D, 0);

    m_width = 1;
    m_height = 1;
}

void Texture::bind(unsigned int unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
}

void Texture::unbind(unsigned int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
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

} // namespace Vestige

/// @file texture.h
/// @brief OpenGL texture loading and management.
#pragma once

#include <glad/gl.h>

#include <string>

namespace Vestige
{

/// @brief Loads and manages an OpenGL 2D texture.
class Texture
{
public:
    Texture();
    ~Texture();

    // Non-copyable (owns GPU resource)
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Movable
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    /// @brief Loads a texture from an image file (PNG, JPG, BMP, TGA).
    /// @param filePath Path to the image file.
    /// @return True if loading succeeded.
    bool loadFromFile(const std::string& filePath);

    /// @brief Creates a solid 1x1 color texture.
    /// @param r Red component (0-255).
    /// @param g Green component (0-255).
    /// @param b Blue component (0-255).
    void createSolidColor(unsigned char r, unsigned char g, unsigned char b);

    /// @brief Binds this texture to a texture unit.
    /// @param unit Texture unit index (0 = GL_TEXTURE0, etc.).
    void bind(unsigned int unit = 0) const;

    /// @brief Unbinds the texture from the given unit.
    /// @param unit Texture unit index.
    static void unbind(unsigned int unit = 0);

    /// @brief Gets the OpenGL texture ID.
    GLuint getId() const;

    /// @brief Gets the texture width in pixels.
    int getWidth() const;

    /// @brief Gets the texture height in pixels.
    int getHeight() const;

    /// @brief Checks if a texture is loaded.
    bool isLoaded() const;

private:
    GLuint m_textureId;
    int m_width;
    int m_height;
};

} // namespace Vestige

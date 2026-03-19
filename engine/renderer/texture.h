/// @file texture.h
/// @brief OpenGL texture loading and management.
#pragma once

#include <glad/gl.h>

#include <memory>
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
    /// @param linear If true, load as linear data (normal/height/metallic-roughness maps).
    ///               If false (default), load as sRGB color data (albedo/emissive/diffuse).
    /// @return True if loading succeeded.
    bool loadFromFile(const std::string& filePath, bool linear = false);

    /// @brief Loads a texture from compressed image data in memory (PNG, JPEG, etc.).
    /// @param compressedData Pointer to the compressed image bytes.
    /// @param dataSize Size of the compressed data in bytes.
    /// @param linear If true, load as linear data; if false, load as sRGB.
    /// @return True if loading succeeded.
    bool loadFromMemory(const unsigned char* compressedData, size_t dataSize,
                        bool linear = false);

    /// @brief Loads a texture from raw, pre-decoded pixel data.
    /// @param rawData Pointer to the raw pixel data.
    /// @param width Image width in pixels.
    /// @param height Image height in pixels.
    /// @param channels Number of color channels (1, 3, or 4).
    /// @param linear If true, load as linear data; if false, load as sRGB.
    /// @return True if loading succeeded.
    bool loadFromMemory(const unsigned char* rawData, int width, int height,
                        int channels, bool linear = false);

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

    /// @brief Generates a normal map from a height map texture on the CPU.
    /// Computes per-pixel normals using Sobel-filtered height gradients.
    /// @param heightMapPath Path to the height map image file.
    /// @param strength Normal map strength (higher = more pronounced bumps).
    /// @return A new Texture containing the generated normal map, or nullptr on failure.
    static std::shared_ptr<Texture> generateNormalFromHeight(
        const std::string& heightMapPath, float strength = 8.0f);

private:
    bool loadFromExr(const std::string& filePath);

    /// @brief Deletes any existing GPU texture before re-creation.
    void releaseGpuTexture();

    /// @brief Selects the appropriate internal format based on channels and sRGB flag.
    static GLenum selectInternalFormat(int channels, bool linear);

    GLuint m_textureId;
    int m_width;
    int m_height;
};

} // namespace Vestige

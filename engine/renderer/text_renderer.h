/// @file text_renderer.h
/// @brief 2D/3D text rendering and text-based height map generation.
#pragma once

#include "renderer/font.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace Vestige
{

/// @brief Renders text in screen-space (2D) and world-space (3D).
class TextRenderer
{
public:
    TextRenderer();
    ~TextRenderer();

    // Non-copyable
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    /// @brief Initializes the text renderer with a font and shader.
    /// @param fontPath Path to a .ttf font file.
    /// @param shaderPath Base path to the assets directory (for text shaders).
    /// @param pixelSize Font size in pixels.
    /// @return True if initialization succeeded.
    bool initialize(const std::string& fontPath, const std::string& shaderPath,
                    int pixelSize = 48);

    /// @brief Renders text in screen-space (2D overlay).
    /// @param text The string to render.
    /// @param x Screen X position (pixels from left).
    /// @param y Screen Y position (pixels from top).
    /// @param scale Text scale factor (1.0 = pixel size).
    /// @param color Text color.
    /// @param screenWidth Viewport width for ortho projection.
    /// @param screenHeight Viewport height for ortho projection.
    void renderText2D(const std::string& text, float x, float y, float scale,
                      const glm::vec3& color, int screenWidth, int screenHeight);

    /// @brief Renders text in world-space (3D positioned).
    /// @param text The string to render.
    /// @param modelMatrix World transform for the text origin.
    /// @param scale Text scale in world units.
    /// @param color Text color.
    /// @param view View matrix.
    /// @param projection Projection matrix.
    void renderText3D(const std::string& text, const glm::mat4& modelMatrix,
                      float scale, const glm::vec3& color,
                      const glm::mat4& view, const glm::mat4& projection);

    /// @brief Generates a height map texture from text for POM embossing/engraving.
    /// @param text The text to render into the height map.
    /// @param textureWidth Width of the generated texture.
    /// @param textureHeight Height of the generated texture.
    /// @param embossed If true, text is raised (white on black). If false, engraved (black on white).
    /// @return Shared pointer to the generated texture.
    std::shared_ptr<Texture> generateTextHeightMap(const std::string& text,
                                                     int textureWidth, int textureHeight,
                                                     bool embossed = true);

    /// @brief Gets the underlying font.
    Font& getFont();

    /// @brief Checks if the text renderer is initialized.
    bool isInitialized() const;

    /// @brief Measures the width of a text string in pixels (at scale 1.0).
    float measureTextWidth(const std::string& text) const;

private:
    void setupQuadBuffers();

    Font m_font;
    Shader m_textShader;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    bool m_initialized = false;
};

} // namespace Vestige

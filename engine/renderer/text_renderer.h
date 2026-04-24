// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

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

/// @brief Oblique (italic) shear helpers (Phase 10.9 P6).
///
/// `renderText2DOblique` approximates italic rendering by shearing each
/// glyph-quad vertex horizontally based on its distance from the font
/// baseline. No second font atlas is needed — the existing upright
/// atlas is re-used at vertex-emit time. Extracted here as a free
/// function so the math is unit-testable without a GL context.
///
/// The standard typographic oblique is ~11° (tan ≈ 0.194), matching
/// common italic designs. Applied as:
///
///     x' = x + (baselineY - y) × factor
///
/// In top-left-origin screen space, smaller Y is higher on screen; a
/// vertex above the baseline receives a positive shift (leans right),
/// a descender below the baseline receives a negative shift (leans
/// back), and a vertex exactly at the baseline is identity.
namespace text_oblique
{

/// @brief Default shear strength — ~11° oblique.
inline constexpr float DEFAULT_SHEAR_FACTOR = 0.2f;

/// @brief Applies the italic horizontal shear to a glyph-quad vertex X.
/// @param x         Original X coordinate.
/// @param y         Vertex Y (top-left-origin: smaller Y = higher).
/// @param baselineY Font baseline Y in the same space.
/// @param factor    Shear strength; `0.0` = no shear, `0.2` ≈ 11°.
/// @return Sheared X coordinate.
float applyShear(float x, float y, float baselineY, float factor);

} // namespace text_oblique

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

    /// @brief Renders text with italic-oblique shear (Phase 10.9 P6).
    ///
    /// Same semantics as `renderText2D`, but each glyph quad is
    /// horizontally sheared so the result approximates italic
    /// typography without needing a separate italic font atlas. Used
    /// by the subtitle renderer for narrator captions when
    /// `SubtitleQueue::narratorStyle() == SubtitleNarratorStyle::Italic`.
    /// @param shearFactor Shear strength; default ≈ 11° oblique.
    void renderText2DOblique(const std::string& text, float x, float y, float scale,
                             const glm::vec3& color, int screenWidth, int screenHeight,
                             float shearFactor = text_oblique::DEFAULT_SHEAR_FACTOR);

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

    /// @brief Shared impl behind `renderText2D` and
    ///        `renderText2DOblique`. `shearFactor == 0.0f` produces
    ///        upright text; non-zero applies the italic shear.
    void renderText2DImpl(const std::string& text, float x, float y, float scale,
                          const glm::vec3& color, int screenWidth, int screenHeight,
                          float shearFactor);

    /// @brief Upper bound on glyphs per renderText2D / renderText3D call.
    ///
    /// Strings beyond this limit are truncated rather than fall back to
    /// chunked draws — HUD lines in practice are much shorter, and a bounded
    /// VBO keeps the batched-upload path simple (one ``glNamedBufferSubData``
    /// + one ``glDrawArrays`` per string, instead of N of each per glyph).
    /// 1024 glyphs ≈ 96 KB of vertex data, which is still a cheap upload.
    static constexpr int  MAX_GLYPHS_PER_CALL = 1024;
    static constexpr int  VERTS_PER_GLYPH     = 6;
    static constexpr int  FLOATS_PER_VERT     = 4;
    static constexpr std::size_t VBO_BYTES    =
        sizeof(float) * VERTS_PER_GLYPH * FLOATS_PER_VERT * MAX_GLYPHS_PER_CALL;

    Font m_font;
    Shader m_textShader;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    bool m_initialized = false;
};

} // namespace Vestige

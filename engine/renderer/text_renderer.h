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
#include <vector>

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
    ///
    /// Phase 10.9 Pe1 — when a frame batch is open
    /// (`beginBatch2D` ↔ `endBatch2D`), this call accumulates glyphs into the
    /// shared batch buffer instead of issuing its own upload + draw, so the
    /// HUD pass collapses ~18 per-string draws into one. Outside of a batch
    /// the call retains pre-Pe1 single-string behaviour.
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

    /// @brief Phase 10.9 Pe1 — open a frame-scoped batch. While a batch
    ///        is open, every `renderText2D` / `renderText2DOblique` call
    ///        merely appends glyphs to the shared batch buffer; nothing
    ///        is drawn until `endBatch2D` flushes the whole queue in one
    ///        upload + draw. Nested begin calls are ignored (same screen
    ///        dims) or logged + treated as start-fresh (different dims).
    /// @param screenWidth  Viewport width (must match every queued call).
    /// @param screenHeight Viewport height (must match every queued call).
    void beginBatch2D(int screenWidth, int screenHeight);

    /// @brief Phase 10.9 Pe1 — close the batch and flush every glyph
    ///        queued since `beginBatch2D` in a single upload + draw.
    ///        No-op when no batch is open or the batch is empty.
    void endBatch2D();

    /// @brief True iff a batch is currently open. Tests + the
    ///        UI render pass use this to assert correct begin/end pairing.
    bool isBatching() const { return m_batchActive; }

private:
    void setupQuadBuffers();

    /// @brief Shared impl behind `renderText2D` and
    ///        `renderText2DOblique`. `shearFactor == 0.0f` produces
    ///        upright text; non-zero applies the italic shear.
    void renderText2DImpl(const std::string& text, float x, float y, float scale,
                          const glm::vec3& color, int screenWidth, int screenHeight,
                          float shearFactor);

    /// @brief Upper bound on glyphs per non-batched renderText2D / renderText3D call.
    /// HUD lines in practice are much shorter; truncation keeps a single-string
    /// upload + draw bounded.
    static constexpr int  MAX_GLYPHS_PER_CALL  = 1024;
    /// @brief Phase 10.9 Pe1 — upper bound on glyphs in one batched flush.
    /// 8192 glyphs covers a frame full of HUD labels + subtitles + toasts
    /// with comfortable headroom (~1.3 MB VBO at 7 floats/vert × 6 verts).
    static constexpr int  MAX_GLYPHS_PER_BATCH = 8192;
    static constexpr int  VERTS_PER_GLYPH      = 6;
    /// @brief Per-vertex layout: xy position, uv texcoord, rgb color.
    /// Pe1 added the per-vertex color so a single batch can mix HUD
    /// element colors without splitting the draw.
    static constexpr int  FLOATS_PER_VERT      = 7;
    static constexpr std::size_t VBO_BYTES     =
        sizeof(float) * VERTS_PER_GLYPH * FLOATS_PER_VERT * MAX_GLYPHS_PER_BATCH;

    Font m_font;
    Shader m_textShader;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    bool m_initialized = false;

    // Phase 10.9 Pe1 — batch state. While `m_batchActive`, every queued
    // call's vertex data accumulates in `m_batchVerts` instead of going
    // straight to the GPU; `endBatch2D` flushes the whole queue at once.
    bool                m_batchActive       = false;
    int                 m_batchScreenWidth  = 0;
    int                 m_batchScreenHeight = 0;
    std::vector<float>  m_batchVerts;
    int                 m_batchGlyphCount   = 0;
};

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file text_renderer.cpp
/// @brief TextRenderer implementation.
#include "renderer/text_renderer.h"
#include "core/logger.h"
#include "utils/utf8.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>

namespace Vestige
{

TextRenderer::TextRenderer() = default;

TextRenderer::~TextRenderer()
{
    if (m_vao != 0)
    {
        glDeleteVertexArrays(1, &m_vao);
    }
    if (m_vbo != 0)
    {
        glDeleteBuffers(1, &m_vbo);
    }
}

bool TextRenderer::initialize(const std::string& fontPath, const std::string& shaderPath,
                                int pixelSize)
{
    // Phase 10 Localization L2 — build the default multi-script font stack.
    // Primary: the passed UI face, loaded with Latin + Greek ranges. Fallback:
    // the bundled Frank Ruhl Libre Hebrew serif, loaded with the Hebrew range.
    // Pure-script strings hit a single font (and atlas), so the common HUD
    // path keeps its single bind+draw (see drawRuns / the MRU cache).
    const std::vector<CodepointRange> latinGreek = {
        {0x0020, 0x007E},  // printable ASCII (Latin)
        {0x0370, 0x03FF},  // basic Greek
        {0x1F00, 0x1FFF},  // Greek Extended (polytonic) — loaded if the face has it
    };

    auto primary = std::make_shared<Font>();
    if (!primary->loadFromFile(fontPath, pixelSize, latinGreek))
    {
        Logger::error("TextRenderer: failed to load font: " + fontPath);
        return false;
    }
    m_fontStack.addFont(std::move(primary));

    // Frank Ruhl Libre lives alongside the primary in the fonts dir. A missing
    // Hebrew face is non-fatal: the stack still renders Latin/Greek, and Hebrew
    // codepoints degrade to the primary's '?' fallback rather than failing boot.
    const auto slash = fontPath.find_last_of("/\\");
    const std::string fontsDir =
        (slash == std::string::npos) ? std::string() : fontPath.substr(0, slash + 1);
    const std::string hebrewPath = fontsDir + "frank_ruhl_libre.ttf";

    auto hebrew = std::make_shared<Font>();
    if (hebrew->loadFromFile(hebrewPath, pixelSize, HEBREW_RANGE))
    {
        m_fontStack.addFont(std::move(hebrew));
    }
    else
    {
        Logger::warning("TextRenderer: Hebrew face not loaded (" + hebrewPath
            + "); Hebrew text will render as the fallback glyph.");
    }

    // Load text shader
    std::string vertPath = shaderPath + "/shaders/text.vert.glsl";
    std::string fragPath = shaderPath + "/shaders/text.frag.glsl";
    if (!m_textShader.loadFromFiles(vertPath, fragPath))
    {
        Logger::error("TextRenderer: failed to load text shaders");
        return false;
    }

    setupQuadBuffers();
    m_initialized = true;

    Logger::info("TextRenderer initialized");
    return true;
}

void TextRenderer::setupQuadBuffers()
{
    glCreateVertexArrays(1, &m_vao);
    glCreateBuffers(1, &m_vbo);

    // AUDIT M29: batch the whole string into one upload + one draw. Pe1
    // grew the VBO to hold a frame's worth of HUD strings (8192 glyphs)
    // and the per-vertex layout to xy + uv + rgb (7 floats per vert).
    glNamedBufferStorage(m_vbo, VBO_BYTES, nullptr, GL_DYNAMIC_STORAGE_BIT);

    constexpr GLsizei kStride = FLOATS_PER_VERT * sizeof(float);
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, kStride);

    // Attribute 0: vec4 (x, y, u, v) at binding 0
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_vao, 0, 0);

    // Attribute 1: vec3 (r, g, b) at offset 16 (Pe1).
    glEnableVertexArrayAttrib(m_vao, 1);
    glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float));
    glVertexArrayAttribBinding(m_vao, 1, 0);
}

namespace text_oblique
{

float applyShear(float x, float y, float baselineY, float factor)
{
    // Top-left-origin: smaller Y is higher on screen. A vertex above
    // the baseline (baselineY > y) gains a positive shift (leans
    // right); a descender below (baselineY < y) gets a negative
    // shift (leans back, matching italic descenders).
    return x + (baselineY - y) * factor;
}

} // namespace text_oblique

void TextRenderer::renderText2D(const std::string& text, float x, float y, float scale,
                                  const glm::vec3& color, int screenWidth, int screenHeight)
{
    renderText2DImpl(text, x, y, scale, color, screenWidth, screenHeight, 0.0f);
}

void TextRenderer::renderText2DOblique(const std::string& text, float x, float y, float scale,
                                         const glm::vec3& color, int screenWidth, int screenHeight,
                                         float shearFactor)
{
    renderText2DImpl(text, x, y, scale, color, screenWidth, screenHeight, shearFactor);
}

namespace
{
// Helper: append the vertex data for one glyph quad into `verts` with
// position + UV + rgb (7 floats per vert × 6 verts). Pulled out so the
// batched and immediate paths share one source of truth (CLAUDE.md Rule 3).
void appendGlyphVerts(std::vector<float>& verts,
                      float xpos, float ypos, float w, float h,
                      float u0, float v0, float u1, float v1,
                      float baselineY, float shearFactor,
                      const glm::vec3& color)
{
    const float topY    = ypos;
    const float bottomY = ypos + h;
    const float xTopL    = text_oblique::applyShear(xpos,     topY,    baselineY, shearFactor);
    const float xTopR    = text_oblique::applyShear(xpos + w, topY,    baselineY, shearFactor);
    const float xBottomL = text_oblique::applyShear(xpos,     bottomY, baselineY, shearFactor);
    const float xBottomR = text_oblique::applyShear(xpos + w, bottomY, baselineY, shearFactor);

    verts.insert(verts.end(), {
        xTopL,    topY,    u0, v0, color.r, color.g, color.b,
        xTopR,    topY,    u1, v0, color.r, color.g, color.b,
        xBottomL, bottomY, u0, v1, color.r, color.g, color.b,
        xTopR,    topY,    u1, v0, color.r, color.g, color.b,
        xBottomR, bottomY, u1, v1, color.r, color.g, color.b,
        xBottomL, bottomY, u0, v1, color.r, color.g, color.b,
    });
}
} // namespace

// Find-or-append the vertex buffer for `font` within `runs`. The default
// stack is 2 fonts and runs are mostly single-font, so the linear scan is
// over ≤2 entries — and the MRU back()-match catches the dominant case.
std::vector<float>& TextRenderer::runVertsFor(std::vector<GlyphRun>& runs, Font* font)
{
    if (!runs.empty() && runs.back().font == font)
    {
        return runs.back().verts;
    }
    for (auto& run : runs)
    {
        if (run.font == font)
        {
            return run.verts;
        }
    }
    // No active run for this font. Reclaim a retired slot (font == nullptr,
    // verts already cleared but capacity retained by resetBatchRuns) before
    // allocating a fresh GlyphRun — this is what keeps the batched path free
    // of per-frame heap churn.
    for (auto& run : runs)
    {
        if (run.font == nullptr)
        {
            run.font = font;
            return run.verts;
        }
    }
    runs.push_back(GlyphRun{font, {}});
    return runs.back().verts;
}

void TextRenderer::resetBatchRuns()
{
    for (auto& run : m_batchRuns)
    {
        run.font = nullptr;
        run.verts.clear();  // keeps capacity — no realloc next frame
    }
    m_batchGlyphCount = 0;
}

FontStack::Hit TextRenderer::resolveGlyph(uint32_t codepoint) const
{
    if (m_mruFont && m_mruFont->hasGlyph(codepoint))
    {
        return FontStack::Hit{m_mruFont, &m_mruFont->getGlyph(codepoint)};
    }
    FontStack::Hit hit = m_fontStack.lookup(codepoint);
    if (hit.font)
    {
        m_mruFont = hit.font;
    }
    return hit;
}

void TextRenderer::drawRuns(const std::vector<GlyphRun>& runs)
{
    // Each run uploads to VBO offset 0 then draws, sequentially. A pure-script
    // string (the HUD case) is a single run → one upload + one draw, identical
    // to the pre-L2 cost. A mixed-script string (≥2 runs) reuses offset 0, so
    // the driver inserts an implicit sync between runs — acceptable because
    // mixed-script strings are rare (plaques) and never on the per-frame HUD
    // path. Switch to distinct per-run offsets only if that ever shows up hot.
    m_textShader.setInt("u_glyphAtlas", 0);
    for (const auto& run : runs)
    {
        if (run.font == nullptr || run.verts.empty())
        {
            continue;
        }
        glBindTextureUnit(0, run.font->getAtlasTextureId());
        glNamedBufferSubData(m_vbo, 0,
            static_cast<GLsizeiptr>(run.verts.size() * sizeof(float)),
            run.verts.data());
        glDrawArrays(GL_TRIANGLES, 0,
            static_cast<GLsizei>(run.verts.size() / FLOATS_PER_VERT));
    }
}

void TextRenderer::beginBatch2D(int screenWidth, int screenHeight)
{
    if (!m_initialized) return;

    if (m_batchActive)
    {
        // Re-entry. If dimensions match, treat as no-op (caller's begin
        // pairs with their own end). If they differ, flush the existing
        // batch then open a fresh one — the screen resized mid-frame.
        if (screenWidth == m_batchScreenWidth && screenHeight == m_batchScreenHeight)
        {
            return;
        }
        Logger::warning("TextRenderer: beginBatch2D called with new dims while batch open; flushing.");
        endBatch2D();
    }

    m_batchActive       = true;
    m_batchScreenWidth  = screenWidth;
    m_batchScreenHeight = screenHeight;
    resetBatchRuns();  // retire last frame's runs, keep their buffers
}

void TextRenderer::endBatch2D()
{
    if (!m_initialized || !m_batchActive)
    {
        return;
    }

    // Empty batch — nothing to draw.
    if (m_batchGlyphCount <= 0)
    {
        m_batchActive = false;
        resetBatchRuns();
        return;
    }

    glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(m_batchScreenWidth),
                                   static_cast<float>(m_batchScreenHeight), 0.0f);

    m_textShader.use();
    m_textShader.setMat4("u_projection", ortho);
    m_textShader.setMat4("u_model", glm::mat4(1.0f));

    // Save GL state
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLint prevBlendSrc, prevBlendDst;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);
    drawRuns(m_batchRuns);  // L2 — one bind+draw per font (≤2 for the default stack)

    // Restore GL state
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(static_cast<GLenum>(prevBlendSrc), static_cast<GLenum>(prevBlendDst));

    m_batchActive = false;
    resetBatchRuns();
}

void TextRenderer::renderText2DImpl(const std::string& text, float x, float y, float scale,
                                      const glm::vec3& color, int screenWidth, int screenHeight,
                                      float shearFactor)
{
    if (!m_initialized || text.empty())
    {
        return;
    }

    // Pe1 — when a frame batch is open, accumulate into the shared buffer
    // and skip the per-call upload + draw. Caller is `UISystem::renderUI`
    // which wraps the HUD pass in begin/endBatch2D.
    const bool batched = m_batchActive
                      && screenWidth  == m_batchScreenWidth
                      && screenHeight == m_batchScreenHeight;

    float cursorX = x;
    float cursorY = y + m_fontStack.fontAt(0)->getAscender() * scale;

    // L2 — glyphs are grouped by source font (per-font draw split). Batched
    // calls append to the shared m_batchRuns; immediate calls use a local set.
    std::vector<GlyphRun> localRuns;
    std::vector<GlyphRun>& runs = batched ? m_batchRuns : localRuns;

    const int perCallCap = batched
        ? std::max(0, MAX_GLYPHS_PER_BATCH - m_batchGlyphCount)
        : MAX_GLYPHS_PER_CALL;

    m_mruFont = nullptr;  // reset MRU per string for deterministic lookup count
    int emitted = 0;
    for (size_t i = 0; i < text.size();)
    {
        if (emitted >= perCallCap)
        {
            break;
        }
        const auto [cp, n] = utf8::decodeAt(text, i);
        i += static_cast<size_t>(n);
        const FontStack::Hit hit = resolveGlyph(cp);
        const GlyphInfo& glyph = *hit.glyph;

        float xpos = cursorX + static_cast<float>(glyph.bearing.x) * scale;
        float ypos = cursorY - static_cast<float>(glyph.bearing.y) * scale;
        float w = static_cast<float>(glyph.size.x) * scale;
        float h = static_cast<float>(glyph.size.y) * scale;

        float u0 = glyph.atlasOffset.x;
        float v0 = glyph.atlasOffset.y;
        float u1 = u0 + glyph.atlasSize.x;
        float v1 = v0 + glyph.atlasSize.y;

        appendGlyphVerts(runVertsFor(runs, hit.font), xpos, ypos, w, h,
                         u0, v0, u1, v1, cursorY, shearFactor, color);
        ++emitted;
        cursorX += static_cast<float>(glyph.advance) / 64.0f * scale;
    }

    if (batched)
    {
        m_batchGlyphCount += emitted;
        return;
    }

    // Immediate (non-batched) path — single string, one draw per font.
    if (emitted == 0)
    {
        return;
    }

    glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(screenWidth),
                                   static_cast<float>(screenHeight), 0.0f);

    m_textShader.use();
    m_textShader.setMat4("u_projection", ortho);
    m_textShader.setMat4("u_model", glm::mat4(1.0f));

    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLint prevBlendSrc, prevBlendDst;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);
    drawRuns(localRuns);

    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(static_cast<GLenum>(prevBlendSrc), static_cast<GLenum>(prevBlendDst));
}

void TextRenderer::renderText3D(const std::string& text, const glm::mat4& modelMatrix,
                                  float scale, const glm::vec3& color,
                                  const glm::mat4& view, const glm::mat4& projection)
{
    if (!m_initialized || text.empty())
    {
        return;
    }

    // Save GL state
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepthWrite;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthWrite);

    m_textShader.use();
    m_textShader.setMat4("u_projection", projection * view);
    m_textShader.setMat4("u_model", modelMatrix);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);  // Don't write depth — text shouldn't occlude geometry

    glBindVertexArray(m_vao);

    float cursorX = 0.0f;
    float pixelScale = scale / static_cast<float>(m_fontStack.fontAt(0)->getPixelSize());

    // AUDIT M29 / L2: glyphs grouped by font, one upload+draw per font.
    std::vector<GlyphRun> runs;
    m_mruFont = nullptr;
    int emitted = 0;
    for (size_t i = 0; i < text.size();)
    {
        if (emitted >= MAX_GLYPHS_PER_CALL)
        {
            break;
        }
        const auto [cp, n] = utf8::decodeAt(text, i);
        i += static_cast<size_t>(n);
        const FontStack::Hit hit = resolveGlyph(cp);
        const GlyphInfo& glyph = *hit.glyph;

        float xpos = cursorX + static_cast<float>(glyph.bearing.x) * pixelScale;
        float ypos = -static_cast<float>(glyph.bearing.y) * pixelScale;
        float w = static_cast<float>(glyph.size.x) * pixelScale;
        float h = static_cast<float>(glyph.size.y) * pixelScale;

        float u0 = glyph.atlasOffset.x;
        float v0 = glyph.atlasOffset.y;
        float u1 = u0 + glyph.atlasSize.x;
        float v1 = v0 + glyph.atlasSize.y;

        // 3D path — no italic shear, baseline = ypos so applyShear is identity.
        appendGlyphVerts(runVertsFor(runs, hit.font), xpos, ypos, w, h,
                         u0, v0, u1, v1, ypos, 0.0f, color);
        ++emitted;

        cursorX += static_cast<float>(glyph.advance) / 64.0f * pixelScale;
    }

    drawRuns(runs);

    // Restore GL state
    glDepthMask(prevDepthWrite ? GL_TRUE : GL_FALSE);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
}

std::shared_ptr<Texture> TextRenderer::generateTextHeightMap(const std::string& text,
                                                                int textureWidth, int textureHeight,
                                                                bool embossed)
{
    if (!m_initialized || text.empty())
    {
        return nullptr;
    }

    // Create an offscreen FBO with a single-channel texture (DSA)
    GLuint fbo = 0;
    GLuint texId = 0;

    glCreateFramebuffers(1, &fbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &texId);

    glTextureStorage2D(texId, 1, GL_R8, textureWidth, textureHeight);
    glTextureParameteri(texId, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(texId, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texId, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texId, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, texId, 0);

    // Check completeness
    if (glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("TextRenderer: height map FBO not complete");
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texId);
        return nullptr;
    }

    // Bind FBO for rendering (need bind for draw operations)
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Clear to background (embossed: black=low, engraved: white=high)
    glViewport(0, 0, textureWidth, textureHeight);
    if (embossed)
    {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);

    // Set text color (embossed: white=raised, engraved: black=cut)
    glm::vec3 textColor = embossed ? glm::vec3(1.0f) : glm::vec3(0.0f);

    // Use ortho projection matching texture dimensions
    glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(textureWidth),
                                   static_cast<float>(textureHeight), 0.0f);

    // Calculate scale to fit text within texture
    float textWidth = measureTextWidth(text);
    float margin = static_cast<float>(textureWidth) * 0.1f;
    float availableWidth = static_cast<float>(textureWidth) - 2.0f * margin;
    float scale = availableWidth / textWidth;
    float textHeightVal = m_fontStack.fontAt(0)->getLineHeight() * scale;
    float yOffset = (static_cast<float>(textureHeight) - textHeightVal) * 0.5f;

    m_textShader.use();
    m_textShader.setMat4("u_projection", ortho);
    m_textShader.setMat4("u_model", glm::mat4(1.0f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);

    float cursorX = margin;
    float cursorY = yOffset + m_fontStack.fontAt(0)->getAscender() * scale;

    // Pe1 / L2 — glyphs grouped by font, one upload+draw per font. Same
    // per-vertex colour layout as the 2D / 3D paths so the shader matches.
    std::vector<GlyphRun> runs;
    m_mruFont = nullptr;
    for (size_t i = 0; i < text.size();)
    {
        const auto [cp, n] = utf8::decodeAt(text, i);
        i += static_cast<size_t>(n);
        const FontStack::Hit hit = resolveGlyph(cp);
        const GlyphInfo& glyph = *hit.glyph;

        float xpos = cursorX + static_cast<float>(glyph.bearing.x) * scale;
        float ypos = cursorY - static_cast<float>(glyph.bearing.y) * scale;
        float w = static_cast<float>(glyph.size.x) * scale;
        float h = static_cast<float>(glyph.size.y) * scale;

        float u0 = glyph.atlasOffset.x;
        float v0 = glyph.atlasOffset.y;
        float u1 = u0 + glyph.atlasSize.x;
        float v1 = v0 + glyph.atlasSize.y;

        appendGlyphVerts(runVertsFor(runs, hit.font), xpos, ypos, w, h,
                         u0, v0, u1, v1, cursorY, 0.0f, textColor);

        cursorX += static_cast<float>(glyph.advance) / 64.0f * scale;
    }

    drawRuns(runs);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Unbind FBO and clean up
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    // Wrap the GL texture in a Texture object
    // Note: We can't directly assign the texture ID to a Texture since it manages ownership.
    // Instead, read the pixels back and load into a Texture.
    std::vector<unsigned char> pixels(static_cast<size_t>(textureWidth) * static_cast<size_t>(textureHeight));
    glGetTextureImage(texId, 0, GL_RED, GL_UNSIGNED_BYTE,
                      static_cast<GLsizei>(pixels.size()), pixels.data());
    glDeleteTextures(1, &texId);

    auto texture = std::make_shared<Texture>();
    // The data is already in OpenGL orientation (bottom-up), so loadFromMemory will flip it
    // which means we need to pass it as top-down. Since the FBO renders top-down and GL
    // stores bottom-up, the glGetTexImage gives bottom-up data. loadFromMemory flips again,
    // which gives bottom-up (correct for GL). So we pass it directly.
    texture->loadFromMemory(pixels.data(), textureWidth, textureHeight, 1, true);

    Logger::debug("Text height map generated: \"" + text + "\" ("
        + std::to_string(textureWidth) + "x" + std::to_string(textureHeight)
        + (embossed ? ", embossed" : ", engraved") + ")");

    return texture;
}

Font& TextRenderer::getFont()
{
    return *m_fontStack.fontAt(0);
}

bool TextRenderer::isInitialized() const
{
    return m_initialized;
}

float TextRenderer::measureTextWidth(const std::string& text) const
{
    // Not initialized → empty stack; resolveGlyph would return a null Hit.
    // Match the pre-L2 graceful behaviour (zero width) rather than deref null.
    if (!m_initialized)
    {
        return 0.0f;
    }

    float width = 0.0f;
    m_mruFont = nullptr;
    for (size_t i = 0; i < text.size();)
    {
        const auto [cp, n] = utf8::decodeAt(text, i);
        i += static_cast<size_t>(n);
        const FontStack::Hit hit = resolveGlyph(cp);
        width += static_cast<float>(hit.glyph->advance) / 64.0f;
    }
    return width;
}

} // namespace Vestige

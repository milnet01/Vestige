// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file text_renderer.cpp
/// @brief TextRenderer implementation.
#include "renderer/text_renderer.h"
#include "core/logger.h"

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
    // Load font
    if (!m_font.loadFromFile(fontPath, pixelSize))
    {
        Logger::error("TextRenderer: failed to load font: " + fontPath);
        return false;
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
    m_batchVerts.clear();
    m_batchGlyphCount   = 0;
}

void TextRenderer::endBatch2D()
{
    if (!m_initialized || !m_batchActive)
    {
        return;
    }

    // Empty batch — nothing to draw.
    if (m_batchGlyphCount <= 0 || m_batchVerts.empty())
    {
        m_batchActive = false;
        m_batchVerts.clear();
        m_batchGlyphCount = 0;
        return;
    }

    glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(m_batchScreenWidth),
                                   static_cast<float>(m_batchScreenHeight), 0.0f);

    m_textShader.use();
    m_textShader.setMat4("u_projection", ortho);
    m_textShader.setMat4("u_model", glm::mat4(1.0f));
    m_textShader.setInt("u_glyphAtlas", 0);

    glBindTextureUnit(0, m_font.getAtlasTextureId());

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
    glNamedBufferSubData(m_vbo, 0,
        static_cast<GLsizeiptr>(m_batchVerts.size() * sizeof(float)),
        m_batchVerts.data());
    glDrawArrays(GL_TRIANGLES, 0,
        static_cast<GLsizei>(m_batchGlyphCount * VERTS_PER_GLYPH));

    // Restore GL state
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(static_cast<GLenum>(prevBlendSrc), static_cast<GLenum>(prevBlendDst));

    m_batchActive = false;
    m_batchVerts.clear();
    m_batchGlyphCount = 0;
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
    float cursorY = y + m_font.getAscender() * scale;

    std::vector<float>* target = batched ? &m_batchVerts : nullptr;
    std::vector<float> localVerts;
    if (!target)
    {
        const std::size_t glyphCap =
            std::min<std::size_t>(text.size(), MAX_GLYPHS_PER_CALL);
        localVerts.reserve(glyphCap * VERTS_PER_GLYPH * FLOATS_PER_VERT);
        target = &localVerts;
    }

    const int perCallCap = batched
        ? std::max(0, MAX_GLYPHS_PER_BATCH - m_batchGlyphCount)
        : MAX_GLYPHS_PER_CALL;

    int emitted = 0;
    for (char c : text)
    {
        if (emitted >= perCallCap)
        {
            break;
        }
        const GlyphInfo& glyph = m_font.getGlyph(c);

        float xpos = cursorX + static_cast<float>(glyph.bearing.x) * scale;
        float ypos = cursorY - static_cast<float>(glyph.bearing.y) * scale;
        float w = static_cast<float>(glyph.size.x) * scale;
        float h = static_cast<float>(glyph.size.y) * scale;

        float u0 = glyph.atlasOffset.x;
        float v0 = glyph.atlasOffset.y;
        float u1 = u0 + glyph.atlasSize.x;
        float v1 = v0 + glyph.atlasSize.y;

        appendGlyphVerts(*target, xpos, ypos, w, h, u0, v0, u1, v1,
                         cursorY, shearFactor, color);
        ++emitted;
        cursorX += static_cast<float>(glyph.advance) / 64.0f * scale;
    }

    if (batched)
    {
        m_batchGlyphCount += emitted;
        return;
    }

    // Immediate (non-batched) path — single string, single draw.
    if (emitted == 0)
    {
        return;
    }

    glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(screenWidth),
                                   static_cast<float>(screenHeight), 0.0f);

    m_textShader.use();
    m_textShader.setMat4("u_projection", ortho);
    m_textShader.setMat4("u_model", glm::mat4(1.0f));
    m_textShader.setInt("u_glyphAtlas", 0);

    glBindTextureUnit(0, m_font.getAtlasTextureId());

    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLint prevBlendSrc, prevBlendDst;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);
    glNamedBufferSubData(m_vbo, 0,
        static_cast<GLsizeiptr>(localVerts.size() * sizeof(float)),
        localVerts.data());
    glDrawArrays(GL_TRIANGLES, 0,
        static_cast<GLsizei>(emitted * VERTS_PER_GLYPH));

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
    m_textShader.setInt("u_glyphAtlas", 0);

    glBindTextureUnit(0, m_font.getAtlasTextureId());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);  // Don't write depth — text shouldn't occlude geometry

    glBindVertexArray(m_vao);

    float cursorX = 0.0f;
    float pixelScale = scale / static_cast<float>(m_font.getPixelSize());

    // AUDIT M29: batched upload — one upload + draw per string.
    std::vector<float> verts;
    const std::size_t glyphCap =
        std::min<std::size_t>(text.size(), MAX_GLYPHS_PER_CALL);
    verts.reserve(glyphCap * VERTS_PER_GLYPH * FLOATS_PER_VERT);

    int emitted = 0;
    for (char c : text)
    {
        if (emitted >= MAX_GLYPHS_PER_CALL)
        {
            break;
        }
        const GlyphInfo& glyph = m_font.getGlyph(c);

        float xpos = cursorX + static_cast<float>(glyph.bearing.x) * pixelScale;
        float ypos = -static_cast<float>(glyph.bearing.y) * pixelScale;
        float w = static_cast<float>(glyph.size.x) * pixelScale;
        float h = static_cast<float>(glyph.size.y) * pixelScale;

        float u0 = glyph.atlasOffset.x;
        float v0 = glyph.atlasOffset.y;
        float u1 = u0 + glyph.atlasSize.x;
        float v1 = v0 + glyph.atlasSize.y;

        // 3D path — no italic shear, baseline = ypos so applyShear is identity.
        appendGlyphVerts(verts, xpos, ypos, w, h, u0, v0, u1, v1,
                         ypos, 0.0f, color);
        ++emitted;

        cursorX += static_cast<float>(glyph.advance) / 64.0f * pixelScale;
    }

    if (emitted > 0)
    {
        glNamedBufferSubData(m_vbo, 0,
            static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
            verts.data());
        glDrawArrays(GL_TRIANGLES, 0,
            static_cast<GLsizei>(emitted * VERTS_PER_GLYPH));
    }

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
    float textHeightVal = m_font.getLineHeight() * scale;
    float yOffset = (static_cast<float>(textureHeight) - textHeightVal) * 0.5f;

    m_textShader.use();
    m_textShader.setMat4("u_projection", ortho);
    m_textShader.setMat4("u_model", glm::mat4(1.0f));
    m_textShader.setInt("u_glyphAtlas", 0);

    glBindTextureUnit(0, m_font.getAtlasTextureId());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);

    float cursorX = margin;
    float cursorY = yOffset + m_font.getAscender() * scale;

    // Pe1 — one upload + draw for the whole string. Same per-vertex
    // colour layout as the 2D / 3D paths so the shader matches.
    std::vector<float> verts;
    verts.reserve(text.size() * VERTS_PER_GLYPH * FLOATS_PER_VERT);

    for (char c : text)
    {
        const GlyphInfo& glyph = m_font.getGlyph(c);

        float xpos = cursorX + static_cast<float>(glyph.bearing.x) * scale;
        float ypos = cursorY - static_cast<float>(glyph.bearing.y) * scale;
        float w = static_cast<float>(glyph.size.x) * scale;
        float h = static_cast<float>(glyph.size.y) * scale;

        float u0 = glyph.atlasOffset.x;
        float v0 = glyph.atlasOffset.y;
        float u1 = u0 + glyph.atlasSize.x;
        float v1 = v0 + glyph.atlasSize.y;

        appendGlyphVerts(verts, xpos, ypos, w, h, u0, v0, u1, v1,
                         cursorY, 0.0f, textColor);

        cursorX += static_cast<float>(glyph.advance) / 64.0f * scale;
    }

    if (!verts.empty())
    {
        glNamedBufferSubData(m_vbo, 0,
            static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
            verts.data());
        glDrawArrays(GL_TRIANGLES, 0,
            static_cast<GLsizei>(verts.size() / FLOATS_PER_VERT));
    }

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
    return m_font;
}

bool TextRenderer::isInitialized() const
{
    return m_initialized;
}

float TextRenderer::measureTextWidth(const std::string& text) const
{
    float width = 0.0f;
    for (char c : text)
    {
        const GlyphInfo& glyph = m_font.getGlyph(c);
        width += static_cast<float>(glyph.advance) / 64.0f;
    }
    return width;
}

} // namespace Vestige

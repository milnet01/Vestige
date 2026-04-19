// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file text_renderer.cpp
/// @brief TextRenderer implementation.
#include "renderer/text_renderer.h"
#include "renderer/framebuffer.h"
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

    // AUDIT M29: batch the whole string into one upload + one draw. Size the
    // VBO up-front to the per-call glyph ceiling. Strings longer than this
    // are truncated (see MAX_GLYPHS_PER_CALL); HUD lines in practice are
    // well under 256 chars.
    glNamedBufferStorage(m_vbo, VBO_BYTES, nullptr, GL_DYNAMIC_STORAGE_BIT);

    // Bind VBO to VAO binding point 0
    glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, 4 * sizeof(float));

    // Attribute 0: vec4 (x, y, u, v) at binding 0
    glEnableVertexArrayAttrib(m_vao, 0);
    glVertexArrayAttribFormat(m_vao, 0, 4, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(m_vao, 0, 0);
}

void TextRenderer::renderText2D(const std::string& text, float x, float y, float scale,
                                  const glm::vec3& color, int screenWidth, int screenHeight)
{
    if (!m_initialized || text.empty())
    {
        return;
    }

    glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(screenWidth),
                                   static_cast<float>(screenHeight), 0.0f);  // Top-left origin

    m_textShader.use();
    m_textShader.setMat4("u_projection", ortho);
    m_textShader.setMat4("u_model", glm::mat4(1.0f));
    m_textShader.setVec3("u_textColor", color);
    m_textShader.setInt("u_glyphAtlas", 0);

    glBindTextureUnit(0, m_font.getAtlasTextureId());

    // Save GL state that text rendering modifies
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLint prevBlendSrc, prevBlendDst;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);

    float cursorX = x;
    float cursorY = y + m_font.getAscender() * scale;

    // AUDIT M29: build one vertex array for the whole string, upload + draw
    // once. Previously this loop did one glNamedBufferSubData + glDrawArrays
    // per glyph, causing dozens of calls per HUD line.
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

        float xpos = cursorX + static_cast<float>(glyph.bearing.x) * scale;
        float ypos = cursorY - static_cast<float>(glyph.bearing.y) * scale;
        float w = static_cast<float>(glyph.size.x) * scale;
        float h = static_cast<float>(glyph.size.y) * scale;

        float u0 = glyph.atlasOffset.x;
        float v0 = glyph.atlasOffset.y;
        float u1 = u0 + glyph.atlasSize.x;
        float v1 = v0 + glyph.atlasSize.y;

        verts.insert(verts.end(), {
            xpos,     ypos,     u0, v0,
            xpos + w, ypos,     u1, v0,
            xpos,     ypos + h, u0, v1,
            xpos + w, ypos,     u1, v0,
            xpos + w, ypos + h, u1, v1,
            xpos,     ypos + h, u0, v1,
        });
        ++emitted;

        cursorX += static_cast<float>(glyph.advance) / 64.0f * scale;
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
    m_textShader.setVec3("u_textColor", color);
    m_textShader.setInt("u_glyphAtlas", 0);

    glBindTextureUnit(0, m_font.getAtlasTextureId());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);  // Don't write depth — text shouldn't occlude geometry

    glBindVertexArray(m_vao);

    float cursorX = 0.0f;
    float pixelScale = scale / static_cast<float>(m_font.getPixelSize());

    // AUDIT M29: batched upload — see renderText2D for rationale.
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

        verts.insert(verts.end(), {
            xpos,     ypos,     u0, v0,
            xpos + w, ypos,     u1, v0,
            xpos,     ypos + h, u0, v1,
            xpos + w, ypos,     u1, v0,
            xpos + w, ypos + h, u1, v1,
            xpos,     ypos + h, u0, v1,
        });
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
    m_textShader.setVec3("u_textColor", textColor);
    m_textShader.setInt("u_glyphAtlas", 0);

    glBindTextureUnit(0, m_font.getAtlasTextureId());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);

    float cursorX = margin;
    float cursorY = yOffset + m_font.getAscender() * scale;

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

        float vertices[6][4] =
        {
            { xpos,     ypos,     u0, v0 },
            { xpos + w, ypos,     u1, v0 },
            { xpos,     ypos + h, u0, v1 },

            { xpos + w, ypos,     u1, v0 },
            { xpos + w, ypos + h, u1, v1 },
            { xpos,     ypos + h, u0, v1 },
        };

        glNamedBufferSubData(m_vbo, 0, sizeof(vertices), vertices);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        cursorX += static_cast<float>(glyph.advance) / 64.0f * scale;
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

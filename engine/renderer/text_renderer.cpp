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
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // Dynamic buffer for 6 vertices * 4 floats (x, y, u, v) per glyph quad
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
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

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_font.getAtlasTextureId());

    // Save GL state that text rendering modifies
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLint prevBlendSrc, prevBlendDst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);

    float cursorX = x;
    float cursorY = y + m_font.getAscender() * scale;

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

        // Two triangles for the glyph quad
        float vertices[6][4] =
        {
            { xpos,     ypos,     u0, v0 },
            { xpos + w, ypos,     u1, v0 },
            { xpos,     ypos + h, u0, v1 },

            { xpos + w, ypos,     u1, v0 },
            { xpos + w, ypos + h, u1, v1 },
            { xpos,     ypos + h, u0, v1 },
        };

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        cursorX += static_cast<float>(glyph.advance) / 64.0f * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

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

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_font.getAtlasTextureId());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);  // Don't write depth — text shouldn't occlude geometry

    glBindVertexArray(m_vao);

    float cursorX = 0.0f;
    float pixelScale = scale / static_cast<float>(m_font.getPixelSize());

    for (char c : text)
    {
        const GlyphInfo& glyph = m_font.getGlyph(c);

        float xpos = cursorX + static_cast<float>(glyph.bearing.x) * pixelScale;
        float ypos = -static_cast<float>(glyph.bearing.y) * pixelScale;
        float w = static_cast<float>(glyph.size.x) * pixelScale;
        float h = static_cast<float>(glyph.size.y) * pixelScale;

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

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        cursorX += static_cast<float>(glyph.advance) / 64.0f * pixelScale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

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

    // Create an offscreen FBO with a single-channel texture
    GLuint fbo = 0;
    GLuint texId = 0;

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &texId);

    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, textureWidth, textureHeight, 0,
                 GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texId, 0);

    // Check completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::error("TextRenderer: height map FBO not complete");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texId);
        return nullptr;
    }

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
    float textHeight = m_font.getLineHeight() * scale;
    float yOffset = (static_cast<float>(textureHeight) - textHeight) * 0.5f;

    m_textShader.use();
    m_textShader.setMat4("u_projection", ortho);
    m_textShader.setMat4("u_model", glm::mat4(1.0f));
    m_textShader.setVec3("u_textColor", textColor);
    m_textShader.setInt("u_glyphAtlas", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_font.getAtlasTextureId());

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

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        cursorX += static_cast<float>(glyph.advance) / 64.0f * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Unbind FBO and clean up
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    // Wrap the GL texture in a Texture object
    // Note: We can't directly assign the texture ID to a Texture since it manages ownership.
    // Instead, read the pixels back and load into a Texture.
    glBindTexture(GL_TEXTURE_2D, texId);
    std::vector<unsigned char> pixels(static_cast<size_t>(textureWidth) * static_cast<size_t>(textureHeight));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
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

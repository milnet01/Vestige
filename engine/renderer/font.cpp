/// @file font.cpp
/// @brief Font implementation using FreeType for glyph rasterization.
#include "renderer/font.h"
#include "core/logger.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glad/gl.h>

#include <algorithm>
#include <vector>

namespace Vestige
{

Font::Font() = default;

Font::~Font()
{
    if (m_atlasTexture != 0)
    {
        glDeleteTextures(1, &m_atlasTexture);
    }
}

Font::Font(Font&& other) noexcept
    : m_glyphs(std::move(other.m_glyphs))
    , m_atlasTexture(other.m_atlasTexture)
    , m_atlasWidth(other.m_atlasWidth)
    , m_atlasHeight(other.m_atlasHeight)
    , m_lineHeight(other.m_lineHeight)
    , m_ascender(other.m_ascender)
    , m_descender(other.m_descender)
    , m_pixelSize(other.m_pixelSize)
    , m_fallbackGlyph(other.m_fallbackGlyph)
{
    other.m_atlasTexture = 0;
    other.m_atlasWidth = 0;
    other.m_atlasHeight = 0;
    other.m_lineHeight = 0.0f;
    other.m_ascender = 0.0f;
    other.m_descender = 0.0f;
    other.m_pixelSize = 0;
    other.m_fallbackGlyph = GlyphInfo{};
}

Font& Font::operator=(Font&& other) noexcept
{
    if (this != &other)
    {
        // Destroy own GPU resources
        if (m_atlasTexture != 0)
        {
            glDeleteTextures(1, &m_atlasTexture);
        }

        // Transfer all state
        m_glyphs = std::move(other.m_glyphs);
        m_atlasTexture = other.m_atlasTexture;
        m_atlasWidth = other.m_atlasWidth;
        m_atlasHeight = other.m_atlasHeight;
        m_lineHeight = other.m_lineHeight;
        m_ascender = other.m_ascender;
        m_descender = other.m_descender;
        m_pixelSize = other.m_pixelSize;
        m_fallbackGlyph = other.m_fallbackGlyph;

        // Zero the source
        other.m_atlasTexture = 0;
        other.m_atlasWidth = 0;
        other.m_atlasHeight = 0;
        other.m_lineHeight = 0.0f;
        other.m_ascender = 0.0f;
        other.m_descender = 0.0f;
        other.m_pixelSize = 0;
        other.m_fallbackGlyph = GlyphInfo{};
    }
    return *this;
}

bool Font::loadFromFile(const std::string& filePath, int pixelSize)
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
    {
        Logger::error("Failed to initialize FreeType");
        return false;
    }

    FT_Face face;
    if (FT_New_Face(ft, filePath.c_str(), 0, &face))
    {
        Logger::error("Failed to load font: " + filePath);
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));
    m_pixelSize = pixelSize;

    // Store font metrics
    m_ascender = static_cast<float>(face->size->metrics.ascender) / 64.0f;
    m_descender = static_cast<float>(face->size->metrics.descender) / 64.0f;
    m_lineHeight = static_cast<float>(face->size->metrics.height) / 64.0f;

    // First pass: determine atlas dimensions
    // Render ASCII 32-126 (printable characters)
    int totalWidth = 0;
    int maxHeight = 0;

    struct GlyphBitmap
    {
        char codepoint;
        int width;
        int height;
        int bearingX;
        int bearingY;
        int advance;
        std::vector<unsigned char> buffer;
    };

    std::vector<GlyphBitmap> bitmaps;

    for (char c = 32; c < 127; c++)
    {
        if (FT_Load_Char(face, static_cast<FT_ULong>(c), FT_LOAD_RENDER))
        {
            Logger::warning("Failed to load glyph: '" + std::string(1, c) + "'");
            continue;
        }

        GlyphBitmap bmp;
        bmp.codepoint = c;
        bmp.width = static_cast<int>(face->glyph->bitmap.width);
        bmp.height = static_cast<int>(face->glyph->bitmap.rows);
        bmp.bearingX = face->glyph->bitmap_left;
        bmp.bearingY = face->glyph->bitmap_top;
        bmp.advance = static_cast<int>(face->glyph->advance.x);

        // Copy bitmap data
        size_t bufSize = static_cast<size_t>(bmp.width) * static_cast<size_t>(bmp.height);
        bmp.buffer.resize(bufSize);
        if (bufSize > 0 && face->glyph->bitmap.buffer)
        {
            std::copy(face->glyph->bitmap.buffer,
                      face->glyph->bitmap.buffer + bufSize,
                      bmp.buffer.begin());
        }

        totalWidth += bmp.width + 1;  // +1 for padding
        maxHeight = std::max(maxHeight, bmp.height);

        bitmaps.push_back(std::move(bmp));
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    if (bitmaps.empty())
    {
        Logger::error("No glyphs loaded from font: " + filePath);
        return false;
    }

    // Shelf-packing: use a single row for simplicity (works well for ASCII)
    // Atlas width is clamped to max 2048, and rows wrap if needed
    int atlasWidth = std::min(totalWidth, 2048);
    int atlasHeight = 0;

    // Calculate needed height with row wrapping
    int curX = 0;
    int curY = 0;
    int rowHeight = maxHeight + 1;

    for (const auto& bmp : bitmaps)
    {
        if (curX + bmp.width + 1 > atlasWidth)
        {
            curX = 0;
            curY += rowHeight;
        }
        curX += bmp.width + 1;
    }
    atlasHeight = curY + rowHeight;

    // Power-of-two is not required but can help
    m_atlasWidth = atlasWidth;
    m_atlasHeight = atlasHeight;

    // Create atlas pixel buffer (single-channel)
    std::vector<unsigned char> atlasData(
        static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight), 0);

    // Second pass: blit glyphs into atlas and record GlyphInfo
    curX = 0;
    curY = 0;

    for (const auto& bmp : bitmaps)
    {
        if (curX + bmp.width + 1 > atlasWidth)
        {
            curX = 0;
            curY += rowHeight;
        }

        // Blit glyph bitmap into atlas
        for (int y = 0; y < bmp.height; y++)
        {
            for (int x = 0; x < bmp.width; x++)
            {
                int atlasIdx = (curY + y) * atlasWidth + (curX + x);
                int bmpIdx = y * bmp.width + x;
                if (atlasIdx >= 0 && static_cast<size_t>(atlasIdx) < atlasData.size()
                    && bmpIdx >= 0 && static_cast<size_t>(bmpIdx) < bmp.buffer.size())
                {
                    atlasData[static_cast<size_t>(atlasIdx)] = bmp.buffer[static_cast<size_t>(bmpIdx)];
                }
            }
        }

        GlyphInfo info;
        info.atlasOffset = glm::vec2(
            static_cast<float>(curX) / static_cast<float>(atlasWidth),
            static_cast<float>(curY) / static_cast<float>(atlasHeight));
        info.atlasSize = glm::vec2(
            static_cast<float>(bmp.width) / static_cast<float>(atlasWidth),
            static_cast<float>(bmp.height) / static_cast<float>(atlasHeight));
        info.size = glm::ivec2(bmp.width, bmp.height);
        info.bearing = glm::ivec2(bmp.bearingX, bmp.bearingY);
        info.advance = bmp.advance;

        m_glyphs[bmp.codepoint] = info;

        curX += bmp.width + 1;
    }

    // Store '?' as fallback
    auto qIt = m_glyphs.find('?');
    if (qIt != m_glyphs.end())
    {
        m_fallbackGlyph = qIt->second;
    }

    // Upload atlas to GPU with DSA (single-channel GL_R8, immutable storage)
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // Byte-aligned rows

    glCreateTextures(GL_TEXTURE_2D, 1, &m_atlasTexture);

    glTextureParameteri(m_atlasTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_atlasTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_atlasTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_atlasTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTextureStorage2D(m_atlasTexture, 1, GL_R8, atlasWidth, atlasHeight);
    glTextureSubImage2D(m_atlasTexture, 0, 0, 0, atlasWidth, atlasHeight,
                        GL_RED, GL_UNSIGNED_BYTE, atlasData.data());

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // Restore default

    Logger::info("Font loaded: " + filePath + " (" + std::to_string(pixelSize)
        + "px, " + std::to_string(m_glyphs.size()) + " glyphs, atlas "
        + std::to_string(atlasWidth) + "x" + std::to_string(atlasHeight) + ")");

    return true;
}

const GlyphInfo& Font::getGlyph(char codepoint) const
{
    auto it = m_glyphs.find(codepoint);
    if (it != m_glyphs.end())
    {
        return it->second;
    }
    return m_fallbackGlyph;
}

GLuint Font::getAtlasTextureId() const
{
    return m_atlasTexture;
}

int Font::getAtlasWidth() const
{
    return m_atlasWidth;
}

int Font::getAtlasHeight() const
{
    return m_atlasHeight;
}

float Font::getLineHeight() const
{
    return m_lineHeight;
}

float Font::getAscender() const
{
    return m_ascender;
}

float Font::getDescender() const
{
    return m_descender;
}

int Font::getPixelSize() const
{
    return m_pixelSize;
}

bool Font::isLoaded() const
{
    return m_atlasTexture != 0;
}

size_t Font::getGlyphCount() const
{
    return m_glyphs.size();
}

} // namespace Vestige

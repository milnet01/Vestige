// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file font.h
/// @brief TrueType font loading and glyph atlas generation via FreeType.
#pragma once

#include "renderer/texture.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace Vestige
{

/// @brief Information about a single glyph in the atlas.
struct GlyphInfo
{
    glm::vec2 atlasOffset = glm::vec2(0.0f);  // UV offset in atlas (normalized 0-1)
    glm::vec2 atlasSize = glm::vec2(0.0f);    // UV size in atlas (normalized 0-1)
    glm::ivec2 size = glm::ivec2(0);          // Glyph bitmap size in pixels
    glm::ivec2 bearing = glm::ivec2(0);       // Offset from baseline to top-left
    int advance = 0;                           // Horizontal advance (in 1/64 pixels)
};

/// @brief Loads a TrueType font and builds a glyph atlas texture.
class Font
{
public:
    Font();
    ~Font();

    // Non-copyable
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;

    /// @brief Loads a font from a .ttf file and builds the glyph atlas.
    /// @param filePath Path to the .ttf font file.
    /// @param pixelSize Font size in pixels.
    /// @return True if loading succeeded.
    bool loadFromFile(const std::string& filePath, int pixelSize = 48);

    /// @brief Gets glyph information for a character.
    const GlyphInfo& getGlyph(char codepoint) const;

    /// @brief Gets the glyph atlas texture.
    GLuint getAtlasTextureId() const;

    /// @brief Gets atlas width in pixels.
    int getAtlasWidth() const;

    /// @brief Gets atlas height in pixels.
    int getAtlasHeight() const;

    /// @brief Gets the line height (ascender - descender + linegap) in pixels.
    float getLineHeight() const;

    /// @brief Gets the font ascender (pixels above baseline).
    float getAscender() const;

    /// @brief Gets the font descender (pixels below baseline, typically negative).
    float getDescender() const;

    /// @brief Gets the pixel size used.
    int getPixelSize() const;

    /// @brief Checks if a font has been loaded.
    bool isLoaded() const;

    /// @brief Gets the number of loaded glyphs.
    size_t getGlyphCount() const;

private:
    std::unordered_map<char, GlyphInfo> m_glyphs;
    GLuint m_atlasTexture = 0;
    int m_atlasWidth = 0;
    int m_atlasHeight = 0;
    float m_lineHeight = 0.0f;
    float m_ascender = 0.0f;
    float m_descender = 0.0f;
    int m_pixelSize = 0;
    GlyphInfo m_fallbackGlyph;
};

} // namespace Vestige

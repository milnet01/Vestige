// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file font.h
/// @brief TrueType font loading and glyph atlas generation via FreeType.
#pragma once

#include "renderer/texture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

    // Field-by-field equality — lets the slice-L1 ASCII back-compat test
    // (Font.AsciiBackwardCompat) assert a glyph is bit-identical to its
    // pre-codepoint-migration value. See phase_10_localization_design.md § 8 test 5.
    bool operator==(const GlyphInfo& o) const
    {
        return atlasOffset == o.atlasOffset && atlasSize == o.atlasSize
            && size == o.size && bearing == o.bearing && advance == o.advance;
    }
    bool operator!=(const GlyphInfo& o) const { return !(*this == o); }
};

/// @brief An inclusive codepoint interval to rasterise into a font atlas.
/// Phase 10 Localization slice L1: loadFromFile takes a list of these so a
/// font can load Latin / Greek / Hebrew blocks instead of just ASCII.
struct CodepointRange
{
    uint32_t firstInclusive;
    uint32_t lastInclusive;
};

inline const std::vector<CodepointRange> ASCII_RANGE  = {{0x0020, 0x007E}};
inline const std::vector<CodepointRange> LATIN1_RANGE = {{0x0020, 0x007E}, {0x00A0, 0x00FF}};
inline const std::vector<CodepointRange> GREEK_RANGES = {{0x0370, 0x03FF}, {0x1F00, 0x1FFF}};
inline const std::vector<CodepointRange> HEBREW_RANGE = {{0x0590, 0x05FF}};

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
    /// @param ranges Inclusive codepoint intervals to rasterise. Defaults to
    ///        the printable-ASCII range, so for a face carrying all of
    ///        0x20..0x7E (the normal case) the two-arg form reproduces the
    ///        pre-L1 ASCII-only load. Only codepoints the face actually
    ///        carries a glyph for are inserted (see hasGlyph) — so a face with
    ///        ASCII holes now skips them rather than storing a .notdef box.
    /// @return True if loading succeeded.
    bool loadFromFile(const std::string& filePath, int pixelSize = 48,
                      const std::vector<CodepointRange>& ranges = ASCII_RANGE);

    /// @brief Gets glyph information for a Unicode codepoint.
    const GlyphInfo& getGlyph(uint32_t codepoint) const;

    /// @brief char-keyed forwarder — slice L1 only, removed in L2 once the
    ///        ~10 callers migrate to the uint32_t form. The unsigned-char step
    ///        prevents sign-extension turning 0x80..0xFF into negative values.
    const GlyphInfo& getGlyph(char c) const
    {
        return getGlyph(static_cast<uint32_t>(static_cast<unsigned char>(c)));
    }

    /// @brief True iff this face rasterised a real glyph for @p codepoint
    ///        (not the fallback). Underpins the L2 FontStack fallback chain.
    bool hasGlyph(uint32_t codepoint) const;

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
    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;
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

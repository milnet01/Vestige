// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file font_stack.h
/// @brief Ordered font fallback chain for multi-script text (Phase 10
///        Localization slice L2). See docs/phases/phase_10_localization_design.md
///        § 5.3.
#pragma once

#include "renderer/font.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Vestige
{

/// @brief Ordered fallback chain — the first font that has a glyph for a
///        codepoint wins. Each script lives in its own Font instance / atlas,
///        so scripts can be loaded independently. The default stack (built in
///        TextRenderer::initialize) holds two fonts: Arimo (Latin + Greek) and
///        Frank Ruhl Libre (Hebrew).
class FontStack
{
public:
    /// @brief A located (font, glyph) pair. `font` is a non-owning pointer
    ///        valid until the next addFont/clear on this stack. The
    ///        TextRenderer's default stack is built once and never mutated
    ///        during rendering, so a Hit's `font` is stable for the frame.
    struct Hit
    {
        Font* font = nullptr;
        const GlyphInfo* glyph = nullptr;
    };

    /// @brief Append a font to the back of the chain. Search order is
    ///        insertion order.
    void addFont(std::shared_ptr<Font> font);

    /// @brief Remove all fonts. Invalidates any outstanding Hit pointers.
    void clear();

    /// @brief First font that covers @p codepoint wins; its real glyph is
    ///        returned. On a miss (no font covers it), returns
    ///        Hit{ first font, that font's fallback '?' glyph } so the caller
    ///        can bind an atlas unconditionally — `font` is never null unless
    ///        the stack is empty. Pins § 8 tests 7 & 8.
    Hit lookup(uint32_t codepoint) const;

    /// @brief Number of fonts in the chain.
    std::size_t fontCount() const { return m_fonts.size(); }

    /// @brief Font at chain index @p i (0 = primary). No bounds check.
    Font* fontAt(std::size_t i) const { return m_fonts[i].get(); }

    /// @brief True iff no fonts have been added.
    bool empty() const { return m_fonts.empty(); }

    /// @brief Count of lookup() invocations since the last reset. Used by the
    ///        MRU-cache test (§ 8 test 9) to confirm the TextRenderer's MRU
    ///        short-circuit skips the stack walk on pure-script runs. The
    ///        increment is one add per actual walk (rare in practice).
    std::size_t lookupCalls() const { return m_lookupCalls; }
    void resetLookupCalls() const { m_lookupCalls = 0; }

private:
    std::vector<std::shared_ptr<Font>> m_fonts;
    mutable std::size_t m_lookupCalls = 0;
};

} // namespace Vestige

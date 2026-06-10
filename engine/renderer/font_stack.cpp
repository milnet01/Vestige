// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file font_stack.cpp
/// @brief FontStack implementation (Phase 10 Localization slice L2).
#include "renderer/font_stack.h"

namespace Vestige
{

void FontStack::addFont(std::shared_ptr<Font> font)
{
    if (font)
    {
        m_fonts.push_back(std::move(font));
    }
}

void FontStack::clear()
{
    m_fonts.clear();
}

FontStack::Hit FontStack::lookup(uint32_t codepoint) const
{
    ++m_lookupCalls;

    if (m_fonts.empty())
    {
        return Hit{};  // degenerate: empty stack — caller guards on this.
    }

    for (const auto& font : m_fonts)
    {
        if (font->hasGlyph(codepoint))
        {
            return Hit{font.get(), &font->getGlyph(codepoint)};
        }
    }

    // Miss: no font covers the codepoint. Return the first font plus its
    // fallback '?' glyph (getGlyph yields the fallback on a miss), so the
    // caller binds the first font's atlas and draws the '?' — never null,
    // never a crash (§ 5.3 / § 8 test 8).
    Font* first = m_fonts.front().get();
    return Hit{first, &first->getGlyph(codepoint)};
}

} // namespace Vestige

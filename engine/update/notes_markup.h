// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file notes_markup.h
/// @brief Turns CHANGELOG markdown lines into lines the update dialog draws
///        (3D_E-0729, spec §4.3). A display subset, not a markdown renderer.
#pragma once

#include <string>
#include <vector>

namespace Vestige::Update
{

/// @brief One line of update notes, ready to draw.
struct NotesLine
{
    enum class Kind
    {
        Heading,    ///< "#" to "###"; level 1-3.
        Bullet,     ///< "- " or "* "; indent counts leading spaces / 2.
        Paragraph,  ///< Any other non-blank line.
        Blank,
    };
    Kind kind = Kind::Paragraph;
    int level = 0;   ///< Heading level.
    int indent = 0;  ///< Bullet or continuation nesting.
    std::string text;  ///< With `**`, backticks and [text](url) reduced to text.
};

/// @brief Classifies each line and strips inline markup. Links are kept as
///        their text only; nothing in the notes is clickable.
std::vector<NotesLine> parseNotes(const std::vector<std::string>& lines);

/// @brief Inline markup removed: `**bold**` -> bold, `` `code` `` -> code,
///        `[text](url)` -> text.
std::string stripInlineMarkup(const std::string& text);

}  // namespace Vestige::Update

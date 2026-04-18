// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file markdown_render.h
/// @brief Minimal markdown parser + ImGui renderer for the Suggestions panel.
///
/// Scoped to what ``scripts/llm_rank.py`` and ``scripts/pysr_driver.py``
/// actually emit — headings, paragraphs, pipe-delimited tables, horizontal
/// rules, fenced code blocks, and inline backtick code spans. This is not a
/// full CommonMark implementation; anything outside that subset is rendered
/// as plain paragraph text.
///
/// The parser is pure (no ImGui dependency) so it can be unit-tested in
/// isolation. ``renderMarkdownBlocks`` is the sole entry point that touches
/// ImGui widgets.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Vestige
{
namespace markdown
{

enum class BlockType
{
    Heading,          ///< level 1..3, text in ``text``
    Paragraph,        ///< plain text (possibly multi-line), in ``text``
    Table,            ///< ``headers`` + ``rows`` populated
    HorizontalRule,   ///< no payload
    CodeBlock,        ///< verbatim text in ``text`` (newlines preserved)
};

/// @brief One parsed block in the document.
struct Block
{
    BlockType                             type = BlockType::Paragraph;
    int                                   level = 0;  ///< heading level 1..3
    std::string                           text;
    std::vector<std::string>              headers;
    std::vector<std::vector<std::string>> rows;
};

/// @brief Parses a markdown document into a linear sequence of blocks.
///
/// Supported constructs:
///  - ``#``, ``##``, ``###`` headings
///  - Blank-line separated paragraphs
///  - GitHub-style pipe tables (header row + ``| --- |`` separator + body)
///  - ``---`` / ``***`` / ``___`` horizontal rules on a line by themselves
///  - Triple-backtick fenced code blocks
///
/// Inline formatting (``**bold**``, backtick code spans) is preserved as-is
/// in ``Block::text`` and ``Block::rows`` — the renderer applies inline
/// styling when drawing.
std::vector<Block> parseMarkdown(std::string_view source);

/// @brief Renders a parsed block list into the current ImGui window.
///
/// Uses ``ImGui::TextWrapped`` / ``ImGui::BeginTable`` / ``ImGui::Separator``
/// etc. Call from inside an ImGui ``Begin`` / ``End`` pair.
void renderMarkdownBlocks(const std::vector<Block>& blocks);

} // namespace markdown
} // namespace Vestige

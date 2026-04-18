// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_markdown_render.cpp
/// @brief Unit tests for ``markdown::parseMarkdown`` — the parser side of
///        the Suggestions-panel markdown renderer. ImGui rendering is not
///        exercised here (no GL context in the test fixture); the renderer
///        is validated by hand through the Workbench.

#include "markdown_render.h"

#include <gtest/gtest.h>

using Vestige::markdown::BlockType;
using Vestige::markdown::parseMarkdown;

TEST(MarkdownParser, EmptyInputProducesNoBlocks)
{
    const auto blocks = parseMarkdown("");
    EXPECT_TRUE(blocks.empty());
}

TEST(MarkdownParser, WhitespaceOnlyProducesNoBlocks)
{
    const auto blocks = parseMarkdown("   \n\n\t\n");
    EXPECT_TRUE(blocks.empty());
}

TEST(MarkdownParser, SingleParagraph)
{
    const auto blocks = parseMarkdown("The quick brown fox.");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::Paragraph);
    EXPECT_EQ(blocks[0].text, "The quick brown fox.");
}

TEST(MarkdownParser, MultilineParagraphJoinedByNewline)
{
    const auto blocks = parseMarkdown("line one\nline two\nline three");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::Paragraph);
    EXPECT_EQ(blocks[0].text, "line one\nline two\nline three");
}

TEST(MarkdownParser, BlankLineSeparatesParagraphs)
{
    const auto blocks = parseMarkdown("para one\n\npara two\n\npara three");
    ASSERT_EQ(blocks.size(), 3u);
    EXPECT_EQ(blocks[0].text, "para one");
    EXPECT_EQ(blocks[1].text, "para two");
    EXPECT_EQ(blocks[2].text, "para three");
}

TEST(MarkdownParser, HeadingLevels)
{
    const auto blocks = parseMarkdown("# H1\n\n## H2\n\n### H3");
    ASSERT_EQ(blocks.size(), 3u);
    EXPECT_EQ(blocks[0].type, BlockType::Heading);
    EXPECT_EQ(blocks[0].level, 1);
    EXPECT_EQ(blocks[0].text, "H1");
    EXPECT_EQ(blocks[1].level, 2);
    EXPECT_EQ(blocks[1].text, "H2");
    EXPECT_EQ(blocks[2].level, 3);
    EXPECT_EQ(blocks[2].text, "H3");
}

TEST(MarkdownParser, DeepHeadingTreatedAsParagraph)
{
    const auto blocks = parseMarkdown("#### not a heading");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::Paragraph);
}

TEST(MarkdownParser, HashWithoutSpaceNotHeading)
{
    const auto blocks = parseMarkdown("#nospace");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::Paragraph);
    EXPECT_EQ(blocks[0].text, "#nospace");
}

TEST(MarkdownParser, HorizontalRuleRecognised)
{
    const auto blocks = parseMarkdown("before\n\n---\n\nafter");
    ASSERT_EQ(blocks.size(), 3u);
    EXPECT_EQ(blocks[0].type, BlockType::Paragraph);
    EXPECT_EQ(blocks[1].type, BlockType::HorizontalRule);
    EXPECT_EQ(blocks[2].type, BlockType::Paragraph);
}

TEST(MarkdownParser, HorizontalRuleVariants)
{
    const auto starBlocks = parseMarkdown("***");
    ASSERT_EQ(starBlocks.size(), 1u);
    EXPECT_EQ(starBlocks[0].type, BlockType::HorizontalRule);

    const auto underscoreBlocks = parseMarkdown("___");
    ASSERT_EQ(underscoreBlocks.size(), 1u);
    EXPECT_EQ(underscoreBlocks[0].type, BlockType::HorizontalRule);
}

TEST(MarkdownParser, FencedCodeBlock)
{
    const auto blocks = parseMarkdown("```\nint x = 1;\nreturn x;\n```");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::CodeBlock);
    EXPECT_EQ(blocks[0].text, "int x = 1;\nreturn x;");
}

TEST(MarkdownParser, FencedCodeBlockWithLanguageTag)
{
    const auto blocks = parseMarkdown("```json\n{\"key\": 1}\n```");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::CodeBlock);
    EXPECT_EQ(blocks[0].text, "{\"key\": 1}");
}

TEST(MarkdownParser, BasicTable)
{
    const auto blocks = parseMarkdown(
        "| Rank | Formula | Why |\n"
        "| --- | --- | --- |\n"
        "| 1 | beer_lambert | exponential decay fits |\n"
        "| 2 | stokes_drag | linear response matches |");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::Table);
    ASSERT_EQ(blocks[0].headers.size(), 3u);
    EXPECT_EQ(blocks[0].headers[0], "Rank");
    EXPECT_EQ(blocks[0].headers[1], "Formula");
    EXPECT_EQ(blocks[0].headers[2], "Why");
    ASSERT_EQ(blocks[0].rows.size(), 2u);
    EXPECT_EQ(blocks[0].rows[0][0], "1");
    EXPECT_EQ(blocks[0].rows[0][1], "beer_lambert");
    EXPECT_EQ(blocks[0].rows[1][1], "stokes_drag");
}

TEST(MarkdownParser, TableWithoutOuterPipes)
{
    const auto blocks = parseMarkdown(
        "a | b\n"
        "--- | ---\n"
        "1 | 2");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::Table);
    ASSERT_EQ(blocks[0].headers.size(), 2u);
    EXPECT_EQ(blocks[0].headers[0], "a");
    EXPECT_EQ(blocks[0].headers[1], "b");
}

TEST(MarkdownParser, TableSeparatorAcceptsAlignmentColons)
{
    const auto blocks = parseMarkdown(
        "| a | b | c |\n"
        "| :--- | :---: | ---: |\n"
        "| 1 | 2 | 3 |");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::Table);
    ASSERT_EQ(blocks[0].rows.size(), 1u);
}

TEST(MarkdownParser, PipeWithoutSeparatorIsParagraph)
{
    // A lone pipe line with no separator below shouldn't become a
    // table — otherwise we'd spuriously reinterpret sentences that
    // happen to contain '|'.
    const auto blocks = parseMarkdown(
        "Something about option A | option B here.");
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].type, BlockType::Paragraph);
}

TEST(MarkdownParser, MixedLlmRankOutput)
{
    // Shape mirrors what scripts/llm_rank.py actually emits: title
    // heading, metadata paragraph, scored table, then a Caveats
    // heading + paragraph.
    const std::string source =
        "# Formula ranking — data.csv\n"
        "\n"
        "Model: `claude-sonnet-4-6`. Dataset: 42 points, 2 variables.\n"
        "\n"
        "| Rank | Formula | Plausibility | Why |\n"
        "| --- | --- | --- | --- |\n"
        "| 1 | beer_lambert | high | exponential attenuation |\n"
        "| 2 | stokes_drag | medium | linear response |\n"
        "\n"
        "## Caveats\n"
        "\n"
        "The `x1` range looks narrow; consider resampling.";

    const auto blocks = parseMarkdown(source);
    ASSERT_EQ(blocks.size(), 5u);
    EXPECT_EQ(blocks[0].type, BlockType::Heading);
    EXPECT_EQ(blocks[0].level, 1);
    EXPECT_EQ(blocks[1].type, BlockType::Paragraph);
    EXPECT_EQ(blocks[2].type, BlockType::Table);
    EXPECT_EQ(blocks[2].headers.size(), 4u);
    EXPECT_EQ(blocks[2].rows.size(), 2u);
    EXPECT_EQ(blocks[3].type, BlockType::Heading);
    EXPECT_EQ(blocks[3].level, 2);
    EXPECT_EQ(blocks[4].type, BlockType::Paragraph);
}

TEST(MarkdownParser, HeadingImmediatelyAfterParagraphFlushes)
{
    // No blank line between paragraph and heading — parser should
    // still recognise the heading on the next line.
    const auto blocks = parseMarkdown("para\n# heading");
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[0].type, BlockType::Paragraph);
    EXPECT_EQ(blocks[0].text, "para");
    EXPECT_EQ(blocks[1].type, BlockType::Heading);
    EXPECT_EQ(blocks[1].text, "heading");
}

TEST(MarkdownParser, TrailingWhitespaceTrimmedFromCells)
{
    const auto blocks = parseMarkdown(
        "|  a  |   b   |\n"
        "| --- | --- |\n"
        "|  1  |  2   |");
    ASSERT_EQ(blocks.size(), 1u);
    ASSERT_EQ(blocks[0].headers.size(), 2u);
    EXPECT_EQ(blocks[0].headers[0], "a");
    EXPECT_EQ(blocks[0].headers[1], "b");
    EXPECT_EQ(blocks[0].rows[0][0], "1");
    EXPECT_EQ(blocks[0].rows[0][1], "2");
}

TEST(MarkdownParser, EscapedPipeInsideTableCell)
{
    const auto blocks = parseMarkdown(
        "| a | b |\n"
        "| --- | --- |\n"
        "| one \\| two | three |");
    ASSERT_EQ(blocks.size(), 1u);
    ASSERT_EQ(blocks[0].rows.size(), 1u);
    EXPECT_EQ(blocks[0].rows[0][0], "one \\| two");
    EXPECT_EQ(blocks[0].rows[0][1], "three");
}

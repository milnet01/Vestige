// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file markdown_render.cpp
/// @brief Parser and ImGui renderer for a narrow markdown subset.

#include "markdown_render.h"

#include "imgui.h"

namespace Vestige
{
namespace markdown
{

namespace
{

std::string_view trimRight(std::string_view s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::string_view trimLeft(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    return s;
}

std::string_view trim(std::string_view s)
{
    return trimLeft(trimRight(s));
}

std::vector<std::string_view> splitLines(std::string_view source)
{
    std::vector<std::string_view> out;
    size_t start = 0;
    for (size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] == '\n')
        {
            out.emplace_back(source.substr(start, i - start));
            start = i + 1;
        }
    }
    if (start <= source.size())
        out.emplace_back(source.substr(start));
    return out;
}

// Splits a single table row "| a | b | c |" into {"a", "b", "c"}. The
// leading and trailing pipes are optional (GFM lets the row start/end
// without them); whitespace around each cell is trimmed.
std::vector<std::string> splitTableRow(std::string_view line)
{
    std::string_view s = trim(line);
    if (!s.empty() && s.front() == '|')
        s.remove_prefix(1);
    if (!s.empty() && s.back() == '|')
        s.remove_suffix(1);

    std::vector<std::string> cells;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        // Backslash-escaped pipe inside a cell stays literal.
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '|')
        {
            ++i;
            continue;
        }
        if (s[i] == '|')
        {
            cells.emplace_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    cells.emplace_back(trim(s.substr(start)));
    return cells;
}

// GFM table separator row: every cell matches /^:?-+:?$/ (optional
// colons for alignment hints, which we ignore).
bool isTableSeparatorRow(std::string_view line)
{
    std::string_view s = trim(line);
    if (s.empty() || s.find('|') == std::string_view::npos)
        return false;
    auto cells = splitTableRow(s);
    // splitTableRow always returns at least one (possibly empty) cell; we
    // rely on the per-cell empty-check below to reject malformed rows.
    for (const auto& cell : cells)
    {
        std::string_view c = cell;
        if (!c.empty() && c.front() == ':')
            c.remove_prefix(1);
        if (!c.empty() && c.back() == ':')
            c.remove_suffix(1);
        if (c.empty())
            return false;
        for (char ch : c)
        {
            if (ch != '-')
                return false;
        }
    }
    return true;
}

bool isTableLine(std::string_view line)
{
    std::string_view s = trim(line);
    return !s.empty() && s.find('|') != std::string_view::npos;
}

bool isHorizontalRule(std::string_view line)
{
    std::string_view s = trim(line);
    if (s.size() < 3)
        return false;
    const char c = s.front();
    if (c != '-' && c != '_' && c != '*')
        return false;
    for (char ch : s)
    {
        if (ch != c)
            return false;
    }
    return true;
}

// Heading detection: "# text", "## text", "### text". Returns the level
// 1..3 and writes the trimmed heading text to ``outText``. Returns 0
// when the line isn't a heading (including ``####`` and deeper, which
// we treat as paragraph text — the Suggestions panel outputs only go
// two levels deep).
int parseHeading(std::string_view line, std::string& outText)
{
    std::string_view s = trimLeft(line);
    int hashes = 0;
    while (hashes < 4 && hashes < static_cast<int>(s.size()) && s[hashes] == '#')
        ++hashes;
    if (hashes < 1 || hashes > 3)
        return 0;
    if (hashes == static_cast<int>(s.size()))
        return 0;
    if (s[hashes] != ' ')
        return 0;
    outText = std::string(trim(s.substr(hashes + 1)));
    return hashes;
}

bool isFenceLine(std::string_view line)
{
    std::string_view s = trim(line);
    return s.size() >= 3 && s.substr(0, 3) == "```";
}

} // namespace

std::vector<Block> parseMarkdown(std::string_view source)
{
    std::vector<Block> out;
    const auto lines = splitLines(source);

    auto flushParagraph = [&](std::string& buffer) {
        if (buffer.empty())
            return;
        // Trim trailing newline for cleaner rendering.
        while (!buffer.empty() && buffer.back() == '\n')
            buffer.pop_back();
        if (!buffer.empty())
        {
            Block b;
            b.type = BlockType::Paragraph;
            b.text = std::move(buffer);
            out.push_back(std::move(b));
        }
        buffer.clear();
    };

    std::string paragraph;
    size_t i = 0;
    while (i < lines.size())
    {
        std::string_view line = lines[i];

        if (isFenceLine(line))
        {
            flushParagraph(paragraph);
            std::string code;
            ++i;
            while (i < lines.size() && !isFenceLine(lines[i]))
            {
                code.append(lines[i]);
                code.push_back('\n');
                ++i;
            }
            if (i < lines.size())   // consume closing fence if present
                ++i;
            if (!code.empty() && code.back() == '\n')
                code.pop_back();
            Block b;
            b.type = BlockType::CodeBlock;
            b.text = std::move(code);
            out.push_back(std::move(b));
            continue;
        }

        if (trim(line).empty())
        {
            flushParagraph(paragraph);
            ++i;
            continue;
        }

        if (isHorizontalRule(line))
        {
            flushParagraph(paragraph);
            Block b;
            b.type = BlockType::HorizontalRule;
            out.push_back(std::move(b));
            ++i;
            continue;
        }

        std::string headingText;
        const int level = parseHeading(line, headingText);
        if (level > 0)
        {
            flushParagraph(paragraph);
            Block b;
            b.type  = BlockType::Heading;
            b.level = level;
            b.text  = std::move(headingText);
            out.push_back(std::move(b));
            ++i;
            continue;
        }

        // Table: current line has a pipe, next line is a separator.
        if (isTableLine(line) && i + 1 < lines.size() && isTableSeparatorRow(lines[i + 1]))
        {
            flushParagraph(paragraph);
            Block b;
            b.type    = BlockType::Table;
            b.headers = splitTableRow(line);
            i += 2;   // skip header + separator
            while (i < lines.size() && isTableLine(lines[i]) && !isTableSeparatorRow(lines[i]))
            {
                b.rows.push_back(splitTableRow(lines[i]));
                ++i;
            }
            out.push_back(std::move(b));
            continue;
        }

        if (!paragraph.empty())
            paragraph.push_back('\n');
        paragraph.append(line);
        ++i;
    }
    flushParagraph(paragraph);
    return out;
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

namespace
{

constexpr ImVec4 kCodeColor    { 0.55f, 0.90f, 1.00f, 1.0f };   // cyan-ish
constexpr ImVec4 kH1Color      { 1.00f, 0.95f, 0.60f, 1.0f };   // warm yellow
constexpr ImVec4 kH2Color      { 0.75f, 1.00f, 0.75f, 1.0f };   // soft green
constexpr ImVec4 kH3Color      { 0.85f, 0.85f, 1.00f, 1.0f };   // soft blue

// Strips ``**bold**`` markers. We don't have a bold font loaded, so
// rendering them verbatim would look worse than plain text.
std::string stripBoldMarkers(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*')
        {
            ++i;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

// Inline renderer: walks ``text`` and splits it into spans around
// backtick-delimited code pairs. Words are emitted one at a time with
// manual wrap-on-overflow, so cyan-coloured code spans sit inline with
// default-coloured prose and the whole paragraph still wraps at the
// available content width.
void renderInline(const std::string& rawText)
{
    const std::string text = stripBoldMarkers(rawText);

    // Short-circuit: no inline code → one wrapped block.
    if (text.find('`') == std::string::npos)
    {
        ImGui::TextWrapped("%s", text.c_str());
        return;
    }

    // Tokenise into (word, isCode) pairs. Words are whitespace-delimited;
    // toggling code state happens at every backtick.
    struct Token { std::string word; bool code; bool spaceBefore; };
    std::vector<Token> tokens;
    bool inCode = false;
    std::string buf;
    bool pendingSpace = false;
    auto flush = [&]() {
        if (!buf.empty())
        {
            tokens.push_back({std::move(buf), inCode, pendingSpace});
            buf.clear();
            pendingSpace = false;
        }
    };
    for (char c : text)
    {
        if (c == '`')
        {
            flush();
            inCode = !inCode;
            continue;
        }
        if (c == ' ' || c == '\t')
        {
            flush();
            pendingSpace = true;
            continue;
        }
        buf.push_back(c);
    }
    flush();

    const float wrapWidth  = ImGui::GetContentRegionAvail().x;
    const float startX     = ImGui::GetCursorPosX();
    const float spaceWidth = ImGui::CalcTextSize(" ").x;
    float curX             = startX;
    bool  firstOnLine      = true;

    for (const auto& tok : tokens)
    {
        const float wordW = ImGui::CalcTextSize(tok.word.c_str()).x;
        const float gap   = (firstOnLine || !tok.spaceBefore) ? 0.0f : spaceWidth;

        if (!firstOnLine && curX - startX + gap + wordW > wrapWidth)
        {
            ImGui::NewLine();
            curX        = startX;
            firstOnLine = true;
        }

        if (!firstOnLine)
            ImGui::SameLine(0.0f, gap);

        if (tok.code)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kCodeColor);
            ImGui::TextUnformatted(tok.word.c_str());
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextUnformatted(tok.word.c_str());
        }

        curX += gap + wordW;
        firstOnLine = false;
    }
}

} // namespace

void renderMarkdownBlocks(const std::vector<Block>& blocks)
{
    for (size_t idx = 0; idx < blocks.size(); ++idx)
    {
        const Block& b = blocks[idx];
        ImGui::PushID(static_cast<int>(idx));
        switch (b.type)
        {
        case BlockType::Heading:
        {
            const ImVec4 color = (b.level == 1) ? kH1Color
                                : (b.level == 2) ? kH2Color
                                                 : kH3Color;
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", b.text.c_str());
            ImGui::PopStyleColor();
            if (b.level == 1)
                ImGui::Separator();
            ImGui::Spacing();
            break;
        }
        case BlockType::Paragraph:
            renderInline(b.text);
            ImGui::Spacing();
            break;
        case BlockType::HorizontalRule:
            ImGui::Separator();
            break;
        case BlockType::CodeBlock:
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kCodeColor);
            ImGui::TextUnformatted(b.text.c_str());
            ImGui::PopStyleColor();
            ImGui::Spacing();
            break;
        }
        case BlockType::Table:
        {
            const int nCols = static_cast<int>(b.headers.size());
            if (nCols > 0 && ImGui::BeginTable("##md_table",
                                                nCols,
                                                ImGuiTableFlags_Borders
                                                    | ImGuiTableFlags_RowBg
                                                    | ImGuiTableFlags_Resizable
                                                    | ImGuiTableFlags_SizingStretchProp))
            {
                for (const auto& h : b.headers)
                    ImGui::TableSetupColumn(h.c_str());
                ImGui::TableHeadersRow();
                for (const auto& row : b.rows)
                {
                    ImGui::TableNextRow();
                    for (int c = 0; c < nCols; ++c)
                    {
                        ImGui::TableSetColumnIndex(c);
                        const std::string& cell = (c < static_cast<int>(row.size())) ? row[c] : std::string();
                        renderInline(cell);
                    }
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            break;
        }
        }
        ImGui::PopID();
    }
}

} // namespace markdown
} // namespace Vestige

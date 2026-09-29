// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file notes_markup.cpp
#include "update/notes_markup.h"

namespace Vestige::Update
{

std::string stripInlineMarkup(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '*' && i + 1 < text.size() && text[i + 1] == '*')
        {
            ++i;  // drop "**"
            continue;
        }
        if (c == '`')
        {
            continue;
        }
        if (c == '[')
        {
            const size_t close = text.find(']', i + 1);
            if (close != std::string::npos && close + 1 < text.size() && text[close + 1] == '(')
            {
                const size_t paren = text.find(')', close + 2);
                if (paren != std::string::npos)
                {
                    out.append(text, i + 1, close - i - 1);
                    i = paren;
                    continue;
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

std::vector<NotesLine> parseNotes(const std::vector<std::string>& lines)
{
    std::vector<NotesLine> out;
    out.reserve(lines.size());
    for (const std::string& raw : lines)
    {
        NotesLine line;
        size_t spaces = 0;
        while (spaces < raw.size() && raw[spaces] == ' ')
        {
            ++spaces;
        }
        const std::string body = raw.substr(spaces);
        line.indent = static_cast<int>(spaces / 2);

        if (body.empty())
        {
            line.kind = NotesLine::Kind::Blank;
        }
        else if (spaces == 0 && body[0] == '#')
        {
            size_t hashes = 0;
            while (hashes < body.size() && body[hashes] == '#')
            {
                ++hashes;
            }
            if (hashes <= 3 && hashes < body.size() && body[hashes] == ' ')
            {
                line.kind = NotesLine::Kind::Heading;
                line.level = static_cast<int>(hashes);
                line.indent = 0;
                line.text = stripInlineMarkup(body.substr(hashes + 1));
            }
            else
            {
                line.text = stripInlineMarkup(body);
            }
        }
        else if (body.size() >= 2 && (body[0] == '-' || body[0] == '*') && body[1] == ' ')
        {
            line.kind = NotesLine::Kind::Bullet;
            line.text = stripInlineMarkup(body.substr(2));
        }
        else
        {
            line.text = stripInlineMarkup(body);
        }
        out.push_back(std::move(line));
    }
    return out;
}

}  // namespace Vestige::Update

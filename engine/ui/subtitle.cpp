// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file subtitle.cpp
/// @brief `SubtitleQueue` implementation.

#include "ui/subtitle.h"

#include <algorithm>

namespace Vestige
{

namespace
{

/// @brief UTF-8 ellipsis ("…", U+2026).
constexpr const char* kEllipsis = "\xE2\x80\xA6";

/// @brief Strips the last char from `line` repeatedly until appending
///        the ellipsis keeps it within `budget`. Assumes ASCII text;
///        multi-byte-UTF-8 at the truncation boundary is left intact
///        only if it happens to fall clear of the strip point — callers
///        concerned with that should pre-normalise (Phase 10.7 captions
///        are ASCII by policy).
void truncateWithEllipsis(std::string& line, std::size_t budget)
{
    // The ellipsis renders as one glyph but occupies 1 character in
    // the caller's char-count budget (matches how a reader perceives
    // "…" on a caption plate).
    if (budget == 0)
    {
        line = kEllipsis;
        return;
    }
    while (line.size() + 1 > budget && !line.empty())
    {
        line.pop_back();
    }
    // Strip trailing whitespace so we don't produce "foo …".
    while (!line.empty() && line.back() == ' ')
    {
        line.pop_back();
    }
    line += kEllipsis;
}

/// @brief Splits `text` on ASCII spaces, emitting each run of non-space
///        chars as one token. Empty inputs and runs are dropped.
std::vector<std::string> tokenize(const std::string& text)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char c : text)
    {
        if (c == ' ')
        {
            if (!current.empty())
            {
                tokens.push_back(std::move(current));
                current.clear();
            }
        }
        else
        {
            current += c;
        }
    }
    if (!current.empty())
    {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

} // namespace

std::vector<std::string> wrapSubtitleText(const std::string& text,
                                          std::size_t maxCharsPerLine,
                                          std::size_t maxLines)
{
    if (text.empty() || maxCharsPerLine == 0 || maxLines == 0)
    {
        return {};
    }

    std::vector<std::string> tokens = tokenize(text);
    if (tokens.empty())
    {
        return {};
    }

    std::vector<std::string> output;
    std::string current;
    const auto flushLine = [&]()
    {
        if (!current.empty())
        {
            output.push_back(std::move(current));
            current.clear();
        }
    };

    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        std::string token = std::move(tokens[i]);

        // Hard-break overlong single tokens at the char limit — a
        // 50-char URL or technical identifier should never overflow
        // the plate just because there's no space to wrap on.
        while (token.size() > maxCharsPerLine)
        {
            if (!current.empty())
            {
                flushLine();
            }
            output.push_back(token.substr(0, maxCharsPerLine));
            token.erase(0, maxCharsPerLine);
        }

        const std::size_t needed = current.empty()
            ? token.size()
            : current.size() + 1 + token.size(); // +1 for joining space
        if (needed > maxCharsPerLine)
        {
            flushLine();
            current = token;
        }
        else
        {
            if (!current.empty())
            {
                current += ' ';
            }
            current += token;
        }
    }
    flushLine();

    // Enforce the maxLines cap with a visible truncation marker.
    if (output.size() > maxLines)
    {
        output.resize(maxLines);
        truncateWithEllipsis(output.back(), maxCharsPerLine);
    }

    return output;
}

float subtitleScaleFactorOf(SubtitleSizePreset preset)
{
    switch (preset)
    {
        case SubtitleSizePreset::Small:  return 1.00f;
        case SubtitleSizePreset::Medium: return 1.25f;
        case SubtitleSizePreset::Large:  return 1.50f;
        case SubtitleSizePreset::XL:     return 2.00f;
    }
    return 1.0f;
}

void SubtitleQueue::enqueue(const Subtitle& subtitle)
{
    // Push newest; if at capacity evict the oldest entry so the
    // currently-speaking line isn't dropped in favour of a stale one.
    if (m_maxConcurrent > 0
        && static_cast<int>(m_active.size()) >= m_maxConcurrent)
    {
        m_active.erase(m_active.begin());
    }

    ActiveSubtitle entry;
    entry.subtitle = subtitle;
    entry.remainingSeconds = std::max(0.0f, subtitle.durationSeconds);
    m_active.push_back(entry);
}

void SubtitleQueue::tick(float deltaTime)
{
    for (auto& entry : m_active)
    {
        entry.remainingSeconds -= deltaTime;
    }
    m_active.erase(
        std::remove_if(m_active.begin(), m_active.end(),
            [](const ActiveSubtitle& e) { return e.remainingSeconds <= 0.0f; }),
        m_active.end());
}

void SubtitleQueue::setMaxConcurrent(int n)
{
    m_maxConcurrent = n;
    if (n >= 0 && static_cast<int>(m_active.size()) > n)
    {
        // Drop oldest entries until we fit. erase(begin, begin+k) is
        // O(k) but the cap is small (3 by default).
        const std::size_t toDrop = m_active.size() - static_cast<std::size_t>(n);
        m_active.erase(m_active.begin(),
                       m_active.begin() + static_cast<std::ptrdiff_t>(toDrop));
    }
}

} // namespace Vestige

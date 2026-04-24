// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file subtitle.cpp
/// @brief `SubtitleQueue` implementation.

#include "ui/subtitle.h"

#include <algorithm>

namespace Vestige
{

std::vector<std::string> wrapSubtitleText(const std::string& text,
                                          std::size_t /*maxCharsPerLine*/,
                                          std::size_t /*maxLines*/)
{
    // Phase 10.9 P1 (red): stub returns the whole string on one line.
    // Green will replace with word-boundary packing + ellipsis-truncate.
    if (text.empty())
    {
        return {};
    }
    return { text };
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

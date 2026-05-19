// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file subtitle_test_helpers.h
/// @brief Shared helpers for subtitle/closed-caption tests.
///
/// Phase 10.9 Slice 20 Ts20-DU9 extraction: `makeLine` was defined
/// byte-identical across test_subtitle.cpp and test_subtitle_renderer.cpp.
/// Both now forward to this single canonical definition.
#pragma once

#include "ui/subtitle.h"

#include <string>

namespace Vestige::Testing
{

/// @brief Build a Subtitle with sensible defaults — used by the
///        accessibility queue tests and the HUD layout tests.
inline Subtitle makeLine(const std::string& text,
                          float duration = 3.0f,
                          SubtitleCategory cat = SubtitleCategory::Dialogue,
                          const std::string& speaker = "")
{
    Subtitle s;
    s.text = text;
    s.durationSeconds = duration;
    s.category = cat;
    s.speaker = speaker;
    return s;
}

}  // namespace Vestige::Testing

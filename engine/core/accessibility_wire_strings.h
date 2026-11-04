// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file accessibility_wire_strings.h
/// @brief 3D_E-0868 — the one list of each accessibility choice stored as text.
///
/// `AccessibilitySettings` stores the UI scale, subtitle size and colour-vision
/// filter as these strings. Settings validation, the apply converters, the
/// editor's Accessibility tab and the player's Accessibility page all read
/// these lists, so a new choice is added here and nowhere else.
///
/// Each list is in its enum's order: index i is the enum value i
/// (`UIScalePreset`, `SubtitleSizePreset`, `ColorVisionMode`).
/// settings_apply.cpp checks each list's length against its enum at compile time.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace Vestige
{

/// @brief UI scale presets, in `UIScalePreset` order. Default "1.0x".
inline constexpr std::array<const char*, 4> kUiScaleWireStrings = {
    "1.0x", "1.25x", "1.5x", "2.0x",
};

/// @brief Subtitle sizes, in `SubtitleSizePreset` order. Default "medium".
inline constexpr std::array<const char*, 4> kSubtitleSizeWireStrings = {
    "small", "medium", "large", "xl",
};

/// @brief Colour-vision filters, in `ColorVisionMode` order. Default "none".
inline constexpr std::array<const char*, 4> kColorVisionWireStrings = {
    "none", "protanopia", "deuteranopia", "tritanopia",
};

/// @brief The index of @a s in @a list, or -1 when it is not in the list.
template <std::size_t N>
constexpr int wireStringIndex(const std::array<const char*, N>& list, std::string_view s)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (s == list[i])
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace Vestige

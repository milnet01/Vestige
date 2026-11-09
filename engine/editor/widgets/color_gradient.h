// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file color_gradient.h
/// @brief Color gradient with draggable stops for particle color over lifetime.
#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <vector>

namespace Vestige
{

/// @brief A color gradient defined by color stops over [0, 1].
///
/// Used for particle color-over-lifetime. Stops are kept sorted by position.
/// Evaluation linearly interpolates RGBA between adjacent stops.
struct ColorGradient
{
    struct ColorStop
    {
        float position = 0.0f;     ///< Normalized position [0, 1]
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    std::vector<ColorStop> stops = {
        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 1.0f, 0.0f}}
    };

    /// @brief Evaluates the gradient at normalized position t [0, 1].
    glm::vec4 evaluate(float t) const;

    /// @brief Adds a color stop and keeps the list sorted.
    void addStop(float position, const glm::vec4& color);

    /// @brief Removes the stop at the given index (won't remove if only 2 remain).
    void removeStop(int index);

    /// @brief Ensures stops are sorted by position.
    void sort();

    /// @brief Sorts, and returns where the stop that was at @a index now is
    ///        (-1 stays -1), so an editor's selection follows its stop.
    int sortTracking(int index);

    /// @brief Where the stop at @a index is after the stop at @a removed was
    ///        erased: one earlier if it came after, -1 if it was the one removed.
    static int indexAfterRemoval(int index, int removed);

    /// @brief Serializes the gradient to JSON.
    nlohmann::json toJson() const;

    /// @brief Deserializes the gradient from JSON.
    static ColorGradient fromJson(const nlohmann::json& j);
};

} // namespace Vestige

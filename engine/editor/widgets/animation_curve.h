// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file animation_curve.h
/// @brief Keyframe-based animation curve with linear interpolation.
#pragma once

#include <nlohmann/json_fwd.hpp>

#include <vector>

namespace Vestige
{

/// @brief A piecewise-linear curve defined by keyframes over [0, 1].
///
/// Used for particle over-lifetime modifiers (size, speed, etc.).
/// Keyframes are kept sorted by time. Evaluation linearly interpolates
/// between adjacent keyframes, clamping at the endpoints.
struct AnimationCurve
{
    struct Keyframe
    {
        float time = 0.0f;    ///< Normalized time [0, 1]
        float value = 0.0f;   ///< Value at this time
    };

    std::vector<Keyframe> keyframes = {{0.0f, 1.0f}, {1.0f, 0.0f}};

    /// @brief Evaluates the curve at normalized time t [0, 1].
    float evaluate(float t) const;

    /// @brief Adds a keyframe and keeps the list sorted by time.
    void addKeyframe(float time, float value);

    /// @brief Removes the keyframe at the given index (won't remove if only 2 remain).
    void removeKeyframe(int index);

    /// @brief Ensures keyframes are sorted by time.
    void sort();

    /// @brief Serializes the curve to JSON.
    nlohmann::json toJson() const;

    /// @brief Deserializes the curve from JSON.
    static AnimationCurve fromJson(const nlohmann::json& j);
};

} // namespace Vestige

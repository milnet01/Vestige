// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file light_utils.h
/// @brief Utility functions for light range calculation, attenuation, and color temperature.
#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

/// @brief Calculates the effective light range from attenuation coefficients.
/// Returns the distance where intensity drops to ~0.4% (1/256).
inline float calculateLightRange(float constant, float linear, float quadratic)
{
    // Solve: 1/(c + l*d + q*d^2) = 1/256
    // => q*d^2 + l*d + (c - 256) = 0
    // Quadratic formula: d = (-l + sqrt(l^2 - 4*q*(c-256))) / (2*q)
    if (quadratic <= 0.0f)
    {
        // No quadratic term — linear falloff or constant
        if (linear > 0.0f)
        {
            return (256.0f - constant) / linear;
        }
        return 50.0f; // fallback for constant-only
    }

    float a = quadratic;
    float b = linear;
    float c = constant - 256.0f;
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f)
    {
        return 50.0f; // fallback
    }
    return (-b + std::sqrt(discriminant)) / (2.0f * a);
}

/// @brief Sets attenuation coefficients from a desired range.
/// Uses the LearnOpenGL attenuation table values, scaled to the given range.
inline void setAttenuationFromRange(float range,
                                    float& outConstant,
                                    float& outLinear,
                                    float& outQuadratic)
{
    range = std::max(range, 0.1f);
    outConstant  = 1.0f;
    outLinear    = 4.5f / range;
    outQuadratic = 75.0f / (range * range);
}

/// @brief Converts color temperature in Kelvin to an RGB color.
/// Based on the Tanner Helland algorithm.
/// @param kelvin Temperature in range [1000, 40000].
/// @return Normalized RGB color (each channel 0.0 to 1.0).
inline glm::vec3 kelvinToRgb(float kelvin)
{
    float temp = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;

    float r, g, b;

    // Red
    if (temp <= 66.0f)
    {
        r = 1.0f;
    }
    else
    {
        r = temp - 60.0f;
        r = 329.698727446f * std::pow(r, -0.1332047592f);
        r = std::clamp(r / 255.0f, 0.0f, 1.0f);
    }

    // Green
    if (temp <= 66.0f)
    {
        g = 99.4708025861f * std::log(temp) - 161.1195681661f;
        g = std::clamp(g / 255.0f, 0.0f, 1.0f);
    }
    else
    {
        g = temp - 60.0f;
        g = 288.1221695283f * std::pow(g, -0.0755148492f);
        g = std::clamp(g / 255.0f, 0.0f, 1.0f);
    }

    // Blue
    if (temp >= 66.0f)
    {
        b = 1.0f;
    }
    else if (temp <= 19.0f)
    {
        b = 0.0f;
    }
    else
    {
        b = temp - 10.0f;
        b = 138.5177312231f * std::log(b) - 305.0447927307f;
        b = std::clamp(b / 255.0f, 0.0f, 1.0f);
    }

    return glm::vec3(r, g, b);
}

} // namespace Vestige

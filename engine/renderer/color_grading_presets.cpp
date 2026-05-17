// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#include "color_grading_presets.h"

#include <algorithm>

namespace Vestige::ColorGradingPresets
{

glm::vec3 warmGolden(const glm::vec3& c)
{
    glm::vec3 out;
    out.r = std::clamp(c.r * 1.05f + 0.02f, 0.0f, 1.0f);
    out.g = c.g;
    out.b = std::clamp(c.b * 0.85f - 0.02f, 0.0f, 1.0f);
    // Gentle contrast S-curve.
    out = out * out * (3.0f - 2.0f * out);
    return out;
}

glm::vec3 coolBlue(const glm::vec3& c)
{
    glm::vec3 out;
    out.r = std::clamp(c.r * 0.85f, 0.0f, 1.0f);
    out.g = std::clamp(c.g * 0.95f + 0.02f, 0.0f, 1.0f);
    out.b = std::clamp(c.b * 1.1f + 0.03f, 0.0f, 1.0f);
    // Slight shadow lift.
    out += glm::vec3(0.02f);
    out = glm::clamp(out, 0.0f, 1.0f);
    return out;
}

glm::vec3 highContrast(const glm::vec3& c)
{
    // Hermite smoothstep S-curve per channel.
    return c * c * (3.0f - 2.0f * c);
}

glm::vec3 desaturated(const glm::vec3& c)
{
    float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    return glm::mix(glm::vec3(lum), c, 0.5f);
}

}  // namespace Vestige::ColorGradingPresets

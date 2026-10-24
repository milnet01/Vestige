// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file light_color.cpp
/// @brief Splits a light's stored colour into a colour and a brightness.
#include "editor/light_color.h"

#include <algorithm>

namespace Vestige
{

LightColor splitLightColor(const glm::vec3& stored)
{
    LightColor lc;
    lc.brightness = std::max({stored.r, stored.g, stored.b, 0.0f});
    if (lc.brightness > 0.0f)
    {
        lc.color = stored / lc.brightness;
    }
    return lc;
}

glm::vec3 combineLightColor(const LightColor& lc)
{
    return lc.color * lc.brightness;
}

LightColor reconcileLightColor(const LightColor& shown, const glm::vec3& stored)
{
    return combineLightColor(shown) == stored ? shown : splitLightColor(stored);
}

} // namespace Vestige

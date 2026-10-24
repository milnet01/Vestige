// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file light_color.h
/// @brief Splits a light's stored colour into a colour and a brightness.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief A light colour as the inspector shows it.
///
/// A light stores one vec3 (`diffuse`) that is colour times brightness, and
/// can exceed 1.0. A colour picker only edits 0..1, so the inspector shows
/// the two parts separately and writes their product back.
struct LightColor
{
    glm::vec3 color = glm::vec3(1.0f);  ///< Brightest channel is 1.0.
    float brightness = 1.0f;            ///< The brightest channel's value.
};

/// @brief Splits a stored light colour. Black gives white at brightness 0.
LightColor splitLightColor(const glm::vec3& stored);

/// @brief The stored light colour for @p lc.
glm::vec3 combineLightColor(const LightColor& lc);

/// @brief @p shown while it still produces @p stored, else a fresh split.
///
/// Keeps the user's picked colour on screen: a darker colour at a higher
/// brightness stays as picked instead of snapping to full colour.
LightColor reconcileLightColor(const LightColor& shown, const glm::vec3& stored);

} // namespace Vestige

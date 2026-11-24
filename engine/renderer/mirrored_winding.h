// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file mirrored_winding.h
/// @brief Whether a model matrix mirrors what it draws (3D_E-1001).
///
/// A model matrix with a negative determinant mirrors what it draws: a scale
/// of -1 on one axis (or on all three) does it, as does an editor mirror or
/// a glTF node with negative scale. A mirror reverses the order a triangle's
/// corners reach the screen in, so with the renderer's counter-clockwise
/// front, back-face culling would remove the side meant to be seen. A draw
/// with such a matrix sets `glFrontFace(GL_CW)` for its duration.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief True when @p model mirrors what it draws (its 3x3 part has a
///        negative determinant).
inline bool isMirrored(const glm::mat4& model)
{
    return glm::determinant(glm::mat3(model)) < 0.0f;
}

} // namespace Vestige

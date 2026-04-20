// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file color_vision_filter.h
/// @brief Color-vision-deficiency simulation matrices for accessibility.
///
/// Applies a 3x3 linear-RGB transform to post-tonemap color so users with
/// red-green or blue-yellow dichromacy see the scene rendered through the
/// same perceptual projection their eyes perform. Matrices are from
/// Viénot, Brettel, and Mollon (1999), "Digital video colourmaps for
/// checking the legibility of displays by dichromats", Color Research &
/// Application 24(4):243-252 — the canonical game-accessibility reference
/// (Unity, Unreal, and the IGDA GA-SIG recommendations all cite this
/// dataset).
///
/// Applied post-tonemap, pre-gamma — i.e., in the same linear-display-RGB
/// space the tonemapped colour already occupies — so the matrix can be
/// multiplied directly without an LMS round-trip.
#pragma once

#include <glm/mat3x3.hpp>

namespace Vestige
{

/// @brief Color-vision-deficiency simulation modes.
///
/// Each mode simulates the most common dichromacy form; anomalous
/// trichromacy is not separately represented (ship the dichromatic
/// matrices; the accessibility win lives in the end-stop case).
enum class ColorVisionMode
{
    Normal,         ///< No transform applied (identity).
    Protanopia,     ///< L-cone absent — severe red-green confusion.
    Deuteranopia,   ///< M-cone absent — classic red-green confusion.
    Tritanopia,     ///< S-cone absent — blue-yellow confusion.
};

/// @brief Returns the 3x3 RGB simulation matrix for the requested mode.
///
/// Matrix is column-major (glm convention), so the fragment shader
/// multiplies as `out = matrix * colour`.
glm::mat3 colorVisionMatrix(ColorVisionMode mode);

/// @brief Human-readable label for UI settings panels.
const char* colorVisionModeLabel(ColorVisionMode mode);

} // namespace Vestige

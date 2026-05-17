// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file color_grading_presets.h
/// @brief Per-pixel transforms used by the built-in colour-grading
///        LUT presets. Extracted from inline lambdas in
///        `color_grading_lut.cpp::initialize` so unit tests can drive
///        the same code path the LUT builder does.
///
/// /test-audit 2026-05-17 Ts19-AC1: the tests in
/// `tests/test_color_grading.cpp` previously re-implemented these
/// formulas locally — silent drift between test and production was
/// possible. Centralising the formulas removes that risk.
#pragma once

#include <glm/glm.hpp>

namespace Vestige::ColorGradingPresets
{

/// @brief "Warm Golden" — temple interiors. Red bias + slight blue cut
///        + Hermite smoothstep S-curve.
glm::vec3 warmGolden(const glm::vec3& c);

/// @brief "Cool Blue" — night / exterior. Blue bias + shadow lift.
glm::vec3 coolBlue(const glm::vec3& c);

/// @brief "High Contrast" — Hermite smoothstep S-curve per channel.
glm::vec3 highContrast(const glm::vec3& c);

/// @brief "Desaturated" — luminance + 50% blend with the input.
glm::vec3 desaturated(const glm::vec3& c);

}  // namespace Vestige::ColorGradingPresets

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_water_reflection_size.cpp
/// @brief Pins waterReflectionTargetSize (3D_E-0805): the planar-reflection target
///        follows a surface's reflectionResolutionScale instead of a fixed quarter
///        of the render target. Pure — no GL context, no component.

#include "scene/water_surface.h"

#include <gtest/gtest.h>

using namespace Vestige;

// The scale is a fraction of each side of the render target.
TEST(WaterReflectionSize, FollowsTheScale)
{
    EXPECT_EQ(waterReflectionTargetSize(1920, 1080, 0.25f), glm::ivec2(480, 270));
    EXPECT_EQ(waterReflectionTargetSize(1920, 1080, 0.5f), glm::ivec2(960, 540));
    EXPECT_EQ(waterReflectionTargetSize(1920, 1080, 1.0f), glm::ivec2(1920, 1080));
}

// Out-of-range scales clamp to the editor slider's 0.1..1 range.
TEST(WaterReflectionSize, ClampsTheScale)
{
    EXPECT_EQ(waterReflectionTargetSize(1920, 1080, 2.0f), glm::ivec2(1920, 1080));
    EXPECT_EQ(waterReflectionTargetSize(1920, 1080, 0.0f), glm::ivec2(192, 108));
}

// A tiny render target never yields a zero-sized (incomplete) framebuffer.
TEST(WaterReflectionSize, NeverZero)
{
    EXPECT_EQ(waterReflectionTargetSize(2, 2, 0.1f), glm::ivec2(1, 1));
}

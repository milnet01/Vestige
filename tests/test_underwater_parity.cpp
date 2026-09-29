// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_underwater_parity.cpp
/// @brief The underwater view must use the same water-column tint as the surface.
///
/// Regression (3D_E-0724): below the pond surface the view was crystal clear,
/// while the same water seen from above was murky. The final composite now
/// tints every pixel through the water column with `waterColumnTint`, a verbatim
/// copy of the surface shader's function (there is no GLSL #include), so a
/// drift between the two would make the pond look different from each side.
#include "shader_parity_helpers.h"

#include "scene/water_surface.h"

#include <gtest/gtest.h>

#include <string>

namespace Vestige
{
namespace
{

TEST(UnderwaterParity, CompositeTintIsTheSurfaceTintVerbatim)
{
    const std::string surface = ::Vestige::Test::extractGlslFunction(
        ::Vestige::Test::readShaderFile("water.frag.glsl"), "waterColumnTint");
    const std::string composite = ::Vestige::Test::extractGlslFunction(
        ::Vestige::Test::readShaderFile("screen_quad.frag.glsl"), "waterColumnTint");

    ASSERT_FALSE(surface.empty());
    EXPECT_EQ(surface, composite);
}

TEST(UnderwaterParity, CompositeAppliesTheTintWhenUnderwater)
{
    const std::string src = ::Vestige::Test::readShaderFile("screen_quad.frag.glsl");
    const size_t gate = src.find("if (u_underwaterEnabled)");
    ASSERT_NE(gate, std::string::npos) << "composite never tints the underwater view";
    EXPECT_NE(src.find("waterColumnTint(color", gate), std::string::npos);
}

TEST(UnderwaterParity, SurfaceRefractionUsesTheSharedTint)
{
    const std::string src = ::Vestige::Test::readShaderFile("water.frag.glsl");
    EXPECT_NE(src.find("refractionColor = waterColumnTint(refractionColor"),
              std::string::npos);
}

TEST(IsPointUnderwater, BelowTheSurfaceInsideTheFootprint)
{
    const glm::vec2 center(10.0f, -5.0f);
    const glm::vec2 half(4.0f, 4.0f);
    EXPECT_TRUE(isPointUnderwater({10.0f, 1.0f, -5.0f}, 2.0f, center, half));
    EXPECT_TRUE(isPointUnderwater({13.9f, 1.0f, -8.9f}, 2.0f, center, half));
}

TEST(IsPointUnderwater, AboveTheSurfaceOrOutsideTheFootprint)
{
    const glm::vec2 center(10.0f, -5.0f);
    const glm::vec2 half(4.0f, 4.0f);
    EXPECT_FALSE(isPointUnderwater({10.0f, 2.5f, -5.0f}, 2.0f, center, half));
    EXPECT_FALSE(isPointUnderwater({15.0f, 1.0f, -5.0f}, 2.0f, center, half));
    EXPECT_FALSE(isPointUnderwater({10.0f, 1.0f, 0.5f}, 2.0f, center, half));
}

} // namespace
} // namespace Vestige

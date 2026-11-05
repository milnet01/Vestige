// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_foliage_translucency.cpp
/// @brief Pins `grassTranslucency` in grass.frag.glsl (copied in
///        foliage.frag.glsl, kept equal by test_shader_copies_registry.cpp):
///        grass between the camera and a sun above the horizon glows.
///
/// 3D_E-0874 lane 06: the glow was scaled by dot(down, lightDir). lightDir
/// points toward the sun, so that is negative whenever the sun is up and the
/// glow only appeared with the sun below the horizon.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <string>

namespace Vestige::Test
{

class FoliageTranslucencyTest : public GLTestFixture
{
protected:
    static float glow(ShaderProgram& prog, glm::vec3 viewDir, glm::vec3 lightDir)
    {
        return prog.run({{"u_viewDir", glm::normalize(viewDir)},
                         {"u_lightDir", glm::normalize(lightDir)}}).r;
    }
};

TEST_F(FoliageTranslucencyTest, BacklitGrassGlowsOnlyWhileTheSunIsUp)
{
    const std::string fnSrc =
        extractGlslFunction(readShaderFile("grass.frag.glsl"), "grassTranslucency");
    ASSERT_FALSE(fnSrc.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3 u_viewDir;\n"
        "uniform vec3 u_lightDir;\n"
        + fnSrc +
        "void main() { outColor = vec4(grassTranslucency(u_viewDir, u_lightDir)); }\n");
    ASSERT_TRUE(prog.valid());

    // Low afternoon sun to the +Z side; the camera stands on the -Z side of the
    // grass looking toward the sun, so viewDir (fragment -> camera) is -Z.
    const glm::vec3 sun(0.0f, 0.3f, 1.0f);
    EXPECT_GT(glow(prog, {0.0f, -0.1f, -1.0f}, sun), 0.1f)
        << "grass between the camera and a daytime sun must glow";

    // Same sun, camera on the sun's side: the blade is front-lit, no glow.
    EXPECT_FLOAT_EQ(glow(prog, {0.0f, 0.1f, 1.0f}, sun), 0.0f);

    // Sun below the horizon: no sunlight passes through the blades.
    EXPECT_FLOAT_EQ(glow(prog, {0.0f, 0.3f, -1.0f}, {0.0f, -0.3f, 1.0f}), 0.0f);
}

}  // namespace Vestige::Test

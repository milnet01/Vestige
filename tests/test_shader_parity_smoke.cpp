// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_shader_parity_smoke.cpp
/// @brief Smoke tests for the GL-context fixture + shader-parity helper.
///
/// These pin the harness itself (Steps 1 + 2 of the C-full plan) so any
/// future GL-fixture regression is caught before it makes the IBL /
/// bloom / color-grading parity tests look broken. If these skip, every
/// downstream parity test in this build will also skip — that's the
/// design (no GL context = no parity check).

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <glad/gl.h>

namespace Vestige::Test
{

class ShaderParitySmokeTest : public GLTestFixture {};

TEST_F(ShaderParitySmokeTest, GlContextReportsAtLeastVersion4_5)
{
    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    EXPECT_GE(major * 10 + minor, 45)
        << "GLTestEnvironment requested 4.5 core; got " << major << "." << minor;
}

TEST_F(ShaderParitySmokeTest, IdentityShaderRoundTripsInputUniform)
{
    // Echo a constant uniform back through outColor — proves the whole
    // compile / FBO / draw / readback path works end-to-end.
    const std::string fs = R"(#version 450 core
layout(location = 0) out vec4 outColor;
uniform vec4 u_value;
void main() { outColor = u_value; }
)";

    UniformTable uniforms;
    uniforms["u_value"] = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);

    glm::vec4 pixel = runShaderForVec4(fs, uniforms);

    EXPECT_NEAR(pixel.r, 0.25f, 1e-6f);
    EXPECT_NEAR(pixel.g, 0.50f, 1e-6f);
    EXPECT_NEAR(pixel.b, 0.75f, 1e-6f);
    EXPECT_NEAR(pixel.a, 1.00f, 1e-6f);
}

TEST_F(ShaderParitySmokeTest, FloatScalarUniformBindsCorrectly)
{
    const std::string fs = R"(#version 450 core
layout(location = 0) out vec4 outColor;
uniform float u_x;
void main() { outColor = vec4(u_x, u_x * 2.0, u_x * 3.0, 1.0); }
)";

    UniformTable uniforms;
    uniforms["u_x"] = 0.1f;

    glm::vec4 pixel = runShaderForVec4(fs, uniforms);
    EXPECT_NEAR(pixel.r, 0.1f, 1e-6f);
    EXPECT_NEAR(pixel.g, 0.2f, 1e-6f);
    EXPECT_NEAR(pixel.b, 0.3f, 1e-6f);
}

}  // namespace Vestige::Test

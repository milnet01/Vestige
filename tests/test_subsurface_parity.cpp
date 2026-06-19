// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_subsurface_parity.cpp
/// @brief CPU↔GPU numerical parity for the Slice R3 subsurface helpers in
///        `assets/shaders/scene.frag.glsl` (`sssFrontScatter`, `sssBackScatter`).
///
/// Extracts each production helper verbatim and runs it through a 1×1 fragment
/// pass, comparing to the CPU mirror in engine/renderer/subsurface_math.h. The
/// four SSS_* tuning constants are file-scope `const`s in the shader, so they
/// are prepended to the extracted helper here (design §10.2). A drift between
/// the GLSL and the CPU mirror — or in the shared constants — fails the assert.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include "renderer/subsurface_math.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <string>

namespace Vestige::Test
{

namespace
{

// The four file-scope consts the helpers reference (design §10.2). Same literals
// as scene.frag.glsl and subsurface_math.h — prepended so the extracted helper
// compiles standalone.
const std::string kSssConsts =
    "const float SSS_MAX_WRAP   = 0.5;\n"
    "const float SSS_DISTORTION = 0.2;\n"
    "const float SSS_POWER      = 4.0;\n"
    "const float SSS_SCALE      = 1.0;\n";

}  // namespace

class SubsurfaceParityTest : public GLTestFixture {};

// =============================================================================
// sssFrontScatter
// =============================================================================

TEST_F(SubsurfaceParityTest, FrontScatterMatchesCpuIncludingShadowSide)
{
    const std::string fnSrc = extractGlslFunction(
        readShaderFile("scene.frag.glsl"), "sssFrontScatter");
    ASSERT_FALSE(fnSrc.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform float u_rawNdotL;\n"
        "uniform float u_strength;\n"
        "uniform vec3  u_color;\n"
        + kSssConsts + fnSrc +
        "void main() { outColor = vec4(sssFrontScatter(u_rawNdotL, u_strength, u_color), 1.0); }\n");
    ASSERT_TRUE(prog.valid());

    const glm::vec3 color(0.9f, 0.4f, 0.35f);
    // Grid INCLUDES negative N·L so the shadow-side bleed range is exercised.
    for (float strength = 0.0f; strength <= 1.0f; strength += 0.25f)
    {
        for (float ndl = -1.0f; ndl <= 1.0f; ndl += 0.1f)
        {
            glm::vec4 gpu = prog.run({{"u_rawNdotL", ndl},
                                      {"u_strength", strength},
                                      {"u_color", color}});
            glm::vec3 cpu = sssFrontScatter(ndl, strength, color);
            EXPECT_NEAR(gpu.r, cpu.x, 1e-5f) << "s=" << strength << " ndl=" << ndl;
            EXPECT_NEAR(gpu.g, cpu.y, 1e-5f) << "s=" << strength << " ndl=" << ndl;
            EXPECT_NEAR(gpu.b, cpu.z, 1e-5f) << "s=" << strength << " ndl=" << ndl;
        }
    }
}

// =============================================================================
// sssBackScatter
// =============================================================================

TEST_F(SubsurfaceParityTest, BackScatterMatchesCpuAcrossViewAndThickness)
{
    const std::string fnSrc = extractGlslFunction(
        readShaderFile("scene.frag.glsl"), "sssBackScatter");
    ASSERT_FALSE(fnSrc.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3  u_V;\n"
        "uniform vec3  u_L;\n"
        "uniform vec3  u_N;\n"
        "uniform float u_strength;\n"
        "uniform float u_thickness;\n"
        "uniform vec3  u_color;\n"
        "uniform vec3  u_radiance;\n"
        + kSssConsts + fnSrc +
        "void main() { outColor = vec4(sssBackScatter(u_V, u_L, u_N, u_strength, u_thickness, u_color, u_radiance), 1.0); }\n");
    ASSERT_TRUE(prog.valid());

    const glm::vec3 N(0.0f, 0.0f, 1.0f);
    const glm::vec3 L(0.0f, 0.0f, 1.0f);
    const glm::vec3 color(0.8f, 0.5f, 0.45f);
    const glm::vec3 radiance(1.2f, 1.1f, 1.0f);

    struct Case { glm::vec3 V; float thickness; const char* name; };
    const Case cases[] = {
        {{0.0f, 0.0f, -1.0f}, 0.0f, "backlit-thin"},
        {{0.0f, 0.0f, -1.0f}, 0.5f, "backlit-medium"},
        {{0.0f, 0.0f, -1.0f}, 1.0f, "backlit-opaque"},
        {{0.0f, 0.0f,  1.0f}, 0.0f, "front-lit"},
        {{0.7f, 0.0f, -0.7f}, 0.2f, "oblique-back"},
    };
    for (const auto& c : cases)
    {
        glm::vec4 gpu = prog.run({{"u_V", glm::normalize(c.V)}, {"u_L", L}, {"u_N", N},
                                  {"u_strength", 0.9f}, {"u_thickness", c.thickness},
                                  {"u_color", color}, {"u_radiance", radiance}});
        glm::vec3 cpu = sssBackScatter(glm::normalize(c.V), L, N, 0.9f, c.thickness, color, radiance);
        EXPECT_NEAR(gpu.r, cpu.x, 1e-5f) << c.name << ".r";
        EXPECT_NEAR(gpu.g, cpu.y, 1e-5f) << c.name << ".g";
        EXPECT_NEAR(gpu.b, cpu.z, 1e-5f) << c.name << ".b";
    }
}

// =============================================================================
// Equivalence guard (design §10.8 #6): both helpers vanish at strength 0.
// =============================================================================

TEST_F(SubsurfaceParityTest, BothHelpersReturnZeroAtStrengthZero)
{
    const std::string& src = readShaderFile("scene.frag.glsl");
    const std::string front = extractGlslFunction(src, "sssFrontScatter");
    const std::string back  = extractGlslFunction(src, "sssBackScatter");
    ASSERT_FALSE(front.empty());
    ASSERT_FALSE(back.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3 u_V; uniform vec3 u_L; uniform vec3 u_N;\n"
        "uniform float u_thickness; uniform vec3 u_color; uniform vec3 u_radiance;\n"
        "uniform float u_rawNdotL;\n"
        + kSssConsts + front + back +
        "void main() {\n"
        "    vec3 f = sssFrontScatter(u_rawNdotL, 0.0, u_color);\n"
        "    vec3 b = sssBackScatter(u_V, u_L, u_N, 0.0, u_thickness, u_color, u_radiance);\n"
        "    outColor = vec4(f + b, 1.0);\n"
        "}\n");
    ASSERT_TRUE(prog.valid());

    glm::vec4 gpu = prog.run({{"u_V", glm::vec3(0, 0, -1)}, {"u_L", glm::vec3(0, 0, 1)},
                              {"u_N", glm::vec3(0, 0, 1)}, {"u_thickness", 0.0f},
                              {"u_color", glm::vec3(1.0f)}, {"u_radiance", glm::vec3(1.0f)},
                              {"u_rawNdotL", 0.5f}});
    EXPECT_FLOAT_EQ(gpu.r, 0.0f);
    EXPECT_FLOAT_EQ(gpu.g, 0.0f);
    EXPECT_FLOAT_EQ(gpu.b, 0.0f);
}

}  // namespace Vestige::Test

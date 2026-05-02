// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_ibl_parity.cpp
/// @brief CPU↔GPU numerical parity for IBL helpers.
///
/// `tests/test_ibl.cpp` reimplements every IBL helper in C++ and asserts
/// against its own reimplementation — if the GLSL drifts the CPU oracle
/// drifts in lockstep and the regression is invisible. This file closes
/// that gap by extracting each helper's GLSL source verbatim from
/// `assets/shaders/brdf_lut.frag.glsl`, running it via a single-pixel
/// fragment-shader pass, and comparing the output to a CPU implementation
/// of the same formula. If the production GLSL is rewritten with a typo,
/// these tests fail because the CPU reference does not change.
///
/// **Reference for the C++ math**: identical to `tests/test_ibl.cpp`.
/// **Tolerances**: chosen empirically per helper. `radicalInverse_VdC` /
/// `hammersley` are bit-exact integer math + one float multiply; tested
/// at 1e-7. `importanceSampleGGX` chains sqrt + sin + cos + cross +
/// normalize; Mesa is typically within 1 ULP per op so 1e-5 covers
/// chain accumulation. `fresnelSchlickRoughness` uses pow(x, 5.0) which
/// is implemented as exp/log on most drivers; 1e-5 covers it.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <cmath>

namespace Vestige::Test
{

namespace
{

constexpr float PI = 3.14159265359f;

// -----------------------------------------------------------------------
// CPU oracles — formula-identical to assets/shaders/brdf_lut.frag.glsl.
// Kept here (not shared with test_ibl.cpp) so the CPU side is the
// independent comparator, not "the same code computed twice".
// -----------------------------------------------------------------------

float radicalInverse_VdC_cpu(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

glm::vec2 hammersley_cpu(uint32_t i, uint32_t N)
{
    return { static_cast<float>(i) / static_cast<float>(N),
             radicalInverse_VdC_cpu(i) };
}

glm::vec3 importanceSampleGGX_cpu(glm::vec2 Xi, glm::vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0f * PI * Xi.x;
    float cosTheta = std::sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

    glm::vec3 H{ std::cos(phi) * sinTheta,
                 std::sin(phi) * sinTheta,
                 cosTheta };

    glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0,0,1) : glm::vec3(1,0,0);
    glm::vec3 tangent   = glm::normalize(glm::cross(up, N));
    glm::vec3 bitangent = glm::cross(N, tangent);

    return tangent * H.x + bitangent * H.y + N * H.z;
}

float geometrySchlickGGX_cpu(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float geometrySmith_cpu(float NdotV, float NdotL, float roughness)
{
    return geometrySchlickGGX_cpu(NdotV, roughness)
         * geometrySchlickGGX_cpu(NdotL, roughness);
}

glm::vec3 fresnelSchlickRoughness_cpu(float cosTheta, glm::vec3 F0, float roughness)
{
    float x = glm::clamp(1.0f - cosTheta, 0.0f, 1.0f);
    float x2 = x * x;
    return F0 + (glm::max(glm::vec3(1.0f - roughness), F0) - F0) * (x2 * x2 * x);
}

// -----------------------------------------------------------------------
// Cached shader sources (read once per process; constructed lazily so a
// missing file fails inside the test, not at static-init time).
// -----------------------------------------------------------------------

const std::string& brdfLutSrc()
{
    static const std::string s = readShaderFile("brdf_lut.frag.glsl");
    return s;
}

const std::string& sceneFragSrc()
{
    static const std::string s = readShaderFile("scene.frag.glsl");
    return s;
}

}  // namespace

class IBLParityTest : public GLTestFixture {};

// =============================================================================
// radicalInverse_VdC
// =============================================================================

TEST_F(IBLParityTest, RadicalInverseVdCMatchesCpuAcrossSampleIndices)
{
    const std::string fnSrc = extractGlslFunction(brdfLutSrc(), "radicalInverse_VdC");
    ASSERT_FALSE(fnSrc.empty());

    const std::string fs =
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform uint u_i;\n"
        + fnSrc +
        "void main() { outColor = vec4(radicalInverse_VdC(u_i), 0.0, 0.0, 1.0); }\n";

    for (uint32_t i : {0u, 1u, 2u, 7u, 64u, 511u, 1023u})
    {
        UniformTable u{{ "u_i", i }};
        glm::vec4 gpu = runShaderForVec4(fs, u);
        float cpu = radicalInverse_VdC_cpu(i);
        EXPECT_NEAR(gpu.r, cpu, 1e-7f) << "radicalInverse_VdC(" << i << ")";
    }
}

// =============================================================================
// hammersley
// =============================================================================

TEST_F(IBLParityTest, HammersleyMatchesCpuAcrossSamplePoints)
{
    const std::string vdc  = extractGlslFunction(brdfLutSrc(), "radicalInverse_VdC");
    const std::string hamm = extractGlslFunction(brdfLutSrc(), "hammersley");
    ASSERT_FALSE(vdc.empty());
    ASSERT_FALSE(hamm.empty());

    const std::string fs =
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform uint u_i;\n"
        "uniform uint u_N;\n"
        + vdc + hamm +
        "void main() {\n"
        "    vec2 r = hammersley(u_i, u_N);\n"
        "    outColor = vec4(r.x, r.y, 0.0, 1.0);\n"
        "}\n";

    const uint32_t N = 1024u;
    for (uint32_t i : {0u, 1u, 100u, 511u, 1023u})
    {
        UniformTable u{{ "u_i", i }, { "u_N", N }};
        glm::vec4 gpu = runShaderForVec4(fs, u);
        glm::vec2 cpu = hammersley_cpu(i, N);
        EXPECT_NEAR(gpu.r, cpu.x, 1e-7f) << "hammersley(" << i << ").x";
        EXPECT_NEAR(gpu.g, cpu.y, 1e-7f) << "hammersley(" << i << ").y";
    }
}

// =============================================================================
// importanceSampleGGX
// =============================================================================

TEST_F(IBLParityTest, ImportanceSampleGGXMatchesCpuAtRepresentativeInputs)
{
    const std::string fnSrc = extractGlslFunction(brdfLutSrc(), "importanceSampleGGX");
    ASSERT_FALSE(fnSrc.empty());

    // brdf_lut.frag.glsl declares `const float PI = ...` at file scope;
    // the function body references it. Re-declare here so the extracted
    // body has the same lookup environment when isolated.
    const std::string fs =
        "#version 450 core\n"
        "const float PI = 3.14159265359;\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec2  u_Xi;\n"
        "uniform vec3  u_N;\n"
        "uniform float u_roughness;\n"
        + fnSrc +
        "void main() {\n"
        "    vec3 H = importanceSampleGGX(u_Xi, u_N, u_roughness);\n"
        "    outColor = vec4(H, 1.0);\n"
        "}\n";

    struct Case { glm::vec2 Xi; glm::vec3 N; float roughness; const char* name; };
    const Case cases[] = {
        {{0.10f, 0.20f}, {0.0f, 0.0f, 1.0f}, 0.5f, "smooth-up-Z"},
        {{0.50f, 0.50f}, {0.0f, 0.0f, 1.0f}, 0.1f, "midpoint-very-smooth"},
        {{0.75f, 0.25f}, {0.0f, 0.0f, 1.0f}, 0.9f, "rough"},
        {{0.20f, 0.80f}, {1.0f, 0.0f, 0.0f}, 0.3f, "tilted-N"},  // up-fallback branch
    };

    for (const auto& c : cases)
    {
        UniformTable u{
            {"u_Xi",        c.Xi},
            {"u_N",         c.N},
            {"u_roughness", c.roughness},
        };
        glm::vec4 gpu = runShaderForVec4(fs, u);
        glm::vec3 cpu = importanceSampleGGX_cpu(c.Xi, c.N, c.roughness);

        EXPECT_NEAR(gpu.r, cpu.x, 1e-5f) << c.name << ".x";
        EXPECT_NEAR(gpu.g, cpu.y, 1e-5f) << c.name << ".y";
        EXPECT_NEAR(gpu.b, cpu.z, 1e-5f) << c.name << ".z";
    }
}

// =============================================================================
// geometrySchlickGGX + geometrySmith
// =============================================================================

TEST_F(IBLParityTest, GeometrySchlickGGXMatchesCpu)
{
    const std::string fnSrc = extractGlslFunction(brdfLutSrc(), "geometrySchlickGGX");
    ASSERT_FALSE(fnSrc.empty());

    const std::string fs =
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform float u_NdotV;\n"
        "uniform float u_roughness;\n"
        + fnSrc +
        "void main() {\n"
        "    outColor = vec4(geometrySchlickGGX(u_NdotV, u_roughness), 0.0, 0.0, 1.0);\n"
        "}\n";

    struct Case { float NdotV; float roughness; const char* name; };
    const Case cases[] = {
        {1.00f, 0.50f, "head-on"},
        {0.50f, 0.50f, "45deg-medium"},
        {0.05f, 0.30f, "near-grazing-smooth"},
        {0.80f, 0.90f, "rough"},
    };

    for (const auto& c : cases)
    {
        UniformTable u{{"u_NdotV", c.NdotV}, {"u_roughness", c.roughness}};
        glm::vec4 gpu = runShaderForVec4(fs, u);
        float cpu = geometrySchlickGGX_cpu(c.NdotV, c.roughness);
        EXPECT_NEAR(gpu.r, cpu, 1e-6f) << c.name;
    }
}

TEST_F(IBLParityTest, GeometrySmithMatchesCpu)
{
    const std::string sub  = extractGlslFunction(brdfLutSrc(), "geometrySchlickGGX");
    const std::string fnSrc = extractGlslFunction(brdfLutSrc(), "geometrySmith");
    ASSERT_FALSE(sub.empty());
    ASSERT_FALSE(fnSrc.empty());

    const std::string fs =
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform float u_NdotV;\n"
        "uniform float u_NdotL;\n"
        "uniform float u_roughness;\n"
        + sub + fnSrc +
        "void main() {\n"
        "    outColor = vec4(geometrySmith(u_NdotV, u_NdotL, u_roughness), 0.0, 0.0, 1.0);\n"
        "}\n";

    struct Case { float NdotV; float NdotL; float roughness; const char* name; };
    const Case cases[] = {
        {1.00f, 1.00f, 0.50f, "both-head-on"},
        {0.50f, 0.80f, 0.30f, "asymmetric-mid"},
        {0.10f, 0.10f, 0.20f, "both-grazing-smooth"},
        {0.70f, 0.60f, 0.95f, "very-rough"},
    };

    for (const auto& c : cases)
    {
        UniformTable u{{"u_NdotV", c.NdotV},
                       {"u_NdotL", c.NdotL},
                       {"u_roughness", c.roughness}};
        glm::vec4 gpu = runShaderForVec4(fs, u);
        float cpu = geometrySmith_cpu(c.NdotV, c.NdotL, c.roughness);
        EXPECT_NEAR(gpu.r, cpu, 1e-6f) << c.name;
    }
}

// =============================================================================
// fresnelSchlickRoughness (lives in scene.frag.glsl, not brdf_lut.frag.glsl)
// =============================================================================

TEST_F(IBLParityTest, FresnelSchlickRoughnessMatchesCpu)
{
    const std::string fnSrc = extractGlslFunction(sceneFragSrc(), "fresnelSchlickRoughness");
    ASSERT_FALSE(fnSrc.empty());

    const std::string fs =
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform float u_cosTheta;\n"
        "uniform vec3  u_F0;\n"
        "uniform float u_roughness;\n"
        + fnSrc +
        "void main() {\n"
        "    vec3 F = fresnelSchlickRoughness(u_cosTheta, u_F0, u_roughness);\n"
        "    outColor = vec4(F, 1.0);\n"
        "}\n";

    struct Case { float cosTheta; glm::vec3 F0; float roughness; const char* name; };
    const Case cases[] = {
        {1.00f, {0.04f, 0.04f, 0.04f}, 0.50f, "head-on-dielectric"},
        {0.00f, {0.04f, 0.04f, 0.04f}, 0.00f, "grazing-smooth-dielectric"},
        {0.50f, {0.95f, 0.64f, 0.54f}, 0.30f, "midangle-copper-metal"},
        {0.10f, {0.91f, 0.92f, 0.92f}, 0.80f, "rough-aluminium"},
    };

    for (const auto& c : cases)
    {
        UniformTable u{{"u_cosTheta", c.cosTheta},
                       {"u_F0",       c.F0},
                       {"u_roughness", c.roughness}};
        glm::vec4 gpu = runShaderForVec4(fs, u);
        glm::vec3 cpu = fresnelSchlickRoughness_cpu(c.cosTheta, c.F0, c.roughness);

        EXPECT_NEAR(gpu.r, cpu.x, 1e-6f) << c.name << ".r";
        EXPECT_NEAR(gpu.g, cpu.y, 1e-6f) << c.name << ".g";
        EXPECT_NEAR(gpu.b, cpu.z, 1e-6f) << c.name << ".b";
    }
}

}  // namespace Vestige::Test

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_bloom_parity.cpp
/// @brief CPU↔GPU numerical parity for bloom helpers in
///        `assets/shaders/bloom_downsample.frag.glsl`.
///
/// `tests/test_bloom.cpp` reimplements `bt709Luminance` + `brightPass` in
/// C++ and asserts against itself, with no link to the actual shader —
/// the GLSL could be rewritten with wrong constants and the unit tests
/// would stay green. This file closes that gap by extracting each helper
/// from the production shader, running it via a 1×1 fragment-shader
/// pass, and comparing to a CPU oracle implementing the same formula.
///
/// **Note** — `tests/test_bloom.cpp::brightPass` implements a *hard*
/// threshold, while the production shader's bright-pass is *soft* (with
/// a `contrib / (contrib + 1.0)` knee). The CPU helper there is
/// historically wrong — separate finding, not actioned in this slice
/// (per CLAUDE.md global rule 11 — stay in lane). This file's parity
/// test for `softThreshold` uses a fresh CPU port that matches the
/// shader, not the existing C++ helper.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <algorithm>

namespace Vestige::Test
{

namespace
{

// CPU oracles — formula-identical to the post-refactor
// bloom_downsample.frag.glsl helpers. Independent of the (historically
// wrong) brightPass() in tests/test_bloom.cpp.

float bt709Luminance_cpu(glm::vec3 color)
{
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

float karisWeight_cpu(glm::vec3 color)
{
    return 1.0f / (1.0f + bt709Luminance_cpu(color));
}

glm::vec3 softThreshold_cpu(glm::vec3 color, float threshold)
{
    float luma    = bt709Luminance_cpu(color);
    float contrib = std::max(0.0f, luma - threshold);
    contrib       = contrib / (contrib + 1.0f);
    return color * (contrib / (luma + 0.0001f));
}

// shader-source caching now lives in `readShaderFile` (basename-keyed) —
// no need for a per-file static cache here.

}  // namespace

class BloomParityTest : public GLTestFixture {};

// =============================================================================
// bt709Luminance
// =============================================================================

TEST_F(BloomParityTest, Bt709LuminanceMatchesCpuAcrossSpectrum)
{
    const std::string fnSrc = extractGlslFunction(
        readShaderFile("bloom_downsample.frag.glsl"), "bt709Luminance");
    ASSERT_FALSE(fnSrc.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3 u_color;\n"
        + fnSrc +
        "void main() { outColor = vec4(bt709Luminance(u_color), 0.0, 0.0, 1.0); }\n");
    ASSERT_TRUE(prog.valid());

    const glm::vec3 cases[] = {
        {1.0f, 1.0f, 1.0f},   // white -> 1.0
        {0.0f, 0.0f, 0.0f},   // black -> 0
        {1.0f, 0.0f, 0.0f},   // red   -> 0.2126
        {0.0f, 1.0f, 0.0f},   // green -> 0.7152
        {0.0f, 0.0f, 1.0f},   // blue  -> 0.0722
        {0.5f, 0.5f, 0.5f},   // mid grey
        {2.5f, 1.8f, 0.3f},   // HDR-ish bright sample
    };

    for (auto c : cases)
    {
        glm::vec4 gpu = prog.run({{"u_color", c}});
        float cpu = bt709Luminance_cpu(c);
        EXPECT_NEAR(gpu.r, cpu, 1e-6f)
            << "(" << c.x << "," << c.y << "," << c.z << ")";
    }
}

// =============================================================================
// karisWeight (depends on bt709Luminance)
// =============================================================================

TEST_F(BloomParityTest, KarisWeightMatchesCpuAcrossSpectrum)
{
    const std::string& src  = readShaderFile("bloom_downsample.frag.glsl");
    const std::string sub   = extractGlslFunction(src, "bt709Luminance");
    const std::string fnSrc = extractGlslFunction(src, "karisWeight");
    ASSERT_FALSE(sub.empty());
    ASSERT_FALSE(fnSrc.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3 u_color;\n"
        + sub + fnSrc +
        "void main() { outColor = vec4(karisWeight(u_color), 0.0, 0.0, 1.0); }\n");
    ASSERT_TRUE(prog.valid());

    const glm::vec3 cases[] = {
        {0.0f, 0.0f, 0.0f},   // luma 0 -> weight 1.0
        {1.0f, 1.0f, 1.0f},   // luma 1 -> weight 0.5
        {0.5f, 0.5f, 0.5f},
        {2.0f, 2.0f, 2.0f},   // bright firefly
        {10.0f, 10.0f, 10.0f},  // very bright firefly (weight ~ 1/11)
    };

    for (auto c : cases)
    {
        glm::vec4 gpu = prog.run({{"u_color", c}});
        float cpu = karisWeight_cpu(c);
        EXPECT_NEAR(gpu.r, cpu, 1e-6f)
            << "(" << c.x << "," << c.y << "," << c.z << ")";
    }
}

// =============================================================================
// softThreshold (depends on bt709Luminance)
// =============================================================================

TEST_F(BloomParityTest, SoftThresholdMatchesCpuAcrossLumaBands)
{
    const std::string& src  = readShaderFile("bloom_downsample.frag.glsl");
    const std::string sub   = extractGlslFunction(src, "bt709Luminance");
    const std::string fnSrc = extractGlslFunction(src, "softThreshold");
    ASSERT_FALSE(sub.empty());
    ASSERT_FALSE(fnSrc.empty());

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3  u_color;\n"
        "uniform float u_threshold;\n"
        + sub + fnSrc +
        "void main() {\n"
        "    vec3 r = softThreshold(u_color, u_threshold);\n"
        "    outColor = vec4(r, 1.0);\n"
        "}\n");
    ASSERT_TRUE(prog.valid());

    struct Case { glm::vec3 color; float threshold; const char* name; };
    const Case cases[] = {
        {{1.0f, 1.0f, 1.0f}, 0.5f, "above-threshold-grey"},
        {{0.5f, 0.5f, 0.5f}, 1.0f, "below-threshold-grey"},
        {{2.0f, 1.5f, 0.8f}, 1.0f, "above-threshold-warm"},
        {{0.1f, 0.1f, 0.1f}, 0.05f, "barely-above-threshold-dark"},
    };

    for (const auto& c : cases)
    {
        glm::vec4 gpu = prog.run({{"u_color", c.color}, {"u_threshold", c.threshold}});
        glm::vec3 cpu = softThreshold_cpu(c.color, c.threshold);

        EXPECT_NEAR(gpu.r, cpu.x, 1e-5f) << c.name << ".r";
        EXPECT_NEAR(gpu.g, cpu.y, 1e-5f) << c.name << ".g";
        EXPECT_NEAR(gpu.b, cpu.z, 1e-5f) << c.name << ".b";
    }
}

}  // namespace Vestige::Test

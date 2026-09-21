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
/// threshold, while the production shader's bright-pass is *soft* (a
/// quadratic knee across `threshold +/- knee`). The CPU helper there is
/// historically wrong — separate finding, not actioned in this slice
/// (per CLAUDE.md global rule 11 — stay in lane). This file's parity
/// test for `softThreshold` uses a fresh CPU port that matches the
/// shader, not the existing C++ helper.
///
/// That description said "a `contrib / (contrib + 1.0)` knee" until
/// 3D_E-0638. It was not a knee — it saturated, so every luminance more
/// than ~1 above the threshold bloomed identically. Worth noting HOW that
/// survived: these ports are deliberate mirrors of the shader, so the
/// parity test pins CPU and GPU to each OTHER and says nothing about
/// whether the shared formula is right. It agreed with the defect
/// perfectly. A parity test is not a correctness test.

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
    const float kneeFraction = 0.5f;

    float luma = bt709Luminance_cpu(color);
    float knee = threshold * kneeFraction;

    float soft = glm::clamp(luma - threshold + knee, 0.0f, 2.0f * knee);
    soft       = (soft * soft) / (4.0f * knee + 0.0001f);

    float contrib = std::max(soft, luma - threshold);
    contrib       = std::max(contrib, 0.0f);

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
        // 3D_E-0638 — the knee band and well past it. The old saturating
        // form agreed with its CPU mirror here too, so these are parity
        // cases only; SoftThresholdScalesWithBrightness is what pins the
        // behaviour they were chosen to exercise.
        {{0.6f, 0.6f, 0.6f}, 1.0f, "inside-knee-band-below"},
        {{1.4f, 1.4f, 1.4f}, 1.0f, "inside-knee-band-above"},
        {{10.0f, 10.0f, 10.0f}, 1.0f, "far-above-threshold"},
        {{100.0f, 100.0f, 100.0f}, 1.0f, "hdr-highlight"},
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

// =============================================================================
// softThreshold — BEHAVIOUR, not parity (3D_E-0638)
// =============================================================================

// The defect this pins was invisible to every test above, and the reason is
// worth stating: the CPU ports here are deliberate mirrors of the shader, so
// a parity test binds the two implementations to each other and says nothing
// about whether the shared formula is correct. The old saturating form passed
// parity perfectly while making a dim lamp and a blazing highlight bloom
// identically.
//
// So this asserts a PROPERTY instead: bloom contribution must keep rising
// with brightness. Runs on the GPU function, extracted from the shipped
// shader, so it fails if the saturation ever returns.
TEST_F(BloomParityTest, SoftThresholdScalesWithBrightness)
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

    const float threshold = 1.0f;
    auto contributionAt = [&](float level)
    {
        return prog.run({{"u_color", glm::vec3(level)},
                         {"u_threshold", threshold}}).r;
    };

    // Strictly increasing across three decades above the threshold. Under the
    // old `contrib / (contrib + 1.0)` squash these converged instead: every
    // level well above the threshold returned ~1.0.
    const float at2   = contributionAt(2.0f);
    const float at10  = contributionAt(10.0f);
    const float at100 = contributionAt(100.0f);

    EXPECT_GT(at10, at2)
        << "a 10x brighter light must bloom more than a 2x one";
    EXPECT_GT(at100, at10)
        << "a 100x brighter light must bloom more than a 10x one";

    // And the gap must WIDEN, not flatten — the specific signature of the
    // saturating form was that successive decades converged.
    EXPECT_GT(at100 - at10, at10 - at2)
        << "bloom response is flattening with brightness, which is the "
           "saturation 3D_E-0638 removed";

    // The knee spans [threshold - knee, threshold + knee], knee = 0.5 * t.
    // Below the band, nothing blooms at all.
    EXPECT_NEAR(contributionAt(0.4f), 0.0f, 1e-5f)
        << "luminance below threshold - knee must contribute nothing";

    // Inside the band but BELOW the threshold it is already non-zero. That
    // early start is the whole point of a knee — it is what replaces the
    // step a bare `max(0, luma - threshold)` would have at the threshold.
    EXPECT_GT(contributionAt(0.9f), 0.0f)
        << "the knee must begin fading in before the threshold, not at it";

    // At the top of the band the knee meets the linear response exactly, so
    // the curve is continuous where the two pieces join. This is the
    // assertion that would catch a mis-scaled knee: get the 4*knee divisor
    // wrong and the two halves no longer meet.
    const float knee = 0.5f * threshold;
    EXPECT_NEAR(contributionAt(threshold + knee), knee, 1e-4f)
        << "knee and linear must meet at threshold + knee";
}

}  // namespace Vestige::Test

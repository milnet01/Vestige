// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_hdr_nan_guards.cpp
/// @brief The TAA resolve and the bloom downsample write only finite,
///        non-negative values when an input texel is NaN or +Inf
///        (3D_E-0874, lane-06 taa_resolve 170 and bloom_downsample 89).
///
/// One bad scene pixel must not survive these passes: TAA feeds its output
/// back as next frame's history, so a NaN or Inf there spreads every frame,
/// and bloom spreads one over its 13-tap footprint. GLSL does not define
/// max() or clamp() on a NaN, so a guard built only from them depends on the
/// driver. Each case puts one bad texel in the middle of a small image and
/// checks every output pixel.

#include "gl_pass_helpers.h"
#include "gl_test_fixture.h"

#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace
{

using Vestige::Shader;
using Vestige::Test::drawFullScreenRed;
using Vestige::Test::FloatTexture;
using Vestige::Test::GLTestFixture;
using Vestige::Test::passShaderPath;

class HdrNanGuardTest : public GLTestFixture {};

constexpr int kSize = 16;

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

/// A kSize x kSize image of @p fill with @p bad at its centre texel.
std::vector<float> imageWithOneBadTexel(float fill, float bad)
{
    std::vector<float> texels(static_cast<std::size_t>(kSize) * kSize, fill);
    texels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2] = bad;
    return texels;
}

/// Counts output pixels that are NaN, infinite or negative.
int badPixels(const std::vector<float>& out)
{
    int bad = 0;
    for (float v : out)
    {
        if (!std::isfinite(v) || v < 0.0f)
        {
            ++bad;
        }
    }
    return bad;
}

std::vector<float> runTaa(float badCurrentTexel)
{
    Shader shader;
    if (!shader.loadFromFiles(passShaderPath("screen_quad.vert.glsl"),
                              passShaderPath("taa_resolve.frag.glsl")))
    {
        return {};
    }
    const FloatTexture current(kSize, kSize, imageWithOneBadTexel(0.5f, badCurrentTexel));
    const FloatTexture history(kSize, kSize, imageWithOneBadTexel(0.5f, 0.5f));
    const FloatTexture zero(kSize, kSize, imageWithOneBadTexel(0.0f, 0.0f));

    shader.use();
    current.bind(0);
    history.bind(1);
    zero.bind(2);  // no motion
    zero.bind(3);  // cleared normals: the disocclusion test is skipped
    zero.bind(4);
    shader.setInt("u_currentTexture", 0);
    shader.setInt("u_historyTexture", 1);
    shader.setInt("u_motionVectorTexture", 2);
    shader.setInt("u_currentNormal", 3);
    shader.setInt("u_prevNormal", 4);
    shader.setFloat("u_feedbackFactor", 0.9f);
    shader.setVec2("u_texelSize", glm::vec2(1.0f / kSize));
    shader.setFloat("u_disocclusionAlpha", 1.0f);
    return drawFullScreenRed(kSize, kSize);
}

std::vector<float> runBloomDownsample(float badSourceTexel, bool firstDownsample)
{
    Shader shader;
    if (!shader.loadFromFiles(passShaderPath("screen_quad.vert.glsl"),
                              passShaderPath("bloom_downsample.frag.glsl")))
    {
        return {};
    }
    // The source is twice the output size, as in the real mip chain.
    constexpr int kSource = kSize * 2;
    std::vector<float> texels(static_cast<std::size_t>(kSource) * kSource, 2.0f);
    texels[static_cast<std::size_t>(kSource / 2) * kSource + kSource / 2] = badSourceTexel;
    const FloatTexture source(kSource, kSource, texels);

    shader.use();
    source.bind(0);
    shader.setInt("u_sourceTexture", 0);
    shader.setVec2("u_srcTexelSize", glm::vec2(1.0f / kSource));
    shader.setBool("u_useKarisAverage", firstDownsample);
    shader.setFloat("u_threshold", firstDownsample ? 1.0f : 0.0f);
    return drawFullScreenRed(kSize, kSize);
}

}  // namespace

TEST_F(HdrNanGuardTest, TaaResolveWritesNoNanFromANanPixel)
{
    const std::vector<float> out = runTaa(kNan);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kSize) * kSize);
    EXPECT_EQ(badPixels(out), 0) << "NaN, Inf or negative pixels reach the TAA history";
}

TEST_F(HdrNanGuardTest, TaaResolveWritesNoNanFromAnInfinitePixel)
{
    const std::vector<float> out = runTaa(kInf);
    ASSERT_EQ(out.size(), static_cast<std::size_t>(kSize) * kSize);
    EXPECT_EQ(badPixels(out), 0) << "NaN, Inf or negative pixels reach the TAA history";
}

TEST_F(HdrNanGuardTest, BloomDownsampleWritesNoNanFromANanPixel)
{
    for (bool first : {true, false})
    {
        const std::vector<float> out = runBloomDownsample(kNan, first);
        ASSERT_EQ(out.size(), static_cast<std::size_t>(kSize) * kSize);
        EXPECT_EQ(badPixels(out), 0) << "first downsample: " << first;
    }
}

TEST_F(HdrNanGuardTest, BloomDownsampleWritesNoNanFromAnInfinitePixel)
{
    for (bool first : {true, false})
    {
        const std::vector<float> out = runBloomDownsample(kInf, first);
        ASSERT_EQ(out.size(), static_cast<std::size_t>(kSize) * kSize);
        EXPECT_EQ(badPixels(out), 0) << "first downsample: " << first;
    }
}

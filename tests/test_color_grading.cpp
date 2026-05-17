// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_color_grading.cpp
/// @brief Unit tests for color grading LUT math (neutral LUT, preset transforms, .cube parsing).
#include "color_grading_test_helpers.h"
#include "renderer/color_grading_presets.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace Vestige;

// =============================================================================
// Neutral LUT generation (mirroring ColorGradingLut::addGeneratedPreset with identity)
// =============================================================================

// /test-audit 2026-05-17 Ts19-D6: canonical implementation now in
// color_grading_test_helpers.h; this alias keeps existing call-sites
// readable.
static std::vector<unsigned char> generateNeutralLut(int size)
{
    return ::Vestige::Testing::makeNeutralLutBytes(size);
}

TEST(ColorGradingTest, NeutralLutIdentity)
{
    auto data = generateNeutralLut(4);
    // Corner (0,0,0) -> black
    EXPECT_EQ(data[0], 0);
    EXPECT_EQ(data[1], 0);
    EXPECT_EQ(data[2], 0);
    EXPECT_EQ(data[3], 255);

    // Corner (3,3,3) -> white
    size_t lastIdx = (3 * 4 * 4 + 3 * 4 + 3) * 4;
    EXPECT_EQ(data[lastIdx + 0], 255);
    EXPECT_EQ(data[lastIdx + 1], 255);
    EXPECT_EQ(data[lastIdx + 2], 255);
    EXPECT_EQ(data[lastIdx + 3], 255);
}

TEST(ColorGradingTest, NeutralLutSize)
{
    auto data = generateNeutralLut(4);
    EXPECT_EQ(data.size(), static_cast<size_t>(4 * 4 * 4 * 4));

    auto data32 = generateNeutralLut(32);
    EXPECT_EQ(data32.size(), static_cast<size_t>(32 * 32 * 32 * 4));
}

TEST(ColorGradingTest, NeutralLutMidpoint)
{
    auto data = generateNeutralLut(32);
    // Entry (15, 15, 15) should be ~mid-gray
    size_t idx = (15 * 32 * 32 + 15 * 32 + 15) * 4;
    // 15/31 * 255 = ~123.4 -> 123
    EXPECT_NEAR(data[idx + 0], 123, 1);
    EXPECT_NEAR(data[idx + 1], 123, 1);
    EXPECT_NEAR(data[idx + 2], 123, 1);
}

// =============================================================================
// LUT lookup math (mirroring shader with half-texel offset)
// =============================================================================

// /test-audit 2026-05-17 Ts19-D6: canonical implementation now in
// color_grading_test_helpers.h.
static glm::vec3 lutLookup(const std::vector<unsigned char>& data, int size,
                            const glm::vec3& color)
{
    return ::Vestige::Testing::lutLookupNearest(data, size, color);
}

TEST(ColorGradingTest, LutLookupIdentity)
{
    auto neutral = generateNeutralLut(32);
    // Black
    glm::vec3 result = lutLookup(neutral, 32, glm::vec3(0.0f));
    EXPECT_NEAR(result.r, 0.0f, 0.02f);
    EXPECT_NEAR(result.g, 0.0f, 0.02f);
    EXPECT_NEAR(result.b, 0.0f, 0.02f);

    // White
    result = lutLookup(neutral, 32, glm::vec3(1.0f));
    EXPECT_NEAR(result.r, 1.0f, 0.02f);
    EXPECT_NEAR(result.g, 1.0f, 0.02f);
    EXPECT_NEAR(result.b, 1.0f, 0.02f);

    // Mid-gray
    result = lutLookup(neutral, 32, glm::vec3(0.5f));
    EXPECT_NEAR(result.r, 0.5f, 0.02f);
    EXPECT_NEAR(result.g, 0.5f, 0.02f);
    EXPECT_NEAR(result.b, 0.5f, 0.02f);
}

TEST(ColorGradingTest, LutLookupClampsOutOfRange)
{
    auto neutral = generateNeutralLut(32);
    // Values > 1.0 should clamp to 1.0
    glm::vec3 result = lutLookup(neutral, 32, glm::vec3(2.0f, -0.5f, 1.5f));
    EXPECT_NEAR(result.r, 1.0f, 0.02f);
    EXPECT_NEAR(result.g, 0.0f, 0.02f);
    EXPECT_NEAR(result.b, 1.0f, 0.02f);
}

// =============================================================================
// LUT intensity blending (mirroring shader mix)
// =============================================================================

static glm::vec3 lutBlend(const glm::vec3& original, const glm::vec3& graded, float intensity)
{
    return glm::mix(original, graded, intensity);
}

TEST(ColorGradingTest, LutIntensityZeroIsPassthrough)
{
    glm::vec3 original(0.3f, 0.5f, 0.7f);
    glm::vec3 graded(0.8f, 0.2f, 0.1f);
    glm::vec3 result = lutBlend(original, graded, 0.0f);
    EXPECT_FLOAT_EQ(result.r, original.r);
    EXPECT_FLOAT_EQ(result.g, original.g);
    EXPECT_FLOAT_EQ(result.b, original.b);
}

TEST(ColorGradingTest, LutIntensityOneIsFullGraded)
{
    glm::vec3 original(0.3f, 0.5f, 0.7f);
    glm::vec3 graded(0.8f, 0.2f, 0.1f);
    glm::vec3 result = lutBlend(original, graded, 1.0f);
    EXPECT_FLOAT_EQ(result.r, graded.r);
    EXPECT_FLOAT_EQ(result.g, graded.g);
    EXPECT_FLOAT_EQ(result.b, graded.b);
}

TEST(ColorGradingTest, LutIntensityHalfIsBlend)
{
    glm::vec3 original(0.0f, 0.0f, 0.0f);
    glm::vec3 graded(1.0f, 1.0f, 1.0f);
    glm::vec3 result = lutBlend(original, graded, 0.5f);
    EXPECT_FLOAT_EQ(result.r, 0.5f);
    EXPECT_FLOAT_EQ(result.g, 0.5f);
    EXPECT_FLOAT_EQ(result.b, 0.5f);
}

// =============================================================================
// Built-in preset transforms (mirroring ColorGradingLut::initialize)
// =============================================================================

// /test-audit 2026-05-17 Ts19-AC1: these tests previously re-implemented
// the warm/cool/contrast/desaturate formulas locally — a refactor in the
// production lambdas in color_grading_lut.cpp would have left the tests
// silently passing on the old formula. Now they drive
// `Vestige::ColorGradingPresets::*` directly, the same functions
// `addGeneratedPreset` uses, so test/production drift is structurally
// impossible.

TEST(ColorGradingTest, WarmPresetIncreasesRed)
{
    glm::vec3 gray(0.5f, 0.5f, 0.5f);
    glm::vec3 result = ColorGradingPresets::warmGolden(gray);
    EXPECT_GT(result.r, result.b);
}

TEST(ColorGradingTest, CoolPresetIncreasesBlue)
{
    glm::vec3 gray(0.5f, 0.5f, 0.5f);
    glm::vec3 result = ColorGradingPresets::coolBlue(gray);
    EXPECT_GT(result.b, result.r);
}

TEST(ColorGradingTest, HighContrastDarkensShadows)
{
    glm::vec3 dark(0.2f, 0.2f, 0.2f);
    glm::vec3 result = ColorGradingPresets::highContrast(dark);
    EXPECT_LT(result.r, 0.2f);
}

TEST(ColorGradingTest, HighContrastBrightensHighlights)
{
    glm::vec3 bright(0.8f, 0.8f, 0.8f);
    glm::vec3 result = ColorGradingPresets::highContrast(bright);
    EXPECT_GT(result.r, 0.8f);
}

TEST(ColorGradingTest, DesaturatedReducesSaturation)
{
    glm::vec3 red(1.0f, 0.0f, 0.0f);
    glm::vec3 result = ColorGradingPresets::desaturated(red);
    EXPECT_LT(result.r, 1.0f);
    EXPECT_GT(result.g, 0.0f);
    EXPECT_GT(result.b, 0.0f);
}

// =============================================================================
// .cube file parsing (mirroring CubeLoader::load)
// =============================================================================

// Minimal parser for test purposes (matches CubeLoader logic)
struct TestCubeData
{
    int size = 0;
    std::vector<unsigned char> rgbaData;
};

static TestCubeData parseCubeString(const std::string& content)
{
    TestCubeData result;
    std::istringstream stream(content);

    int lutSize = 0;
    std::vector<glm::vec3> entries;
    std::string line;

    while (std::getline(stream, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        // rfind(x, 0) == 0 is the C++17-compatible equivalent of starts_with()
        // — short-circuits at position 0 instead of scanning the whole string
        // (cppcheck: stlIfStrFind).
        if (line.rfind("LUT_3D_SIZE", 0) == 0)
        {
            std::istringstream iss(line.substr(12));
            iss >> lutSize;
            if (lutSize < 2 || lutSize > 128)
            {
                return TestCubeData{};
            }
            continue;
        }
        if (line.rfind("TITLE", 0) == 0 || line.rfind("DOMAIN", 0) == 0)
        {
            continue;
        }
        if (lutSize == 0)
        {
            continue;
        }

        std::istringstream iss(line);
        float r, g, b;
        if (!(iss >> r >> g >> b))
        {
            continue;
        }
        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);
        entries.emplace_back(r, g, b);
    }

    if (lutSize == 0 || static_cast<int>(entries.size()) != lutSize * lutSize * lutSize)
    {
        return TestCubeData{};
    }

    result.size = lutSize;
    result.rgbaData.resize(entries.size() * 4);
    for (size_t i = 0; i < entries.size(); i++)
    {
        result.rgbaData[i * 4 + 0] = static_cast<unsigned char>(entries[i].r * 255.0f + 0.5f);
        result.rgbaData[i * 4 + 1] = static_cast<unsigned char>(entries[i].g * 255.0f + 0.5f);
        result.rgbaData[i * 4 + 2] = static_cast<unsigned char>(entries[i].b * 255.0f + 0.5f);
        result.rgbaData[i * 4 + 3] = 255;
    }
    return result;
}

TEST(ColorGradingTest, CubeParserValidFile)
{
    // A minimal 2x2x2 .cube file
    std::string cube =
        "LUT_3D_SIZE 2\n"
        "0.0 0.0 0.0\n"
        "1.0 0.0 0.0\n"
        "0.0 1.0 0.0\n"
        "1.0 1.0 0.0\n"
        "0.0 0.0 1.0\n"
        "1.0 0.0 1.0\n"
        "0.0 1.0 1.0\n"
        "1.0 1.0 1.0\n";

    auto data = parseCubeString(cube);
    EXPECT_EQ(data.size, 2);
    EXPECT_EQ(data.rgbaData.size(), static_cast<size_t>(2 * 2 * 2 * 4));

    // First entry: (0,0,0) -> black
    EXPECT_EQ(data.rgbaData[0], 0);
    EXPECT_EQ(data.rgbaData[1], 0);
    EXPECT_EQ(data.rgbaData[2], 0);

    // Last entry: (1,1,1) -> white
    size_t last = (2 * 2 * 2 - 1) * 4;
    EXPECT_EQ(data.rgbaData[last + 0], 255);
    EXPECT_EQ(data.rgbaData[last + 1], 255);
    EXPECT_EQ(data.rgbaData[last + 2], 255);
}

TEST(ColorGradingTest, CubeParserCommentSkip)
{
    std::string cube =
        "# This is a comment\n"
        "LUT_3D_SIZE 2\n"
        "# Another comment\n"
        "0.0 0.0 0.0\n"
        "1.0 0.0 0.0\n"
        "0.0 1.0 0.0\n"
        "1.0 1.0 0.0\n"
        "0.0 0.0 1.0\n"
        "1.0 0.0 1.0\n"
        "0.0 1.0 1.0\n"
        "1.0 1.0 1.0\n";

    auto data = parseCubeString(cube);
    EXPECT_EQ(data.size, 2);
}

TEST(ColorGradingTest, CubeParserInvalidSize)
{
    std::string cube = "LUT_3D_SIZE 0\n";
    auto data = parseCubeString(cube);
    EXPECT_EQ(data.size, 0);
}

TEST(ColorGradingTest, CubeParserWrongEntryCount)
{
    std::string cube =
        "LUT_3D_SIZE 2\n"
        "0.0 0.0 0.0\n"
        "1.0 1.0 1.0\n";  // Only 2 entries, need 8

    auto data = parseCubeString(cube);
    EXPECT_EQ(data.size, 0);
}

TEST(ColorGradingTest, CubeParserClampsValues)
{
    std::string cube =
        "LUT_3D_SIZE 2\n"
        "1.5 -0.5 0.5\n"
        "1.0 0.0 0.0\n"
        "0.0 1.0 0.0\n"
        "1.0 1.0 0.0\n"
        "0.0 0.0 1.0\n"
        "1.0 0.0 1.0\n"
        "0.0 1.0 1.0\n"
        "1.0 1.0 1.0\n";

    auto data = parseCubeString(cube);
    EXPECT_EQ(data.size, 2);
    // First entry: 1.5 clamped to 1.0, -0.5 clamped to 0.0
    EXPECT_EQ(data.rgbaData[0], 255);
    EXPECT_EQ(data.rgbaData[1], 0);
}

// =============================================================================
// Half-texel offset correctness
// =============================================================================

// Phase 10.9 Slice 18 Ts1 cleanup: dropped
// `HalfTexelOffsetBlackMapsToOrigin` and `HalfTexelOffsetWhiteMapsToEnd`
// — each computed a constant and asserted it equalled the same
// constant (`0 * 31/32 + 0.5/32 == 0.5/32`, `1 * 31/32 + 0.5/32 ==
// 31.5/32`). No SUT was exercised. The half-texel offset used by the
// production sampler is pinned by `test_color_grading_parity.cpp`'s
// identity-round-trip.

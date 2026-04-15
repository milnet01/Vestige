// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_color_grading.cpp
/// @brief Unit tests for color grading LUT math (neutral LUT, preset transforms, .cube parsing).
#include <gtest/gtest.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

// =============================================================================
// Neutral LUT generation (mirroring ColorGradingLut::addGeneratedPreset with identity)
// =============================================================================

static std::vector<unsigned char> generateNeutralLut(int size)
{
    std::vector<unsigned char> data(static_cast<size_t>(size * size * size) * 4);
    float maxIdx = static_cast<float>(size - 1);

    for (int b = 0; b < size; b++)
    {
        for (int g = 0; g < size; g++)
        {
            for (int r = 0; r < size; r++)
            {
                size_t idx = static_cast<size_t>((b * size * size + g * size + r)) * 4;
                data[idx + 0] = static_cast<unsigned char>(static_cast<float>(r) / maxIdx * 255.0f + 0.5f);
                data[idx + 1] = static_cast<unsigned char>(static_cast<float>(g) / maxIdx * 255.0f + 0.5f);
                data[idx + 2] = static_cast<unsigned char>(static_cast<float>(b) / maxIdx * 255.0f + 0.5f);
                data[idx + 3] = 255;
            }
        }
    }
    return data;
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

static glm::vec3 lutLookup(const std::vector<unsigned char>& data, int size,
                            const glm::vec3& color)
{
    glm::vec3 c = glm::clamp(color, 0.0f, 1.0f);

    // Half-texel offset for correct 3D LUT sampling
    float s = static_cast<float>(size);
    glm::vec3 coord = c * ((s - 1.0f) / s) + glm::vec3(0.5f / s);

    // Nearest-neighbor lookup (GPU does trilinear, but for testing nearest is fine)
    int r = static_cast<int>(coord.r * static_cast<float>(size - 1) + 0.5f);
    int g = static_cast<int>(coord.g * static_cast<float>(size - 1) + 0.5f);
    int b = static_cast<int>(coord.b * static_cast<float>(size - 1) + 0.5f);
    r = std::clamp(r, 0, size - 1);
    g = std::clamp(g, 0, size - 1);
    b = std::clamp(b, 0, size - 1);

    size_t idx = static_cast<size_t>((b * size * size + g * size + r)) * 4;
    return glm::vec3(
        static_cast<float>(data[idx + 0]) / 255.0f,
        static_cast<float>(data[idx + 1]) / 255.0f,
        static_cast<float>(data[idx + 2]) / 255.0f);
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

static glm::vec3 warmTransform(const glm::vec3& c)
{
    glm::vec3 out;
    out.r = std::clamp(c.r * 1.05f + 0.02f, 0.0f, 1.0f);
    out.g = c.g;
    out.b = std::clamp(c.b * 0.85f - 0.02f, 0.0f, 1.0f);
    out = out * out * (3.0f - 2.0f * out);
    return out;
}

static glm::vec3 coolTransform(const glm::vec3& c)
{
    glm::vec3 out;
    out.r = std::clamp(c.r * 0.85f, 0.0f, 1.0f);
    out.g = std::clamp(c.g * 0.95f + 0.02f, 0.0f, 1.0f);
    out.b = std::clamp(c.b * 1.1f + 0.03f, 0.0f, 1.0f);
    out += glm::vec3(0.02f);
    out = glm::clamp(out, 0.0f, 1.0f);
    return out;
}

static glm::vec3 contrastTransform(const glm::vec3& c)
{
    return c * c * (3.0f - 2.0f * c);
}

static glm::vec3 desaturateTransform(const glm::vec3& c)
{
    float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    return glm::mix(glm::vec3(lum), c, 0.5f);
}

TEST(ColorGradingTest, WarmPresetIncreasesRed)
{
    glm::vec3 gray(0.5f, 0.5f, 0.5f);
    glm::vec3 result = warmTransform(gray);
    EXPECT_GT(result.r, result.b);
}

TEST(ColorGradingTest, CoolPresetIncreasesBlue)
{
    glm::vec3 gray(0.5f, 0.5f, 0.5f);
    glm::vec3 result = coolTransform(gray);
    EXPECT_GT(result.b, result.r);
}

TEST(ColorGradingTest, HighContrastDarkensShadows)
{
    glm::vec3 dark(0.2f, 0.2f, 0.2f);
    glm::vec3 result = contrastTransform(dark);
    EXPECT_LT(result.r, 0.2f);
}

TEST(ColorGradingTest, HighContrastBrightensHighlights)
{
    glm::vec3 bright(0.8f, 0.8f, 0.8f);
    glm::vec3 result = contrastTransform(bright);
    EXPECT_GT(result.r, 0.8f);
}

TEST(ColorGradingTest, DesaturatedReducesSaturation)
{
    glm::vec3 red(1.0f, 0.0f, 0.0f);
    glm::vec3 result = desaturateTransform(red);
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
        if (line.find("LUT_3D_SIZE") == 0)
        {
            std::istringstream iss(line.substr(12));
            iss >> lutSize;
            if (lutSize < 2 || lutSize > 128)
            {
                return TestCubeData{};
            }
            continue;
        }
        if (line.find("TITLE") == 0 || line.find("DOMAIN") == 0)
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

TEST(ColorGradingTest, HalfTexelOffsetBlackMapsToOrigin)
{
    // For input 0.0, the LUT coordinate should be 0.5/32 = 0.015625
    // This maps to texel index ~0, which is the black corner of a neutral LUT
    float lutSize = 32.0f;
    float coord = 0.0f * ((lutSize - 1.0f) / lutSize) + (0.5f / lutSize);
    EXPECT_NEAR(coord, 0.5f / 32.0f, 0.0001f);
}

TEST(ColorGradingTest, HalfTexelOffsetWhiteMapsToEnd)
{
    // For input 1.0, the LUT coordinate should be 31.5/32 = 0.984375
    float lutSize = 32.0f;
    float coord = 1.0f * ((lutSize - 1.0f) / lutSize) + (0.5f / lutSize);
    EXPECT_NEAR(coord, 31.5f / 32.0f, 0.0001f);
}

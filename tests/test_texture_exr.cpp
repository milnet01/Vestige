// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_texture_exr.cpp
/// @brief Pins the CPU half of EXR loading, Texture::decodeExr (3D_E-0686).
///
/// Texture::loadFromExr is the only consumer of tinyexr, and before this test
/// nothing opened an .exr at all, so a decode change on a tinyexr bump would
/// have surfaced only as a wrong-looking HDRI. The fixture is a committed 2x3
/// RGBA fp32 image written by tinyexr v1.0.13, with distinct values above 1.0
/// and non-trivial alpha, all exactly representable, so the comparison is
/// exact. Needs no GL context.

#include "renderer/texture.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

using Vestige::Texture;

namespace
{

const std::string kFixture = std::string(VESTIGE_TEXTURE_FIXTURES_DIR) + "/hdr_2x3.exr";

// The fixture's pixels as authored, TOP row first, RGBA.
constexpr std::array<std::array<float, 8>, 3> kTopDownRows = {{
    {0.25f, 0.5f, 1.0f, 1.0f, 2.0f, 4.0f, 8.0f, 0.5f},
    {16.0f, 0.125f, 0.0f, 1.0f, 1.5f, 3.0f, 6.0f, 0.25f},
    {0.75f, 0.0625f, 32.0f, 1.0f, 64.0f, 0.5f, 0.25f, 0.75f},
}};

}  // namespace

TEST(TextureExr, DecodesFixtureExactlyAndFlipsRowsForOpenGL)
{
    int width = 0;
    int height = 0;
    std::vector<float> rgba;
    ASSERT_TRUE(Texture::decodeExr(kFixture, width, height, rgba));
    ASSERT_EQ(width, 2);
    ASSERT_EQ(height, 3);
    ASSERT_EQ(rgba.size(), 2u * 3u * 4u);

    // OpenGL's row 0 is the bottom of the image, so output row r holds
    // authored row (height - 1 - r).
    for (int r = 0; r < height; ++r)
    {
        const auto& expected = kTopDownRows.at(static_cast<size_t>(height - 1 - r));
        for (size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(rgba.at(static_cast<size_t>(r) * 8 + i), expected.at(i))
                << "output row " << r << ", component " << i;
        }
    }
}

TEST(TextureExr, RejectsMissingFile)
{
    int width = 0;
    int height = 0;
    std::vector<float> rgba;
    EXPECT_FALSE(Texture::decodeExr(kFixture + ".missing", width, height, rgba));
    EXPECT_TRUE(rgba.empty());
}

TEST(TextureExr, RejectsNonExrFile)
{
    int width = 0;
    int height = 0;
    std::vector<float> rgba;
    // Any committed non-EXR file will do; this test file's own source is one.
    EXPECT_FALSE(Texture::decodeExr(std::string(VESTIGE_TEXTURE_FIXTURES_DIR)
                                        + "/../../test_texture_exr.cpp",
                                    width, height, rgba));
    EXPECT_TRUE(rgba.empty());
}

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_skybox_equirect_size.cpp
/// @brief 3D_E-0874 lane-04 — Skybox::loadEquirectangular refuses an image
///        whose header size it cannot convert, and keeps the sky it had.
///
/// The on-disk cap alone did not bound the decoded size, and a one-row image
/// gave a cubemap face size of zero: the load reported success with a
/// texture that had no storage, after deleting the old sky.

#include "gl_test_fixture.h"
#include "test_helpers.h"

#include "renderer/skybox.h"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <stb_image_write.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vestige::Test
{

namespace
{

/// Writes a grey single-channel PNG of the given size and returns its path.
std::string writeGreyPng(const std::string& name, int w, int h)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h), 128);
    const std::string path = (std::filesystem::temp_directory_path()
        / ("vestige_equirect_" + name + "_" + Testing::vestigeTestStamp() + ".png")).string();
    EXPECT_NE(stbi_write_png(path.c_str(), w, h, 1, pixels.data(), w), 0);
    return path;
}

}  // namespace

class SkyboxEquirectSizeTest : public GLTestFixture {};

TEST_F(SkyboxEquirectSizeTest, OneRowImageIsRefusedAndTheOldSkyKept)
{
    const std::string good = writeGreyPng("good", 8, 4);
    const std::string oneRow = writeGreyPng("one_row", 8, 1);

    Skybox skybox;
    ASSERT_TRUE(skybox.loadEquirectangular(good));
    const GLuint before = skybox.getTextureId();

    EXPECT_FALSE(skybox.loadEquirectangular(oneRow));
    EXPECT_TRUE(skybox.hasTexture());
    EXPECT_EQ(skybox.getTextureId(), before) << "a refused load replaced the sky";

    std::filesystem::remove(good);
    std::filesystem::remove(oneRow);
}

TEST_F(SkyboxEquirectSizeTest, ImageWiderThanTheTextureLimitIsRefused)
{
    // Compresses to a few kilobytes, so the on-disk cap never sees it.
    const std::string wide = writeGreyPng("wide", 16385, 2);

    Skybox skybox;
    EXPECT_FALSE(skybox.loadEquirectangular(wide));
    EXPECT_FALSE(skybox.hasTexture());

    std::filesystem::remove(wide);
}

}  // namespace Vestige::Test

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_texture_upload.cpp
/// @brief 3D_E-0870 — every 8-bit Texture load path uploads rows that are
///        not 4-byte aligned without shearing them.
///
/// A 3-pixel-wide RGB image has 9-byte rows. Uploaded at the default
/// GL_UNPACK_ALIGNMENT of 4, each row after the first starts 3 bytes late,
/// which banded the terrain diagonally (82d0738). Reads each texture back
/// and compares it with the source, bottom row first (Texture flips images
/// for GL).
#include "gl_test_fixture.h"
#include "renderer/texture.h"
#include "test_helpers.h"

#include <glad/gl.h>
#include <gtest/gtest.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

constexpr int kW = 3;  // 9-byte RGB rows: not a multiple of 4
constexpr int kH = 3;
constexpr int kChannels = 3;

class TextureUploadTest : public GLTestFixture {};

/// Distinct bytes, so a shifted row cannot match by accident.
std::vector<uint8_t> sourcePixels()
{
    std::vector<uint8_t> pixels(static_cast<size_t>(kW * kH * kChannels));
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        pixels[i] = static_cast<uint8_t>(10 + i * 7);
    }
    return pixels;
}

/// The source with its rows in reverse order, as Texture stores it.
std::vector<uint8_t> flippedRows(const std::vector<uint8_t>& src)
{
    const size_t row = static_cast<size_t>(kW * kChannels);
    std::vector<uint8_t> out(src.size());
    for (size_t y = 0; y < static_cast<size_t>(kH); ++y)
    {
        std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(y * row), row,
                    out.begin() + static_cast<std::ptrdiff_t>((kH - 1 - y) * row));
    }
    return out;
}

std::vector<uint8_t> readBack(const Texture& texture)
{
    std::vector<uint8_t> out(static_cast<size_t>(kW * kH * kChannels), 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(texture.getId(), 0, GL_RGB, GL_UNSIGNED_BYTE,
                      static_cast<GLsizei>(out.size()), out.data());
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    return out;
}

std::string writePng(const std::vector<uint8_t>& pixels)
{
    const std::string path = (std::filesystem::temp_directory_path()
        / ("vestige_texture_upload_" + Testing::vestigeTestStamp() + ".png")).string();
    EXPECT_NE(stbi_write_png(path.c_str(), kW, kH, kChannels, pixels.data(), kW * kChannels), 0);
    return path;
}

} // namespace

TEST_F(TextureUploadTest, RawRgbWithUnalignedRowsReadsBackIntact)
{
    const auto src = sourcePixels();
    Texture texture;
    ASSERT_TRUE(texture.loadFromMemory(src.data(), kW, kH, kChannels, /*linear=*/true));
    EXPECT_EQ(readBack(texture), flippedRows(src));
}

TEST_F(TextureUploadTest, PngFileWithUnalignedRowsReadsBackIntact)
{
    const auto src = sourcePixels();
    const std::string path = writePng(src);
    Texture texture;
    ASSERT_TRUE(texture.loadFromFile(path, /*linear=*/true));
    EXPECT_EQ(readBack(texture), flippedRows(src));
    std::filesystem::remove(path);
}

TEST_F(TextureUploadTest, PngInMemoryWithUnalignedRowsReadsBackIntact)
{
    const auto src = sourcePixels();
    const std::string path = writePng(src);
    std::vector<unsigned char> bytes;
    {
        // Closed before the remove below: Windows refuses to delete an open file.
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good());
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    Texture texture;
    ASSERT_TRUE(texture.loadFromMemory(bytes.data(), bytes.size(), /*linear=*/true));
    EXPECT_EQ(readBack(texture), flippedRows(src));
    std::filesystem::remove(path);
}

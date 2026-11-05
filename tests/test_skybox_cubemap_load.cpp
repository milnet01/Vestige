// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_skybox_cubemap_load.cpp
/// @brief 3D_E-0825 — Skybox::loadCubemap must expand 1- and 2-channel faces
///        to RGBA and refuse faces whose size differs from the first.

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

constexpr int kFaceW = 3;  // odd width: an RGB upload would be misaligned
constexpr int kFaceH = 3;  // square: GL needs a complete cube

/// Writes a grey single-channel PNG whose pixels are base, base+1, ...
std::string writeGreyFace(const std::string& name, int w, int h, uint8_t base)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(w * h));
    for (size_t i = 0; i < pixels.size(); ++i)
    {
        pixels[i] = static_cast<uint8_t>(base + i);
    }
    const std::string path = (std::filesystem::temp_directory_path()
        / ("vestige_cube_" + name + "_" + Testing::vestigeTestStamp() + ".png")).string();
    EXPECT_NE(stbi_write_png(path.c_str(), w, h, 1, pixels.data(), w), 0);
    return path;
}

void removeAll(const std::vector<std::string>& paths)
{
    for (const auto& p : paths)
    {
        std::filesystem::remove(p);
    }
}

}  // namespace

class SkyboxCubemapLoadTest : public GLTestFixture {};

TEST_F(SkyboxCubemapLoadTest, GreyFacesReadBackAsGrey)
{
    std::vector<std::string> faces;
    for (int f = 0; f < 6; ++f)
    {
        faces.push_back(writeGreyFace("f" + std::to_string(f), kFaceW, kFaceH,
                                      static_cast<uint8_t>(10 + f * 20)));
    }

    Skybox skybox;
    ASSERT_TRUE(skybox.loadCubemap(faces));

    constexpr size_t kFaceBytes = static_cast<size_t>(kFaceW * kFaceH * 4);
    std::vector<uint8_t> all(kFaceBytes * 6, 0);
    while (glGetError() != GL_NO_ERROR) {}  // drop errors from before the read
    glGetTextureImage(skybox.getTextureId(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      static_cast<GLsizei>(all.size()), all.data());
    ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    for (int f = 0; f < 6; ++f)
    {
        const uint8_t* rgba = all.data() + static_cast<size_t>(f) * kFaceBytes;
        for (int i = 0; i < kFaceW * kFaceH; ++i)
        {
            const uint8_t grey = static_cast<uint8_t>(10 + f * 20 + i);
            EXPECT_EQ(rgba[i * 4 + 0], grey) << "face " << f << " px " << i;
            EXPECT_EQ(rgba[i * 4 + 1], grey) << "face " << f << " px " << i;
            EXPECT_EQ(rgba[i * 4 + 2], grey) << "face " << f << " px " << i;
        }
    }
    removeAll(faces);
}

TEST_F(SkyboxCubemapLoadTest, RejectsFaceWithDifferentSize)
{
    std::vector<std::string> faces;
    for (int f = 0; f < 6; ++f)
    {
        const int w = (f == 3) ? kFaceW + 1 : kFaceW;
        faces.push_back(writeGreyFace("m" + std::to_string(f), w, kFaceH, 5));
    }

    Skybox skybox;
    EXPECT_FALSE(skybox.loadCubemap(faces));
    removeAll(faces);
}


// 3D_E-0874 lane 04: a second loadCubemap created a new texture without
// deleting the first, leaking it on every sky change.
TEST_F(SkyboxCubemapLoadTest, ReloadDeletesThePreviousCubemap)
{
    std::vector<std::string> faces;
    for (int f = 0; f < 6; ++f)
    {
        faces.push_back(writeGreyFace("r" + std::to_string(f), kFaceW, kFaceH, 9));
    }

    Skybox skybox;
    ASSERT_TRUE(skybox.loadCubemap(faces));
    const GLuint first = skybox.getTextureId();
    ASSERT_TRUE(skybox.loadCubemap(faces));
    EXPECT_NE(skybox.getTextureId(), 0u);
    EXPECT_FALSE(glIsTexture(first) && first != skybox.getTextureId())
        << "the first cubemap is still alive after the reload";
    removeAll(faces);
}

// A reload that fails part way must leave the sky it had, not report a texture
// while holding none.
TEST_F(SkyboxCubemapLoadTest, FailedReloadKeepsThePreviousSky)
{
    std::vector<std::string> good;
    std::vector<std::string> bad;
    for (int f = 0; f < 6; ++f)
    {
        good.push_back(writeGreyFace("g" + std::to_string(f), kFaceW, kFaceH, 9));
        bad.push_back(writeGreyFace("b" + std::to_string(f),
                                    (f == 4) ? kFaceW + 1 : kFaceW, kFaceH, 9));
    }

    Skybox skybox;
    ASSERT_TRUE(skybox.loadCubemap(good));
    const GLuint before = skybox.getTextureId();
    EXPECT_FALSE(skybox.loadCubemap(bad));
    EXPECT_TRUE(skybox.hasTexture());
    EXPECT_EQ(skybox.getTextureId(), before);
    EXPECT_TRUE(glIsTexture(skybox.getTextureId())) << "hasTexture() is true but the texture is gone";
    removeAll(good);
    removeAll(bad);
}

}  // namespace Vestige::Test

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_skybox_hdr_range.cpp
/// @brief 3D_E-0772 — the visible sky keeps an HDRI's sun at full brightness,
///        while the IBL capture still caps it.
///
/// The meadow HDRI's sun peaks near 60,000. Skybox once capped every texel at
/// 50 to keep the sun out of the image-based lighting, which also flattened
/// the visible sun into the bright clouds around it. The cap now lives in the
/// IBL capture only. These tests load a small synthetic equirect with one
/// bright block and read back both cubemaps.

#include "gl_test_fixture.h"
#include "test_helpers.h"

#include "renderer/environment_map.h"
#include "renderer/fullscreen_quad.h"
#include "renderer/shader.h"
#include "renderer/skybox.h"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <stb_image_write.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Vestige::Test
{

namespace
{

constexpr int kWidth = 64;
constexpr int kHeight = 32;
constexpr float kSkyValue = 0.5f;
constexpr float kSunValue = 5000.0f;

/// Writes a 64x32 equirect, all kSkyValue except a 4x4 block of kSunValue
/// above the horizon, and returns its path.
std::string writeSunEquirect()
{
    std::vector<float> pixels(static_cast<size_t>(kWidth * kHeight * 3), kSkyValue);
    for (int y = 8; y < 12; ++y)
    {
        for (int x = 30; x < 34; ++x)
        {
            const auto i = (static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)) * 3;
            pixels[i + 0] = kSunValue;
            pixels[i + 1] = kSunValue;
            pixels[i + 2] = kSunValue;
        }
    }
    // Per-test name: ctest runs shards in parallel, and Windows refuses to
    // delete a file another shard still has open.
    const std::string path = (std::filesystem::temp_directory_path()
        / ("vestige_sun_equirect_" + Testing::vestigeTestStamp() + ".hdr")).string();
    EXPECT_NE(stbi_write_hdr(path.c_str(), kWidth, kHeight, 3, pixels.data()), 0);
    return path;
}

/// Largest channel value in mip 0 of a GL_RGB16F cubemap.
float cubemapMax(GLuint cubemap)
{
    GLint size = 0;
    glGetTextureLevelParameteriv(cubemap, 0, GL_TEXTURE_WIDTH, &size);
    std::vector<float> texels(static_cast<size_t>(size * size * 3 * 6));
    glGetTextureImage(cubemap, 0, GL_RGB, GL_FLOAT,
                      static_cast<GLsizei>(texels.size() * sizeof(float)), texels.data());
    return *std::max_element(texels.begin(), texels.end());
}

}  // namespace

class SkyboxHdrRangeTest : public GLTestFixture {};

TEST_F(SkyboxHdrRangeTest, VisibleSkyKeepsTheSunAboveTheIblCap)
{
    const std::string path = writeSunEquirect();
    Skybox skybox;
    ASSERT_TRUE(skybox.loadEquirectangular(path));

    EXPECT_GT(cubemapMax(skybox.getTextureId()), Skybox::IBL_MAX_RADIANCE * 10.0f)
        << "the sky cubemap flattened the sun to the IBL cap";

    std::filesystem::remove(path);
}

TEST_F(SkyboxHdrRangeTest, IblCaptureStillCapsTheSun)
{
    const std::string path = writeSunEquirect();
    Skybox skybox;
    ASSERT_TRUE(skybox.loadEquirectangular(path));

    EnvironmentMap envMap;
    ASSERT_TRUE(envMap.initialize(std::string(VESTIGE_SHADER_DIR) + "/.."));
    FullscreenQuad quad;
    Shader unusedSkyboxShader;
    envMap.generate(skybox.getTextureId(), true, quad, unusedSkyboxShader);
    ASSERT_TRUE(envMap.isReady());

    // RGB16F stores 50 exactly; the slack covers driver rounding only.
    EXPECT_LE(cubemapMax(envMap.getEnvironmentCubemap()), Skybox::IBL_MAX_RADIANCE + 0.1f)
        << "the sun reached the IBL capture uncapped";

    std::filesystem::remove(path);
}

}  // namespace Vestige::Test

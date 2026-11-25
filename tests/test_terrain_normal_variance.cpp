// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_normal_variance.cpp
/// @brief 3D_E-0816 — ground seen from afar keeps its bumpiness in the
///        specular highlight.
///
/// A mipmap averages the detail normals, so a distant texel's averaged normal
/// is shorter than 1 and, once normalised, flat. Shading that flat normal with
/// the map's roughness made far meadow ground glint like wet stone under the
/// sun. sampleGround() must widen the roughness by the spread the shortened
/// normal stands for. This runs the real sampleGround() from terrain.frag.glsl
/// on a normal array whose 1x1 mip averages a checker of two tilted normals,
/// then reads back the roughness it returns.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include <glad/gl.h>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Vestige::Test
{

namespace
{

constexpr float kMapRoughness = 0.3f;

/// terrain.frag.glsl with its main() and output removed, plus a
/// main() that writes sampleGround()'s roughness and shading-normal length.
std::string harnessSource()
{
    const std::string src = readShaderFile("terrain.frag.glsl");
    const std::size_t mainPos = src.find("void main()");
    if (src.empty() || mainPos == std::string::npos)
    {
        return {};
    }
    std::string head;
    std::size_t pos = 0;
    while (pos < mainPos)
    {
        std::size_t end = src.find('\n', pos);
        if (end == std::string::npos || end > mainPos)
        {
            end = mainPos;
        }
        const std::string line = src.substr(pos, end - pos);
        // A stage input becomes a plain global: functions this harness never
        // calls still read it, and the harness's vertex stage writes none.
        if (line.rfind("in ", 0) == 0)
        {
            head += line.substr(3) + "\n";
        }
        else if (line.rfind("out ", 0) != 0)
        {
            head += line + "\n";
        }
        pos = end + 1;
    }
    return head
        + "out vec4 outColor;\n"
          "void main()\n"
          "{\n"
          "    GroundSample g = sampleGround(vec2(0.25), vec4(1.0, 0.0, 0.0, 0.0));\n"
          "    outColor = vec4(g.roughness, length(g.tnormal), 0.0, 1.0);\n"
          "}\n";
}

using Texel = std::array<std::uint8_t, 3>;

/// Encodes a unit normal as RGB8, as the committed normal maps store it.
Texel encode(float x, float y, float z)
{
    auto e = [](float v)
    { return static_cast<std::uint8_t>(std::lround(((v * 0.5f) + 0.5f) * 255.0f)); };
    return {e(x), e(y), e(z)};
}

/// A 4-layer RGB8 array, 2x2 at level 0, with its 1x1 mip built by GL — the
/// same box average the engine's glGenerateTextureMipmap gives the ground maps.
GLuint makeArray2x2(const std::array<Texel, 4>& texels)
{
    std::vector<std::uint8_t> data;
    for (int layer = 0; layer < 4; ++layer)
    {
        for (const Texel& t : texels)
        {
            data.insert(data.end(), t.begin(), t.end());
        }
    }
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);
    glTextureStorage3D(tex, 2, GL_RGB8, 2, 2, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage3D(tex, 0, 0, 0, 0, 2, 2, 4, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateTextureMipmap(tex);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

/// A 4-layer RGB8 array of one texel: the material map (R=AO G=roughness B=height).
GLuint makeMaterialArray()
{
    const auto g = static_cast<std::uint8_t>(std::lround(kMapRoughness * 255.0f));
    std::vector<std::uint8_t> data;
    for (int layer = 0; layer < 4; ++layer)
    {
        data.insert(data.end(), {255, g, 128});
    }
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);
    glTextureStorage3D(tex, 1, GL_RGB8, 1, 1, 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage3D(tex, 0, 0, 0, 0, 1, 1, 4, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

/// The roughness a vMF lobe fitted to an averaged normal of length `len` adds
/// to a map roughness `r` (engine convention: alpha = r^2).
float expectedRoughness(float r, float len)
{
    const float kappa = (3.0f * len - len * len * len) / (1.0f - len * len);
    const float alpha2 = std::pow(r, 4.0f) + 2.0f / kappa;
    return std::pow(alpha2, 0.25f);
}

class TerrainNormalVarianceTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        const Texel grey = encode(0.0f, 0.0f, 0.0f);
        m_albedo = makeArray2x2({grey, grey, grey, grey});
        m_material = makeMaterialArray();
        m_prog = std::make_unique<ShaderProgram>(harnessSource());
    }

    void TearDown() override
    {
        m_prog.reset();
        if (m_albedo != 0)
        {
            glDeleteTextures(1, &m_albedo);
        }
        if (m_material != 0)
        {
            glDeleteTextures(1, &m_material);
        }
    }

    /// Runs sampleGround() with `normalArray`, sampling only mip `level`, at
    /// ground quality `quality` (2 High, 0 Low).
    glm::vec4 sample(GLuint normalArray, int level, int quality = 2)
    {
        glTextureParameteri(normalArray, GL_TEXTURE_BASE_LEVEL, level);
        glBindTextureUnit(6, m_albedo);
        glBindTextureUnit(7, normalArray);
        glBindTextureUnit(8, m_material);
        const UniformTable u{
            {"u_albedoArray", 6}, {"u_normalArray", 7}, {"u_materialArray", 8},
            {"u_groundQuality", quality},
            {"u_layerTiling[0]", 1.0f}, {"u_layerTiling[1]", 1.0f},
            {"u_layerTiling[2]", 1.0f}, {"u_layerTiling[3]", 1.0f}};
        return m_prog->run(u);
    }

    GLuint m_albedo = 0;
    GLuint m_material = 0;
    std::unique_ptr<ShaderProgram> m_prog;
};

}  // namespace

TEST_F(TerrainNormalVarianceTest, AveragedBumpsWidenRoughness)
{
    ASSERT_TRUE(m_prog && m_prog->valid());

    // A checker of normals tilted about 37 degrees either way along X. Its 1x1
    // mip averages to roughly (0, 0, 0.8): the bumps are still there, the mip
    // just cannot show them.
    const Texel left  = encode(-0.6f, 0.0f, 0.8f);
    const Texel right = encode( 0.6f, 0.0f, 0.8f);
    const GLuint bumpy = makeArray2x2({left, right, right, left});

    const glm::vec4 near = sample(bumpy, 0);
    const glm::vec4 far  = sample(bumpy, 1);
    glDeleteTextures(1, &bumpy);

    // Close up, one texel: the map's roughness, untouched.
    EXPECT_NEAR(near.x, kMapRoughness, 0.02f)
        << "a single detail texel changed the roughness";

    // Length 0.8 gives kappa = 5.24, so roughness = (0.3^4 + 2/kappa)^0.25 = 0.79.
    const float expected = expectedRoughness(kMapRoughness, 0.8f);
    EXPECT_NEAR(far.x, expected, 0.03f)
        << "far ground (averaged normal length 0.8) shaded with roughness " << far.x
        << ", as smooth as one flat texel, so its highlight glints like wet stone";

    EXPECT_NEAR(far.y, 1.0f, 1e-3f) << "the shading normal must still be unit length";
}

TEST_F(TerrainNormalVarianceTest, LowQualityKeepsTheBumpsItDoesNotShow)
{
    ASSERT_TRUE(m_prog && m_prog->valid());

    // Low shades the flat macro normal at every distance, so even close up it
    // must take the bumps' whole-texture spread (the 1x1 mip, length 0.8).
    const Texel left  = encode(-0.6f, 0.0f, 0.8f);
    const Texel right = encode( 0.6f, 0.0f, 0.8f);
    const GLuint bumpy = makeArray2x2({left, right, right, left});
    const glm::vec4 low = sample(bumpy, 0, /*quality=*/0);
    glDeleteTextures(1, &bumpy);

    EXPECT_NEAR(low.x, expectedRoughness(kMapRoughness, 0.8f), 0.03f)
        << "Low-quality ground shaded with roughness " << low.x;
    EXPECT_NEAR(low.y, 1.0f, 1e-3f);
}

TEST_F(TerrainNormalVarianceTest, FlatGroundKeepsItsRoughness)
{
    ASSERT_TRUE(m_prog && m_prog->valid());

    // Every texel agrees, so the mip loses nothing and nothing may change.
    const Texel up = encode(0.0f, 0.0f, 1.0f);
    const GLuint flat = makeArray2x2({up, up, up, up});
    const glm::vec4 far = sample(flat, 1);
    glDeleteTextures(1, &flat);

    EXPECT_NEAR(far.x, kMapRoughness, 0.02f);
}

}  // namespace Vestige::Test

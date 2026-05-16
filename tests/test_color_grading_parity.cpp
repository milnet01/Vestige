// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_color_grading_parity.cpp
/// @brief CPU↔GPU numerical parity for the colour-grading LUT path in
///        `assets/shaders/screen_quad.frag.glsl::applyColorGradingLut`.
///
/// `tests/test_color_grading.cpp` already pins per-component LUT
/// transforms (warm / cool / contrast / desaturate) and the CPU
/// nearest-neighbour `lutLookup` against itself. This file closes the
/// CPU↔GPU side: build an identity 3D LUT in CPU memory, upload it to
/// a `GL_RGBA8` 3D texture, then run inputs through the production
/// helper extracted from `screen_quad.frag.glsl` and confirm the GPU
/// trilinear sample at voxel-centre inputs returns the same value as
/// the CPU nearest-neighbour lookup (both produce the input unchanged
/// for an identity LUT).
///
/// Notes on tolerance — `GL_RGBA8` is 8-bit; quantisation budget is
/// ~`1/255 ≈ 4e-3` per channel. Tolerance set at `5e-3` covers the
/// quantise + dequantise round-trip without masking real divergence.

#include "gl_test_fixture.h"
#include "shader_parity_helpers.h"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <vector>

namespace Vestige::Test
{

namespace
{

/// Identity LUT: voxel (i,j,k) holds (i/(N-1), j/(N-1), k/(N-1)) as
/// 8-bit RGBA. Layout matches `tests/test_color_grading.cpp::generateNeutralLut`
/// (B-major: index = ((b * N + g) * N + r) * 4) so the GL upload sees
/// the same memory layout the CPU helper indexes against.
std::vector<unsigned char> makeIdentityLut(int size)
{
    std::vector<unsigned char> data(static_cast<size_t>(size * size * size) * 4);
    float maxIdx = static_cast<float>(size - 1);
    for (int b = 0; b < size; ++b)
    {
        for (int g = 0; g < size; ++g)
        {
            for (int r = 0; r < size; ++r)
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

/// Upload @a data as a `GL_RGBA8` 3D texture with linear filtering
/// + clamp-to-edge wrap. Returns the texture name; caller must free.
GLuint uploadLut3D(const std::vector<unsigned char>& data, int size)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, size, size, size, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_3D, 0);
    return tex;
}

/// CPU oracle — formula-identical to the C++ helper in
/// `tests/test_color_grading.cpp::lutLookup`. Re-stated here so this
/// file is self-contained and the comparator is independent.
glm::vec3 lutLookup_cpu(const std::vector<unsigned char>& data, int size,
                        glm::vec3 color)
{
    glm::vec3 c = glm::clamp(color, 0.0f, 1.0f);
    float s = static_cast<float>(size);
    glm::vec3 coord = c * ((s - 1.0f) / s) + glm::vec3(0.5f / s);

    int r = std::clamp(static_cast<int>(coord.r * static_cast<float>(size - 1) + 0.5f), 0, size - 1);
    int g = std::clamp(static_cast<int>(coord.g * static_cast<float>(size - 1) + 0.5f), 0, size - 1);
    int b = std::clamp(static_cast<int>(coord.b * static_cast<float>(size - 1) + 0.5f), 0, size - 1);

    size_t idx = static_cast<size_t>((b * size * size + g * size + r)) * 4;
    return { static_cast<float>(data[idx + 0]) / 255.0f,
             static_cast<float>(data[idx + 1]) / 255.0f,
             static_cast<float>(data[idx + 2]) / 255.0f };
}

// shader-source caching now lives in `readShaderFile` (basename-keyed) —
// no need for a per-file static cache here.

}  // namespace

class ColorGradingParityTest : public GLTestFixture {};

// =============================================================================
// applyColorGradingLut — identity LUT, voxel-centre inputs
// =============================================================================

TEST_F(ColorGradingParityTest, IdentityLutAtVoxelCentresMatchesCpuLookup)
{
    const std::string fnSrc = extractGlslFunction(
        readShaderFile("screen_quad.frag.glsl"), "applyColorGradingLut");
    ASSERT_FALSE(fnSrc.empty());

    constexpr int LUT_SIZE = 32;
    const auto lutData = makeIdentityLut(LUT_SIZE);
    const GLuint lutTex = uploadLut3D(lutData, LUT_SIZE);
    ASSERT_NE(lutTex, 0u);

    // Bind LUT to texture unit 1 (unit 0 is reserved for the FBO target
    // in case the helper ever uses sampler bindings; safer to stay off it).
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lutTex);

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3      u_color;\n"
        "uniform float     u_intensity;\n"
        "uniform sampler3D u_lut;\n"
        + fnSrc +
        "void main() {\n"
        "    vec3 r = applyColorGradingLut(u_color, u_lut, u_intensity);\n"
        "    outColor = vec4(r, 1.0);\n"
        "}\n");
    ASSERT_TRUE(prog.valid());

    // Voxel-centre values: `i/(N-1)` for i in {0, 7, 15, 23, 31}.
    const float c0  = 0.0f / 31.0f;
    const float c7  = 7.0f / 31.0f;
    const float c15 = 15.0f / 31.0f;
    const float c23 = 23.0f / 31.0f;
    const float c31 = 31.0f / 31.0f;

    const glm::vec3 inputs[] = {
        {c0,  c0,  c0 },   // black voxel
        {c31, c31, c31},   // white voxel
        {c15, c15, c15},   // mid voxel
        {c7,  c23, c15},   // off-axis interior voxel
        {c31, c0,  c0 },   // pure red voxel
    };

    for (auto in : inputs)
    {
        glm::vec4 gpu = prog.run({
            {"u_color",     in},
            {"u_intensity", 1.0f},
            {"u_lut",       1},  // sampler bound to unit 1
        });
        glm::vec3 cpu = lutLookup_cpu(lutData, LUT_SIZE, in);

        EXPECT_NEAR(gpu.r, cpu.x, 5e-3f) << "(" << in.x << "," << in.y << "," << in.z << ").r";
        EXPECT_NEAR(gpu.g, cpu.y, 5e-3f) << "(" << in.x << "," << in.y << "," << in.z << ").g";
        EXPECT_NEAR(gpu.b, cpu.z, 5e-3f) << "(" << in.x << "," << in.y << "," << in.z << ").b";

        // Identity LUT also implies output equals input (within quantisation).
        EXPECT_NEAR(gpu.r, in.x, 5e-3f) << "identity round-trip: r";
        EXPECT_NEAR(gpu.g, in.y, 5e-3f) << "identity round-trip: g";
        EXPECT_NEAR(gpu.b, in.z, 5e-3f) << "identity round-trip: b";
    }

    glBindTexture(GL_TEXTURE_3D, 0);
    glDeleteTextures(1, &lutTex);
}

// =============================================================================
// applyColorGradingLut — intensity = 0 must passthrough exactly
// =============================================================================

TEST_F(ColorGradingParityTest, ZeroIntensityIsExactPassthrough)
{
    const std::string fnSrc = extractGlslFunction(
        readShaderFile("screen_quad.frag.glsl"), "applyColorGradingLut");
    ASSERT_FALSE(fnSrc.empty());

    // Use a LUT that maps everything to 0 (a "kill" LUT) so any
    // intensity > 0 would visibly darken the output. With intensity = 0
    // the LUT's contribution is fully muted and the input passes through.
    constexpr int LUT_SIZE = 8;
    std::vector<unsigned char> killLut(static_cast<size_t>(LUT_SIZE * LUT_SIZE * LUT_SIZE) * 4, 0);
    for (size_t i = 3; i < killLut.size(); i += 4) killLut[i] = 255;  // alpha
    const GLuint lutTex = uploadLut3D(killLut, LUT_SIZE);
    ASSERT_NE(lutTex, 0u);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lutTex);

    ShaderProgram prog(
        "#version 450 core\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform vec3      u_color;\n"
        "uniform float     u_intensity;\n"
        "uniform sampler3D u_lut;\n"
        + fnSrc +
        "void main() {\n"
        "    vec3 r = applyColorGradingLut(u_color, u_lut, u_intensity);\n"
        "    outColor = vec4(r, 1.0);\n"
        "}\n");
    ASSERT_TRUE(prog.valid());

    const glm::vec3 input(0.4f, 0.7f, 0.2f);
    glm::vec4 gpu = prog.run({
        {"u_color",     input},
        {"u_intensity", 0.0f},
        {"u_lut",       1},
    });

    // mix(input, killed, 0.0) == input exactly (no FP rounding).
    EXPECT_FLOAT_EQ(gpu.r, input.x);
    EXPECT_FLOAT_EQ(gpu.g, input.y);
    EXPECT_FLOAT_EQ(gpu.b, input.z);

    glBindTexture(GL_TEXTURE_3D, 0);
    glDeleteTextures(1, &lutTex);
}

// Phase 10.9 Slice 18 Ts1 cleanup: dropped
// `MixBlendMatchesCpuAcrossIntensities` — it tested generic GLSL `mix`
// against `glm::mix` with arbitrary inputs, not the LUT-blend path
// the file otherwise pins. If GLSL `mix` ever drifted from `lerp`
// semantics the rest of the codebase would catch fire long before
// LUT blending did. The actual LUT-blend wiring is covered by the
// identity round-trip in the suite's main parity test.

}  // namespace Vestige::Test

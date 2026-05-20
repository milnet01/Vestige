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

#include "color_grading_test_helpers.h"
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

// /test-audit 2026-05-17 Ts19-D6: canonical LUT-bytes builder lives in
// color_grading_test_helpers.h, shared with the CPU-only test file.
std::vector<unsigned char> makeIdentityLut(int size)
{
    return ::Vestige::Testing::makeNeutralLutBytes(size);
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

/// RAII wrapper for a GL texture name. /test-audit 2026-05-17 Ts19-I1:
/// the prior tests called `glDeleteTextures` at the end of the function,
/// so any earlier `ASSERT_TRUE` (e.g. shader-compile failure) would abort
/// the function and leak the texture into the next test's GL state.
class ScopedGLTexture
{
public:
    ScopedGLTexture() = default;
    explicit ScopedGLTexture(GLuint id) : m_id(id) {}
    ~ScopedGLTexture() { if (m_id) glDeleteTextures(1, &m_id); }
    ScopedGLTexture(const ScopedGLTexture&) = delete;
    ScopedGLTexture& operator=(const ScopedGLTexture&) = delete;
    ScopedGLTexture(ScopedGLTexture&& o) noexcept : m_id(o.m_id) { o.m_id = 0; }
    ScopedGLTexture& operator=(ScopedGLTexture&& o) noexcept
    {
        if (this != &o)
        {
            if (m_id) glDeleteTextures(1, &m_id);
            m_id = o.m_id;
            o.m_id = 0;
        }
        return *this;
    }
    GLuint id() const { return m_id; }
    operator GLuint() const { return m_id; }
private:
    GLuint m_id = 0;
};

// /test-audit 2026-05-17 Ts19-D6: CPU LUT oracle lives in
// color_grading_test_helpers.h, shared with the CPU-only test file.
glm::vec3 lutLookup_cpu(const std::vector<unsigned char>& data, int size,
                        glm::vec3 color)
{
    return ::Vestige::Testing::lutLookupNearest(data, size, color);
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
    ScopedGLTexture lutTex{uploadLut3D(lutData, LUT_SIZE)};
    ASSERT_NE(lutTex.id(), 0u);

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
    // lutTex frees itself when it leaves scope.
}

// =============================================================================
// applyColorGradingLut — identity LUT, INTER-voxel inputs (trilinear path)
// =============================================================================

// Ts20-CV7: the voxel-centre test above lands every input on an exact
// texel, so a broken trilinear coordinate mapping (e.g. a missing
// half-texel inset) could still pass it. Drive inputs that fall BETWEEN
// voxel centres — 0.1 / 0.5 / 0.9 fractions of the [0,1] range — so the
// GPU must trilinearly interpolate. An identity LUT is a linear ramp, so
// the interpolated sample must still equal the input within the 8-bit
// quantisation budget. The CPU nearest-neighbour oracle is intentionally
// NOT used here (it snaps to a voxel and would diverge by up to half a
// cell); the identity round-trip is the correct reference for sub-voxel
// inputs.
TEST_F(ColorGradingParityTest, IdentityLutInterVoxelInputsRoundTrip)
{
    const std::string fnSrc = extractGlslFunction(
        readShaderFile("screen_quad.frag.glsl"), "applyColorGradingLut");
    ASSERT_FALSE(fnSrc.empty());

    constexpr int LUT_SIZE = 32;
    const auto lutData = makeIdentityLut(LUT_SIZE);
    ScopedGLTexture lutTex{uploadLut3D(lutData, LUT_SIZE)};
    ASSERT_NE(lutTex.id(), 0u);
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

    // None of these coordinates is an i/31 voxel centre — each forces a
    // genuine trilinear blend between adjacent texels.
    const glm::vec3 inputs[] = {
        {0.1f, 0.1f, 0.1f},
        {0.5f, 0.5f, 0.5f},
        {0.9f, 0.9f, 0.9f},
        {0.1f, 0.5f, 0.9f},
        {0.37f, 0.62f, 0.84f},
    };

    for (auto in : inputs)
    {
        glm::vec4 gpu = prog.run({
            {"u_color",     in},
            {"u_intensity", 1.0f},
            {"u_lut",       1},
        });
        EXPECT_NEAR(gpu.r, in.x, 5e-3f) << "inter-voxel round-trip r @ " << in.x;
        EXPECT_NEAR(gpu.g, in.y, 5e-3f) << "inter-voxel round-trip g @ " << in.y;
        EXPECT_NEAR(gpu.b, in.z, 5e-3f) << "inter-voxel round-trip b @ " << in.z;
    }

    glBindTexture(GL_TEXTURE_3D, 0);
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
    ScopedGLTexture lutTex{uploadLut3D(killLut, LUT_SIZE)};
    ASSERT_NE(lutTex.id(), 0u);
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
    // lutTex frees itself when it leaves scope.
}

// Phase 10.9 Slice 18 Ts1 cleanup: dropped
// `MixBlendMatchesCpuAcrossIntensities` — it tested generic GLSL `mix`
// against `glm::mix` with arbitrary inputs, not the LUT-blend path
// the file otherwise pins. If GLSL `mix` ever drifted from `lerp`
// semantics the rest of the codebase would catch fire long before
// LUT blending did. The actual LUT-blend wiring is covered by the
// identity round-trip in the suite's main parity test.

}  // namespace Vestige::Test

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_fxaa.cpp
/// @brief GPU smoke + behaviour tests for the Tier-1 budget AA + sharpen
///        stack shaders (fxaa.frag.glsl, cas.frag.glsl). Confirms each pass
///        compiles + links against the real screen-quad convention, that FXAA
///        measurably softens a stair-stepped (aliased) edge (design §6 item 7),
///        and that CAS leaves a flat region untouched (adaptive weight → no
///        distortion).
///
/// These render the ACTUAL shipped shader files (via readShaderFile), so a
/// GLSL regression in either shader fails here rather than silently in-engine.
#include "shader_parity_helpers.h"
#include "gl_test_fixture.h"
#include "lsan_guard.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

using Vestige::Test::GLTestFixture;
using Vestige::Test::readShaderFile;

namespace
{

// Full-screen triangle that also emits v_texCoord in [0,1] — the post-process
// shaders read v_texCoord (the shipped screen_quad.vert.glsl does the same, but
// that one needs a VBO; the gl_VertexID trick keeps this test attribute-less).
constexpr const char* kVertexSrc = R"(#version 450 core
out vec2 v_texCoord;
void main()
{
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    v_texCoord = 0.5 * p + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

GLuint compile(GLenum stage, const std::string& src)
{
    GLuint sh = glCreateShader(stage);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048] = {0};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        ADD_FAILURE() << "shader compile failed:\n" << log;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// Links kVertexSrc + the given fragment source. Returns 0 (with a failure) on
// any compile/link error — the "does this shader build?" smoke check.
GLuint linkProgram(const std::string& fragSrc)
{
    GLuint vs = compile(GL_VERTEX_SHADER, kVertexSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048] = {0};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        ADD_FAILURE() << "program link failed:\n" << log;
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// Renders `prog` over an RGBA8 input texture (LINEAR-filtered, clamped) into an
// RGBA8 FBO of the same size and returns the readback as floats. `extraUniforms`
// runs after u_colorTexture/u_rtMetrics are set, for pass-specific knobs.
std::vector<glm::vec4> renderPass(
    GLuint prog, int w, int h, const std::vector<glm::vec4>& input,
    const std::function<void(GLuint)>& extraUniforms = {})
{
    // llvmpipe JITs + caches process-lifetime pipe state on first draw — a
    // third-party allocation, not ours. Keep it off LSan's books.
    Vestige::Test::ScopedLeakCheckDisable noLeakTracking;

    GLuint inTex = 0;
    glGenTextures(1, &inTex);
    glBindTexture(GL_TEXTURE_2D, inTex);
    std::vector<float> pixels;
    pixels.reserve(static_cast<size_t>(w) * h * 4);
    for (const auto& c : input) { pixels.push_back(c.r); pixels.push_back(c.g); pixels.push_back(c.b); pixels.push_back(c.a); }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_FLOAT, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint fbo = 0, outTex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &outTex);
    glBindTexture(GL_TEXTURE_2D, outTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex, 0);
    EXPECT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inTex);
    glUniform1i(glGetUniformLocation(prog, "u_colorTexture"), 0);
    GLint rtLoc = glGetUniformLocation(prog, "u_rtMetrics");
    if (rtLoc >= 0)
        glUniform4f(rtLoc, 1.0f / w, 1.0f / h, static_cast<float>(w), static_cast<float>(h));
    if (extraUniforms) extraUniforms(prog);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    std::vector<glm::vec4> out(static_cast<size_t>(w) * h);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out.data());

    glDeleteVertexArrays(1, &vao);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &outTex);
    glDeleteTextures(1, &inTex);
    return out;
}

// A hard SHALLOW-SLOPE edge: white below a near-horizontal boundary that rises
// one row every 4 columns. Unlike an axis-aligned edge (already alias-free, and
// which FXAA correctly leaves alone) or a 45° edge (1-px runs — FXAA's edge
// search has nothing to march along), a shallow slope produces the long
// stair-steps that are the canonical aliasing case FXAA exists to smooth.
std::vector<glm::vec4> shallowEdge(int w, int h)
{
    std::vector<glm::vec4> img(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float boundary = static_cast<float>(h) * 0.5f + static_cast<float>(x) * 0.25f;
            img[static_cast<size_t>(y) * w + x] =
                (static_cast<float>(y) > boundary) ? glm::vec4(1, 1, 1, 1)
                                                   : glm::vec4(0, 0, 0, 1);
        }
    return img;
}

class FxaaShaderTest : public GLTestFixture {};

} // namespace

TEST_F(FxaaShaderTest, FxaaAndCasCompileAndLink)
{
    EXPECT_NE(linkProgram(readShaderFile("fxaa.frag.glsl")), 0u);
    EXPECT_NE(linkProgram(readShaderFile("cas.frag.glsl")), 0u);
}

TEST_F(FxaaShaderTest, FxaaSoftensStairSteppedEdge)
{
    constexpr int W = 64, H = 64;
    GLuint prog = linkProgram(readShaderFile("fxaa.frag.glsl"));
    ASSERT_NE(prog, 0u);

    const auto out = renderPass(prog, W, H, shallowEdge(W, H));
    glDeleteProgram(prog);
    ASSERT_EQ(out.size(), static_cast<size_t>(W) * H);

    auto lumaAt = [&](int x, int y) { return out[static_cast<size_t>(y) * W + x].r; };

    // Deep in each flat region the image must be untouched — no over-blur.
    EXPECT_LT(lumaAt(10, 8),  0.05f) << "top black region blurred";
    EXPECT_GT(lumaAt(10, 56), 0.95f) << "bottom white region dimmed";

    // FXAA must introduce intermediate luma along the stair-stepped edge —
    // proof it blended the aliased step corners. Count strongly anti-aliased
    // pixels; a mere pass-through would leave every pixel pure black or white.
    int softened = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            float v = lumaAt(x, y);
            if (v > 0.15f && v < 0.85f) ++softened;
        }
    EXPECT_GT(softened, 20) << "FXAA barely softened the edge (" << softened << " px)";
}

TEST_F(FxaaShaderTest, CasLeavesFlatRegionUnchanged)
{
    constexpr int W = 16, H = 16;
    GLuint prog = linkProgram(readShaderFile("cas.frag.glsl"));
    ASSERT_NE(prog, 0u);

    std::vector<glm::vec4> flat(static_cast<size_t>(W) * H, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
    const auto out = renderPass(prog, W, H, flat, [](GLuint p) {
        glUniform1f(glGetUniformLocation(p, "u_sharpness"), 0.5f);
    });
    glDeleteProgram(prog);
    ASSERT_EQ(out.size(), static_cast<size_t>(W) * H);

    // Adaptive sharpening has nothing to sharpen on a flat field — the centre
    // pixel must survive (the neighbour blend cancels to the original value).
    const glm::vec4 c = out[static_cast<size_t>(H / 2) * W + W / 2];
    EXPECT_NEAR(c.r, 0.5f, 0.02f);
    EXPECT_NEAR(c.g, 0.5f, 0.02f);
    EXPECT_NEAR(c.b, 0.5f, 0.02f);
}

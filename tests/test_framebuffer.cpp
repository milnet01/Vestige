// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_framebuffer.cpp
/// @brief Unit tests for the FramebufferConfig struct and the MRT (second
///        color attachment) path used by the motion-vector buffer (Slice R1).
#include "renderer/framebuffer.h"

#include "gl_test_fixture.h"

#include <glad/gl.h>

#include <array>

#include <gtest/gtest.h>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

TEST(FramebufferConfigTest, DefaultValues)
{
    FramebufferConfig config;

    EXPECT_EQ(config.width, 1280);
    EXPECT_EQ(config.height, 720);
    EXPECT_EQ(config.samples, 1);
    EXPECT_TRUE(config.hasColorAttachment);
    EXPECT_TRUE(config.hasDepthAttachment);
    EXPECT_FALSE(config.isFloatingPoint);
    EXPECT_FALSE(config.isDepthTexture);
}

TEST(FramebufferConfigTest, CustomValues)
{
    FramebufferConfig config;
    config.width = 2048;
    config.height = 2048;
    config.samples = 4;
    config.hasColorAttachment = false;
    config.hasDepthAttachment = true;
    config.isFloatingPoint = true;
    config.isDepthTexture = true;

    EXPECT_EQ(config.width, 2048);
    EXPECT_EQ(config.height, 2048);
    EXPECT_EQ(config.samples, 4);
    EXPECT_FALSE(config.hasColorAttachment);
    EXPECT_TRUE(config.hasDepthAttachment);
    EXPECT_TRUE(config.isFloatingPoint);
    EXPECT_TRUE(config.isDepthTexture);
}

TEST(FramebufferConfigTest, ShadowMapConfig)
{
    // Typical shadow map configuration
    FramebufferConfig config;
    config.width = 2048;
    config.height = 2048;
    config.samples = 1;
    config.hasColorAttachment = false;
    config.hasDepthAttachment = true;
    config.isDepthTexture = true;

    EXPECT_EQ(config.samples, 1);
    EXPECT_FALSE(config.hasColorAttachment);
    EXPECT_TRUE(config.isDepthTexture);
}

TEST(FramebufferConfigTest, HDRConfig)
{
    // Typical HDR rendering configuration
    FramebufferConfig config;
    config.isFloatingPoint = true;
    config.samples = 4;

    EXPECT_TRUE(config.isFloatingPoint);
    EXPECT_EQ(config.samples, 4);
    EXPECT_TRUE(config.hasColorAttachment);
    EXPECT_TRUE(config.hasDepthAttachment);
}

TEST(FramebufferConfigTest, MSAAConfig)
{
    // 4x MSAA configuration
    FramebufferConfig config;
    config.samples = 4;

    EXPECT_EQ(config.samples, 4);
    EXPECT_TRUE(config.hasColorAttachment);
    EXPECT_TRUE(config.hasDepthAttachment);
    EXPECT_FALSE(config.isFloatingPoint);
}

TEST(FramebufferConfigTest, SecondColorAttachmentDefaultsOff)
{
    FramebufferConfig config;
    EXPECT_FALSE(config.secondColorAttachment);
}

// --- GL-context tests for the MRT (second color attachment) path (Slice R1) ---

namespace
{
// Build the TAA-scene-FBO-shaped config: non-MSAA, HDR (RGBA16F), MRT motion attachment.
FramebufferConfig makeMrtConfig(int w, int h)
{
    FramebufferConfig config;
    config.width = w;
    config.height = h;
    config.samples = 1;
    config.isFloatingPoint = true;          // RGBA16F — both attachments
    config.secondColorAttachment = true;    // motion buffer at GL_COLOR_ATTACHMENT1
    return config;
}
}  // namespace

class FramebufferMrtTest : public GLTestFixture {};

// FBO with two color attachments must be GL_FRAMEBUFFER_COMPLETE.
TEST_F(FramebufferMrtTest, SecondAttachmentFramebufferIsComplete)
{
    Framebuffer fb(makeMrtConfig(64, 64));
    EXPECT_TRUE(fb.isComplete());
}

// Both draw buffers must be enumerated so the fragment shader's two outputs
// route to GL_COLOR_ATTACHMENT0 and GL_COLOR_ATTACHMENT1.
TEST_F(FramebufferMrtTest, DrawBuffersEnumerateBothAttachments)
{
    Framebuffer fb(makeMrtConfig(64, 64));
    ASSERT_TRUE(fb.isComplete());

    // GL_DRAW_BUFFERi is queried with glGetIntegerv against the bound framebuffer.
    fb.bind();
    GLint drawBuffer0 = 0;
    GLint drawBuffer1 = 0;
    glGetIntegerv(GL_DRAW_BUFFER0, &drawBuffer0);
    glGetIntegerv(GL_DRAW_BUFFER1, &drawBuffer1);

    EXPECT_EQ(drawBuffer0, static_cast<GLint>(GL_COLOR_ATTACHMENT0));
    EXPECT_EQ(drawBuffer1, static_cast<GLint>(GL_COLOR_ATTACHMENT1));
}

// Load-bearing (guards cold-eyes C-A): a single glClear writes the global
// clear-color into BOTH draw buffers, so a scene with blue >= 0.5 would clear
// the motion attachment's coverage flag (.b) to "covered". clearSecondAttachment()
// must zero attachment 1 independent of the scene clear color.
TEST_F(FramebufferMrtTest, SecondAttachmentClearsToZeroUnderNonZeroClearColor)
{
    Framebuffer fb(makeMrtConfig(4, 4));
    ASSERT_TRUE(fb.isComplete());

    fb.bind();
    // Scene clear color with blue >= 0.5 — the failure case from the review.
    glClearColor(0.2f, 0.3f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // Independently zero the motion attachment.
    fb.clearSecondAttachment();

    // Read attachment 1 back.
    std::array<float, 4> px1{{-1.0f, -1.0f, -1.0f, -1.0f}};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb.getId());
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, px1.data());

    EXPECT_FLOAT_EQ(px1[0], 0.0f);
    EXPECT_FLOAT_EQ(px1[1], 0.0f);
    EXPECT_FLOAT_EQ(px1[2], 0.0f) << "coverage flag (.b) must be 0 regardless of scene clear color";
    EXPECT_FLOAT_EQ(px1[3], 0.0f);

    // Attachment 0 still holds the scene clear color (unchanged by the .b clear).
    std::array<float, 4> px0{{0.0f, 0.0f, 0.0f, 0.0f}};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, px0.data());
    EXPECT_NEAR(px0[0], 0.2f, 1e-3f);
    EXPECT_NEAR(px0[1], 0.3f, 1e-3f);
    EXPECT_NEAR(px0[2], 0.9f, 1e-3f);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

// bindColorTexture(unit, 1) must bind attachment 1; the default arg keeps all
// existing single-attachment call-sites pointing at attachment 0.
TEST_F(FramebufferMrtTest, BindAttachmentOneSucceeds)
{
    Framebuffer fb(makeMrtConfig(16, 16));
    ASSERT_TRUE(fb.isComplete());

    // Should bind without a GL error.
    while (glGetError() != GL_NO_ERROR) {}  // drain
    fb.bindColorTexture(3, 1);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

// --- Slice R2: third color attachment (world-normal buffer) ---

namespace
{
// TAA-scene-FBO-shaped config with the R2 normal attachment at GL_COLOR_ATTACHMENT2.
FramebufferConfig makeMrt3Config(int w, int h)
{
    FramebufferConfig config = makeMrtConfig(w, h);
    config.thirdColorAttachment = true;     // normal buffer at GL_COLOR_ATTACHMENT2
    return config;
}
}  // namespace

// FBO with three color attachments must be GL_FRAMEBUFFER_COMPLETE.
TEST_F(FramebufferMrtTest, ThirdAttachmentFramebufferIsComplete)
{
    Framebuffer fb(makeMrt3Config(64, 64));
    EXPECT_TRUE(fb.isComplete());
}

// All three draw buffers must be enumerated so the scene shader's three outputs
// route to GL_COLOR_ATTACHMENT0/1/2.
TEST_F(FramebufferMrtTest, DrawBuffersEnumerateThreeAttachments)
{
    Framebuffer fb(makeMrt3Config(64, 64));
    ASSERT_TRUE(fb.isComplete());

    fb.bind();
    GLint db0 = 0, db1 = 0, db2 = 0;
    glGetIntegerv(GL_DRAW_BUFFER0, &db0);
    glGetIntegerv(GL_DRAW_BUFFER1, &db1);
    glGetIntegerv(GL_DRAW_BUFFER2, &db2);
    EXPECT_EQ(db0, static_cast<GLint>(GL_COLOR_ATTACHMENT0));
    EXPECT_EQ(db1, static_cast<GLint>(GL_COLOR_ATTACHMENT1));
    EXPECT_EQ(db2, static_cast<GLint>(GL_COLOR_ATTACHMENT2));
}

// Test 6 (R2, clear-to-zero portion) — the normal attachment must clear to a
// zero-length normal regardless of the scene clear colour. That (0,0,0,0) is the
// V_mask sentinel: pixels no opaque geometry wrote keep R1 feedback. Extends the
// R1 attachment-1 clear-to-zero guard to attachment 2. (The "holds normalize(world
// normal) for an opaque pixel" half is a render-pipeline / launch-time spot-check,
// §9.10 #6/#10 — not expressible at the Framebuffer unit level.)
TEST_F(FramebufferMrtTest, ThirdAttachmentClearsToZeroUnderNonZeroClearColor)
{
    Framebuffer fb(makeMrt3Config(4, 4));
    ASSERT_TRUE(fb.isComplete());

    fb.bind();
    glClearColor(0.7f, 0.4f, 0.9f, 1.0f);   // non-zero — would be a bogus normal
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    fb.clearThirdAttachment();

    std::array<float, 4> px2{{-1.0f, -1.0f, -1.0f, -1.0f}};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb.getId());
    glReadBuffer(GL_COLOR_ATTACHMENT2);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, px2.data());
    EXPECT_FLOAT_EQ(px2[0], 0.0f);
    EXPECT_FLOAT_EQ(px2[1], 0.0f);
    EXPECT_FLOAT_EQ(px2[2], 0.0f) << "uncovered normal must be the zero-length V_mask sentinel";
    EXPECT_FLOAT_EQ(px2[3], 0.0f);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

// bindColorTexture(unit, 2) must bind attachment 2 without a GL error.
TEST_F(FramebufferMrtTest, BindAttachmentTwoSucceeds)
{
    Framebuffer fb(makeMrt3Config(16, 16));
    ASSERT_TRUE(fb.isComplete());

    while (glGetError() != GL_NO_ERROR) {}  // drain
    fb.bindColorTexture(4, 2);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

// Two-attachment FBOs stay byte-for-byte unchanged: no attachment 2, no DRAW_BUFFER2.
TEST_F(FramebufferMrtTest, TwoAttachmentFboHasNoThirdDrawBuffer)
{
    Framebuffer fb(makeMrtConfig(32, 32));   // second only, no third
    ASSERT_TRUE(fb.isComplete());

    fb.bind();
    GLint db2 = 0;
    glGetIntegerv(GL_DRAW_BUFFER2, &db2);
    EXPECT_EQ(db2, static_cast<GLint>(GL_NONE));
}

namespace
{
// TAA-scene-FBO-shaped config with the R4 GI injection source at GL_COLOR_ATTACHMENT3.
FramebufferConfig makeMrt4Config(int w, int h)
{
    FramebufferConfig config = makeMrt3Config(w, h);
    config.fourthColorAttachment = true;    // GI injection source at GL_COLOR_ATTACHMENT3
    return config;
}
}  // namespace

// FBO with four color attachments must be GL_FRAMEBUFFER_COMPLETE.
TEST_F(FramebufferMrtTest, FourthAttachmentFramebufferIsComplete)
{
    Framebuffer fb(makeMrt4Config(64, 64));
    EXPECT_TRUE(fb.isComplete());
}

// All four draw buffers must be enumerated so the scene shader's four outputs
// route to GL_COLOR_ATTACHMENT0/1/2/3.
TEST_F(FramebufferMrtTest, DrawBuffersEnumerateFourAttachments)
{
    Framebuffer fb(makeMrt4Config(64, 64));
    ASSERT_TRUE(fb.isComplete());

    fb.bind();
    GLint db0 = 0, db1 = 0, db2 = 0, db3 = 0;
    glGetIntegerv(GL_DRAW_BUFFER0, &db0);
    glGetIntegerv(GL_DRAW_BUFFER1, &db1);
    glGetIntegerv(GL_DRAW_BUFFER2, &db2);
    glGetIntegerv(GL_DRAW_BUFFER3, &db3);
    EXPECT_EQ(db0, static_cast<GLint>(GL_COLOR_ATTACHMENT0));
    EXPECT_EQ(db1, static_cast<GLint>(GL_COLOR_ATTACHMENT1));
    EXPECT_EQ(db2, static_cast<GLint>(GL_COLOR_ATTACHMENT2));
    EXPECT_EQ(db3, static_cast<GLint>(GL_COLOR_ATTACHMENT3));
}

// The GI injection source must clear to (0,0,0,0) regardless of the scene clear
// colour — so uncovered / non-opaque texels inject zero radiance into the froxel
// cache rather than stale garbage. Extends the att1/att2 clear-to-zero guards to att3.
TEST_F(FramebufferMrtTest, FourthAttachmentClearsToZeroUnderNonZeroClearColor)
{
    Framebuffer fb(makeMrt4Config(4, 4));
    ASSERT_TRUE(fb.isComplete());

    fb.bind();
    glClearColor(0.7f, 0.4f, 0.9f, 1.0f);   // non-zero — would be a bogus radiance
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    fb.clearFourthAttachment();

    std::array<float, 4> px3{{-1.0f, -1.0f, -1.0f, -1.0f}};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb.getId());
    glReadBuffer(GL_COLOR_ATTACHMENT3);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, px3.data());
    EXPECT_FLOAT_EQ(px3[0], 0.0f);
    EXPECT_FLOAT_EQ(px3[1], 0.0f);
    EXPECT_FLOAT_EQ(px3[2], 0.0f) << "uncovered GI source must inject zero radiance";
    EXPECT_FLOAT_EQ(px3[3], 0.0f);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

// bindColorTexture(unit, 3) must bind attachment 3 without a GL error.
TEST_F(FramebufferMrtTest, BindAttachmentThreeSucceeds)
{
    Framebuffer fb(makeMrt4Config(16, 16));
    ASSERT_TRUE(fb.isComplete());

    while (glGetError() != GL_NO_ERROR) {}  // drain
    fb.bindColorTexture(5, 3);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

// Three-attachment FBOs stay byte-for-byte unchanged: no attachment 3, no DRAW_BUFFER3.
TEST_F(FramebufferMrtTest, ThreeAttachmentFboHasNoFourthDrawBuffer)
{
    Framebuffer fb(makeMrt3Config(32, 32));   // third only, no fourth
    ASSERT_TRUE(fb.isComplete());

    fb.bind();
    GLint db3 = 0;
    glGetIntegerv(GL_DRAW_BUFFER3, &db3);
    EXPECT_EQ(db3, static_cast<GLint>(GL_NONE));
}

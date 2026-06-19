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

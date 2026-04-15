// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_framebuffer.cpp
/// @brief Unit tests for the FramebufferConfig struct.
#include "renderer/framebuffer.h"

#include <gtest/gtest.h>

using namespace Vestige;

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

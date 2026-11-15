// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_water_fbo.cpp
/// @brief WaterFbo keeps no incomplete render target (3D_E-0874 lane-05,
///        water_fbo.cpp resize). A failed init or resize must free both
///        targets, so the engine skips the planar passes and the water shader
///        falls back to the cubemap, instead of drawing into a broken target.

#include "renderer/water_fbo.h"

#include "gl_test_fixture.h"

#include <gtest/gtest.h>

using namespace Vestige;

class WaterFboGLTest : public ::Vestige::Test::GLTestFixture
{
};

// A zero-sized texture has no storage, so its framebuffer is incomplete.
TEST_F(WaterFboGLTest, FailedResizeFreesBothTargets)
{
    WaterFbo fbo;
    ASSERT_TRUE(fbo.init(64, 64, 64, 64));
    ASSERT_NE(fbo.getReflectionTexture(), 0u);

    EXPECT_FALSE(fbo.resize(0, 0, 0, 0));
    while (glGetError() != GL_NO_ERROR) {}  // the zero-size storage call's error

    EXPECT_FALSE(fbo.isComplete());
    EXPECT_EQ(fbo.getReflectionTexture(), 0u);
    EXPECT_EQ(fbo.getRefractionTexture(), 0u);
    EXPECT_EQ(fbo.getRefractionDepthTexture(), 0u);

    // Asking again for the size that failed does not rebuild it every frame.
    EXPECT_FALSE(fbo.resize(0, 0, 0, 0));
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    // A good size afterwards builds both targets again.
    EXPECT_TRUE(fbo.resize(32, 32, 32, 32));
    EXPECT_TRUE(fbo.isComplete());
    EXPECT_NE(fbo.getReflectionTexture(), 0u);
}

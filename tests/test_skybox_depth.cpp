// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_skybox_depth.cpp
/// @brief 3D_E-0874 — the skybox's depth follows the live depth convention.
///
/// The water reflection and refraction passes render geometry-only under the
/// engine's reverse-Z (GL_GEQUAL, cleared 0). The sky was placed at 0.99999
/// whenever a pass was geometry-only, which is in front of nearly every
/// reverse-Z surface, so the sky drew over the reflected and refracted scene.
#include "gl_test_fixture.h"
#include "renderer/scoped_forward_z.h"
#include "renderer/skybox.h"
#include "renderer/skybox_depth.h"

#include <glad/gl.h>
#include <gtest/gtest.h>

using namespace Vestige;
using Vestige::Test::GLTestFixture;

namespace
{

class SkyboxDepthTest : public GLTestFixture
{
protected:
    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        // The engine's main convention (renderer.cpp initialisation).
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
        glDepthFunc(GL_GEQUAL);
        glClearDepth(0.0);
    }

    void TearDown() override
    {
        if (IsSkipped())
        {
            return;
        }
        glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
        glDepthFunc(GL_LESS);
        glClearDepth(1.0);
    }
};

} // namespace

// A reverse-Z pass (main view, water reflection, water refraction) puts the
// sky at the far plane, behind any surface, and leaves the sun uncapped.
TEST_F(SkyboxDepthTest, ReverseZPassPutsTheSkyBehindEverySurface)
{
    const SkyboxPassState state = skyboxPassStateForCurrentDepthTest();

    // Under GL_GEQUAL a sky fragment at farDepth survives wherever farDepth
    // >= the stored depth. A distant surface sits near 0 in reverse-Z.
    const float distantSurfaceDepth = 0.001f;
    EXPECT_LT(state.farDepth, distantSurfaceDepth) << "the sky would draw over the surface";
    EXPECT_EQ(state.maxRadiance, 0.0f) << "a visible sky is not capped";
}

// A cubemap capture (forward-Z) puts the sky just inside the cleared far
// value and caps the sun, since captures feed lighting.
TEST_F(SkyboxDepthTest, ForwardZCapturePutsTheSkyJustInsideFar)
{
    ScopedForwardZ forwardZ;
    const SkyboxPassState state = skyboxPassStateForCurrentDepthTest();

    EXPECT_LT(state.farDepth, 1.0f) << "GL_LESS would fail against the cleared value";
    EXPECT_GT(state.farDepth, 0.999f) << "the sky would draw over distant geometry";
    EXPECT_EQ(state.maxRadiance, Skybox::IBL_MAX_RADIANCE);
}

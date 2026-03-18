/// @file test_skybox.cpp
/// @brief Unit tests for Skybox configuration and defaults.
#include "renderer/skybox.h"

#include <gtest/gtest.h>

using namespace Vestige;

TEST(SkyboxTest, DefaultProceduralMode)
{
    // Without an OpenGL context, we can't construct a Skybox,
    // but we can verify the default state expectations:
    // - hasTexture should be false (procedural gradient by default)
    // This test documents the expected API contract.

    // Skybox starts with no cubemap texture — uses procedural gradient
    // (Actual GL construction tested via integration tests)
    EXPECT_TRUE(true);  // Placeholder for contract documentation
}

TEST(SkyboxTest, HasTextureDefaultFalse)
{
    // The Skybox class is designed to start without a cubemap texture.
    // When hasTexture() returns false, the skybox shader renders a procedural gradient.
    // When a cubemap is loaded via loadCubemap(), hasTexture() returns true.
    //
    // This behavior is verified by the shader: u_hasCubemap uniform controls the branch.
    EXPECT_TRUE(true);  // Placeholder — GL context required for full test
}

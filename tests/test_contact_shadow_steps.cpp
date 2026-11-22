// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_contact_shadow_steps.cpp
/// @brief 3D_E-0874 lane-06 contact_shadows.frag.glsl 111 — a contact
///        shadow's strength fades with how far along the ray its occluder
///        is: an occluder k of N steps away gives 1 - (k/N)^2.
///
/// The march started two steps out and stepped again before its first
/// sample, so each sample was one step further than its loop index said:
/// every hit was graded one step too close (too dark), and the last sample
/// overshot the ray length.
///
/// Scene, all in view space (identity view): a wall facing the camera at
/// z = -10 fills the depth buffer, and a slab 0.4 nearer covers every
/// column from the one the ray's 4th step lands in. The light points along
/// +x, tilted slightly toward the camera, so the ray crosses the screen.
#include "gl_pass_helpers.h"
#include "gl_test_fixture.h"

#include "renderer/shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

using Vestige::Shader;
using Vestige::Test::drawFullScreenRed;
using Vestige::Test::FloatTexture;
using Vestige::Test::GLTestFixture;
using Vestige::Test::passShaderPath;

class ContactShadowStepsTest : public GLTestFixture {};

constexpr int   kSize      = 256;
constexpr float kNear      = 0.1f;
constexpr float kWallZ     = -10.0f;
constexpr float kSlabLift  = 0.4f;   // slab is this much nearer than the wall
constexpr float kRayLength = 4.0f;   // view distance 10 leaves it unscaled
constexpr int   kSteps     = 8;
constexpr int   kHitStep   = 4;      // the slab starts at the ray's 4th step

/// Reverse-Z infinite projection, 90 degree field of view, square: NDC depth
/// is near / -z, and [3][2] holds the near plane, as the shader reads it.
glm::mat4 projection()
{
    glm::mat4 p(0.0f);
    p[0][0] = 1.0f;
    p[1][1] = 1.0f;
    p[2][3] = -1.0f;
    p[3][2] = kNear;
    return p;
}

float depthAt(float viewZ) { return kNear / -viewZ; }

/// Screen u of a view-space point, as the shader projects a ray sample.
float screenU(const glm::vec3& p) { return (p.x / -p.z) * 0.5f + 0.5f; }

}  // namespace

TEST_F(ContactShadowStepsTest, ShadowStrengthFollowsTheOccludersDistanceAlongTheRay)
{
    Shader shader;
    ASSERT_TRUE(shader.loadFromFiles(passShaderPath("screen_quad.vert.glsl"),
                                     passShaderPath("contact_shadows.frag.glsl")));

    // The pixel under test: column 64, middle row.
    const int px = 64;
    const int py = kSize / 2;
    const float ndcX = ((static_cast<float>(px) + 0.5f) / kSize) * 2.0f - 1.0f;
    const float ndcY = ((static_cast<float>(py) + 0.5f) / kSize) * 2.0f - 1.0f;
    const glm::vec3 surface(ndcX * -kWallZ, ndcY * -kWallZ, kWallZ);

    const glm::vec3 towardLight = glm::normalize(glm::vec3(1.0f, 0.0f, 0.1f));
    const glm::vec3 step = towardLight * (kRayLength / kSteps);

    // The slab starts at the column the hit step's sample reads; the step
    // before must still read the wall.
    const auto column = [](float u) { return static_cast<int>(std::floor(u * kSize)); };
    const int slabFirst = column(screenU(surface + step * float(kHitStep)));
    ASSERT_LT(column(screenU(surface + step * float(kHitStep - 1))), slabFirst);
    ASSERT_GT(slabFirst, px + 2) << "the slab must not touch the pixel's own depth taps";

    std::vector<float> depth(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            depth[static_cast<std::size_t>(y) * kSize + x] =
                depthAt(x >= slabFirst ? kWallZ + kSlabLift : kWallZ);
        }
    }
    const FloatTexture depthTexture(kSize, kSize, depth);

    const glm::mat4 proj = projection();
    shader.use();
    depthTexture.bind(0);
    shader.setInt("u_depthTexture", 0);
    shader.setMat4("u_projection", proj);
    shader.setMat4("u_invProjection", glm::inverse(proj));
    shader.setMat4("u_view", glm::mat4(1.0f));
    shader.setVec3("u_lightDirection", -towardLight);
    shader.setVec2("u_texelSize", glm::vec2(1.0f / kSize));
    shader.setFloat("u_rayLength", kRayLength);
    shader.setInt("u_numSteps", kSteps);

    const std::vector<float> out = drawFullScreenRed(kSize, kSize);
    ASSERT_EQ(out.size(), depth.size());

    // The wall faces +z, so N.L is the light's z; the shader fades the
    // shadow by 1 - smoothstep(0, 0.5, N.L).
    const float nDotL = towardLight.z;
    const float s = std::clamp(nDotL / 0.5f, 0.0f, 1.0f);
    const float normalFade = 1.0f - s * s * (3.0f - 2.0f * s);
    const float t = static_cast<float>(kHitStep) / kSteps;
    const float expected = 1.0f - 0.6f * (1.0f - t * t) * normalFade;

    EXPECT_NEAR(out[static_cast<std::size_t>(py) * kSize + px], expected, 0.01f)
        << "an occluder " << kHitStep << " of " << kSteps
        << " steps away should give the shadow of t = " << t;
}

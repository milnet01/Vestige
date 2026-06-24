// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gi_probe_math.cpp
/// @brief Phase 13 G1 — GL-free unit tests for the RSM flux CPU spec
///        (gi_probe_math.h). The GPU parity test (test_gi_probe_gpu.cpp) pins the
///        directional formula against the production GLSL; these pin the CPU math
///        itself (directional + point, including attenuation) without a context.

#include "renderer/gi_probe_math.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

namespace Vestige::Test
{

TEST(GiProbeMath, DirectionalFullCosineWhenFacingLight)
{
    // Normal up, light travels straight down ⇒ N·L = 1 ⇒ flux = albedo·radiance.
    glm::vec4 f = giRsmFluxDirectional({0.8f, 0.4f, 0.2f}, {1.0f, 1.0f, 1.0f},
                                       {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
    EXPECT_NEAR(f.r, 0.8f, 1e-6f);
    EXPECT_NEAR(f.g, 0.4f, 1e-6f);
    EXPECT_NEAR(f.b, 0.2f, 1e-6f);
    EXPECT_NEAR(f.a, 1.0f, 1e-6f) << "alpha is the coverage marker";
}

TEST(GiProbeMath, DirectionalBackFacingClampsToZero)
{
    // Normal points the same way the light travels ⇒ N·L < 0 ⇒ clamped to 0.
    glm::vec4 f = giRsmFluxDirectional({1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
                                       {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
    EXPECT_NEAR(f.r, 0.0f, 1e-6f);
    EXPECT_NEAR(f.g, 0.0f, 1e-6f);
    EXPECT_NEAR(f.b, 0.0f, 1e-6f);
    EXPECT_NEAR(f.a, 1.0f, 1e-6f) << "coverage still marked even at zero flux";
}

TEST(GiProbeMath, DirectionalRenormalisesNonUnitNormal)
{
    glm::vec4 unit = giRsmFluxDirectional({0.5f, 0.5f, 0.5f}, {2.0f, 2.0f, 2.0f},
                                          {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
    glm::vec4 scaled = giRsmFluxDirectional({0.5f, 0.5f, 0.5f}, {2.0f, 2.0f, 2.0f},
                                            {0.0f, 7.3f, 0.0f}, {0.0f, -1.0f, 0.0f});
    EXPECT_NEAR(scaled.r, unit.r, 1e-6f) << "normal magnitude must not affect flux";
}

TEST(GiProbeMath, PointAttenuatesWithDistance)
{
    const glm::vec3 albedo{1.0f}, radiance{1.0f}, normal{0.0f, 1.0f, 0.0f};
    const float kc = 1.0f, kl = 0.0f, kq = 0.0f;  // constant-only ⇒ atten = 1

    // Light 1m directly above ⇒ N·L = 1, atten = 1 ⇒ flux = 1.
    glm::vec4 near = giRsmFluxPoint(albedo, radiance, {0.0f, 0.0f, 0.0f}, normal,
                                    {0.0f, 1.0f, 0.0f}, kc, kl, kq);
    EXPECT_NEAR(near.r, 1.0f, 1e-6f);

    // Same geometry but with quadratic falloff ⇒ flux must drop below the
    // unattenuated value as distance grows.
    glm::vec4 far = giRsmFluxPoint(albedo, radiance, {0.0f, 0.0f, 0.0f}, normal,
                                   {0.0f, 4.0f, 0.0f}, 1.0f, 0.09f, 0.032f);
    EXPECT_LT(far.r, near.r) << "quadratic attenuation must reduce distant flux";
    EXPECT_GT(far.r, 0.0f);
}

}  // namespace Vestige::Test

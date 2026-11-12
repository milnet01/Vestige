// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_cloth_friction.cpp
/// @brief Cloth friction slows sliding on both backends (3D_E-0875).
///
/// A flat cloth rests on a floor (a plane collider at y = 0, which has no
/// collision margin on either backend) while gravity is tilted 30 degrees
/// sideways. That is a 30-degree slope: the cloth slides along +X unless
/// friction holds it. Coulomb's law gives three outcomes:
///   - no friction: it slides freely;
///   - static 0.9, above tan(30) ~ 0.577, with kinetic 0.3 below it: it
///     stays put, held by static friction alone (kinetic alone slides);
///   - kinetic coefficient 0.3 (below tan 30): it slides, with acceleration
///     g (sin 30 - 0.3 cos 30), about 0.48 of the frictionless value.
/// Before the fix both backends edited velocity in the collision pass and
/// then rebuilt velocity from positions, so all three slid the same.

#include <gtest/gtest.h>

#include "gl_test_fixture.h"
#include "physics/cloth_simulator.h"
#include "physics/gpu_cloth_simulator.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Vestige;

namespace
{

constexpr float kSlopeRadians = 0.5235988f;  // 30 degrees
constexpr int   kFrames = 60;                // one second at 60 Hz

ClothConfig slopeConfig()
{
    ClothConfig cfg;
    cfg.width = 4;
    cfg.height = 4;
    cfg.spacing = 0.1f;
    cfg.particleMass = 0.1f;
    cfg.substeps = 10;
    cfg.damping = 0.0f;         // Damping would slow the slide on its own.
    cfg.sleepThreshold = 0.0f;  // A slow first frame must not put it to sleep.
    cfg.gravity = 9.81f * glm::vec3(std::sin(kSlopeRadians), -std::cos(kSlopeRadians), 0.0f);
    return cfg;
}

/// Mean X travel of every particle after one second on the slope.
float slideDistance(IClothSolverBackend& sim, float staticMu, float kineticMu)
{
    sim.setWind(glm::vec3(1.0f, 0.0f, 0.0f), 0.0f);
    sim.addPlaneCollider(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
    sim.setFriction(staticMu, kineticMu);

    const uint32_t n = sim.getParticleCount();
    float startX = 0.0f;
    const glm::vec3* p = sim.getPositions();
    for (uint32_t i = 0; i < n; ++i) startX += p[i].x;

    for (int f = 0; f < kFrames; ++f) sim.simulate(1.0f / 60.0f);

    float endX = 0.0f;
    p = sim.getPositions();
    for (uint32_t i = 0; i < n; ++i) endX += p[i].x;
    return (endX - startX) / static_cast<float>(n);
}

float cpuSlide(float staticMu, float kineticMu)
{
    ClothSimulator sim;
    sim.initialize(slopeConfig());
    return slideDistance(sim, staticMu, kineticMu);
}

/// How far past its tether the worst particle ends, in metres, when a
/// plane collider lifts a cloth half a metre above its one pinned corner.
/// The tether (each particle's rest distance to the pin) is far shorter.
float tetherOverrun(IClothSolverBackend& sim)
{
    const uint32_t n = sim.getParticleCount();
    const std::vector<glm::vec3> rest(sim.getPositions(), sim.getPositions() + n);
    sim.setWind(glm::vec3(1.0f, 0.0f, 0.0f), 0.0f);
    sim.pinParticle(0, rest[0]);
    sim.rebuildLRA();
    sim.addPlaneCollider(glm::vec3(0.0f, 1.0f, 0.0f), rest[0].y + 0.5f);

    for (int f = 0; f < 30; ++f) sim.simulate(1.0f / 60.0f);

    const glm::vec3* p = sim.getPositions();
    float worst = 0.0f;
    for (uint32_t i = 1; i < n; ++i)
    {
        worst = std::max(worst, glm::length(p[i] - p[0]) - glm::length(rest[i] - rest[0]));
    }
    return worst;
}

ClothConfig flatConfig()
{
    ClothConfig cfg = slopeConfig();
    cfg.gravity = glm::vec3(0.0f, -9.81f, 0.0f);
    return cfg;
}

}  // namespace

// 3D_E-0875 follow-up: the CPU solved tethers before collisions and the GPU
// after, so where a collider and a tether disagreed the two backends ended
// in different places. Both now end the substep on the tether.
TEST(ClothTetherOrder, CpuTetherHoldsAgainstACollider)
{
    ClothSimulator sim;
    sim.initialize(flatConfig());
    EXPECT_LT(tetherOverrun(sim), 1e-3f);
}

TEST(ClothFrictionSlope, CpuFrictionlessClothSlides)
{
    // 0.5 g sin30 t^2 = 2.45 m with no losses; the speed cap and contact
    // handling lose a little.
    EXPECT_GT(cpuSlide(0.0f, 0.0f), 2.0f);
}

TEST(ClothFrictionSlope, CpuStaticFrictionHoldsClothOnSlope)
{
    const float slid = cpuSlide(0.9f, 0.3f);
    EXPECT_LT(std::abs(slid), 0.01f)
        << "static friction 0.9 exceeds tan(30 deg), so the cloth should "
           "stay put; it slid " << slid << " m";
}

TEST(ClothFrictionSlope, CpuKineticFrictionSlowsTheSlide)
{
    const float free = cpuSlide(0.0f, 0.0f);
    const float slowed = cpuSlide(0.3f, 0.3f);
    // Coulomb: (sin30 - 0.3 cos30) / sin30 = 0.48.
    EXPECT_NEAR(slowed / free, 0.48f, 0.1f)
        << "frictionless " << free << " m, kinetic 0.3 " << slowed << " m";
}

class GpuClothFrictionSlope : public ::Vestige::Test::GLTestFixture
{
protected:
    /// Returns NaN when the GPU pipeline is unavailable.
    static float gpuSlide(float staticMu, float kineticMu)
    {
        GpuClothSimulator sim;
        sim.setShaderPath(VESTIGE_SHADER_DIR);
        sim.initialize(slopeConfig(), /*seed=*/0);
        if (!sim.isInitialized() || !sim.hasShaders()) return std::nanf("");
        sim.setWindQuality(ClothWindQuality::SIMPLE);
        return slideDistance(sim, staticMu, kineticMu);
    }
};

TEST_F(GpuClothFrictionSlope, StaticHoldsAndKineticMatchesCpu)
{
    const float free = gpuSlide(0.0f, 0.0f);
    if (std::isnan(free)) GTEST_SKIP() << "GPU cloth compute pipeline not available";

    EXPECT_GT(free, 2.0f);

    const float held = gpuSlide(0.9f, 0.3f);
    EXPECT_LT(std::abs(held), 0.01f) << "GPU cloth slid " << held << " m under static friction";

    const float slowed = gpuSlide(0.3f, 0.3f);
    EXPECT_NEAR(slowed / free, 0.48f, 0.1f)
        << "GPU frictionless " << free << " m, kinetic 0.3 " << slowed << " m";

    // Parity with the CPU backend, the reference implementation.
    EXPECT_NEAR(slowed, cpuSlide(0.3f, 0.3f), 0.05f);
}

TEST_F(GpuClothFrictionSlope, TetherHoldsAgainstAColliderAsOnTheCpu)
{
    GpuClothSimulator sim;
    sim.setShaderPath(VESTIGE_SHADER_DIR);
    sim.initialize(flatConfig(), /*seed=*/0);
    if (!sim.isInitialized() || !sim.hasShaders())
    {
        GTEST_SKIP() << "GPU cloth compute pipeline not available";
    }
    sim.setWindQuality(ClothWindQuality::SIMPLE);
    EXPECT_LT(tetherOverrun(sim), 1e-3f);
}

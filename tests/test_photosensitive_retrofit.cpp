// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_photosensitive_retrofit.cpp
/// @brief Phase 10.7 slice C — regression tests for the photosensitive
///        consumer retrofits. Covers the particle-emitter flicker
///        clamp (C2) through its public `getCoupledLight` surface.
///
/// The bloom retrofit (C1) is validated indirectly through the
/// underlying `limitBloomIntensity` helper's existing tests in
/// `test_photosensitive_safety.cpp`; its renderer wire-up is a
/// one-line clamp at the uniform-upload site that would need a live
/// GL context to test directly, which is outside the unit-test
/// budget.

#include <gtest/gtest.h>

#include "accessibility/photosensitive_safety.h"
#include "scene/particle_emitter.h"

#include <glm/glm.hpp>

using namespace Vestige;

namespace
{

// Build a minimal fire emitter with one live particle so
// `getCoupledLight` returns a value instead of nullopt.
ParticleEmitterComponent makeLivingFireEmitter(float flickerSpeed,
                                                float lightIntensity = 1.0f)
{
    ParticleEmitterComponent emitter;
    auto& cfg = emitter.getConfig();
    cfg.emitsLight     = true;
    cfg.flickerSpeed   = flickerSpeed;
    cfg.lightColor     = glm::vec3(1.0f);
    cfg.lightIntensity = lightIntensity;
    cfg.lightRange     = 5.0f;
    // Run one update tick with a positive emission rate so the
    // emitter spawns at least one particle. That gives `m_data.count
    // > 0` and `getCoupledLight` returns a value.
    cfg.emissionRate = 10.0f;
    cfg.maxParticles = 16;
    // update() spawns particles; ensure the data container is
    // pre-sized.
    for (int i = 0; i < 4; ++i)
    {
        emitter.update(0.1f);
    }
    return emitter;
}

} // namespace

// -- C2: particle-emitter flicker clamp --

TEST(PhotosensitiveFlicker, DisabledModeReturnsCoupledLightUnchanged)
{
    // With safe mode off the flicker signal comes out exactly as the
    // pre-retrofit implementation would have produced it. The easiest
    // way to verify this is to compare two calls: safe-mode-off vs.
    // safe-mode-on with limits so wide they are inert. Both should
    // produce identical diffuse colour.
    const float flickerSpeed = 12.0f; // ~1.91 Hz dominant
    auto emitter = makeLivingFireEmitter(flickerSpeed);

    auto lightOff = emitter.getCoupledLight(glm::vec3(0.0f), false);
    PhotosensitiveLimits openLimits;
    openLimits.maxStrobeHz = 1000.0f; // inert ceiling
    auto lightOnOpen =
        emitter.getCoupledLight(glm::vec3(0.0f), true, openLimits);

    ASSERT_TRUE(lightOff.has_value());
    ASSERT_TRUE(lightOnOpen.has_value());
    EXPECT_FLOAT_EQ(lightOff->diffuse.r, lightOnOpen->diffuse.r);
}

TEST(PhotosensitiveFlicker, SafeModeClampsAboveCeilingEmitter)
{
    // Authored flicker well above the 2 Hz default ceiling. The
    // authored dominant frequency at flickerSpeed=20 is
    //   20 / (2π) ≈ 3.18 Hz — above 2.0 Hz cap.
    // Safe mode should therefore clamp the effective base frequency.
    // We verify the behaviour behaviourally: at the same
    // m_elapsedTime, the effective-speed-clamped version should
    // produce a different flicker phase than the unclamped version.
    auto emitterA = makeLivingFireEmitter(20.0f, 2.0f);
    auto emitterB = makeLivingFireEmitter(20.0f, 2.0f);

    PhotosensitiveLimits limits;
    limits.maxStrobeHz = 2.0f;

    auto lightOff = emitterA.getCoupledLight(glm::vec3(0.0f), false);
    auto lightOn  = emitterB.getCoupledLight(glm::vec3(0.0f), true, limits);

    ASSERT_TRUE(lightOff.has_value());
    ASSERT_TRUE(lightOn.has_value());
    EXPECT_NE(lightOff->diffuse.r, lightOn->diffuse.r);
}

TEST(PhotosensitiveFlicker, SafeModeBelowCeilingIsIdentityPass)
{
    // Default flickerSpeed of 10 has dominant ≈ 1.59 Hz — below the
    // 2 Hz default cap. Safe mode on should produce the *same* light
    // as safe mode off.
    const float flickerSpeed = 10.0f;
    auto emitterA = makeLivingFireEmitter(flickerSpeed);
    auto emitterB = makeLivingFireEmitter(flickerSpeed);

    auto lightOff = emitterA.getCoupledLight(glm::vec3(0.0f), false);
    auto lightOn  = emitterB.getCoupledLight(glm::vec3(0.0f), true);

    ASSERT_TRUE(lightOff.has_value());
    ASSERT_TRUE(lightOn.has_value());
    EXPECT_FLOAT_EQ(lightOff->diffuse.r, lightOn->diffuse.r);
}

TEST(PhotosensitiveFlicker, CoupledLightStillEmittedWhenClamped)
{
    // Safe-mode clamp should not suppress the coupled light entirely
    // — only slow its flicker. The returned light must still be
    // non-empty and have non-zero diffuse intensity.
    auto emitter = makeLivingFireEmitter(20.0f, 5.0f);
    PhotosensitiveLimits limits;
    limits.maxStrobeHz = 2.0f;

    auto light = emitter.getCoupledLight(glm::vec3(0.0f), true, limits);
    ASSERT_TRUE(light.has_value());
    EXPECT_GT(light->diffuse.r, 0.0f);
}

// -- Sanity for the Hz conversion used in the retrofit --

TEST(PhotosensitiveFlicker, AuthoredSpeedConvertsToExpectedHz)
{
    // Sanity-check that clampStrobeHz with the same conversion
    // reproduces the expected cap. flickerSpeed=20 → 3.18 Hz →
    // clamp at 2 Hz → effective flickerSpeed = 2 * 2π ≈ 12.566.
    static constexpr float TWO_PI = 6.28318530717958647692f;
    const float authoredSpeed = 20.0f;
    const float authoredHz    = authoredSpeed / TWO_PI;

    PhotosensitiveLimits limits;
    limits.maxStrobeHz = 2.0f;

    const float safeHz    = clampStrobeHz(authoredHz, true, limits);
    const float safeSpeed = safeHz * TWO_PI;

    EXPECT_NEAR(authoredHz, 3.18309886f, 1e-4f);
    EXPECT_FLOAT_EQ(safeHz, 2.0f);
    EXPECT_NEAR(safeSpeed, 12.566371f, 1e-4f);
}

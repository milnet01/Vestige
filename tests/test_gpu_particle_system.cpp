// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_particle_system.cpp
/// @brief Unit tests for the GPU particle system (CPU-side logic).
///
/// GPU-specific tests (compute dispatch, indirect draw) require an OpenGL context
/// and are tested visually. These tests cover the CPU-side configuration, behavior
/// composition, emission parameter building, and data structures.

#include "renderer/gpu_particle_system.h"
#include "scene/gpu_particle_emitter.h"
#include "scene/particle_emitter.h"

#include <gtest/gtest.h>
#include <glm/gtc/constants.hpp>

#include <cstring>

using namespace Vestige;

// ===========================================================================
// BehaviorBlock tests
// ===========================================================================

class BehaviorBlockTest : public ::testing::Test
{
protected:
    BehaviorBlock block;
};

TEST_F(BehaviorBlockTest, DefaultEmpty)
{
    EXPECT_EQ(block.behaviorCount, 0);
    EXPECT_EQ(block.colorStopCount, 0);
    EXPECT_EQ(block.sizeKeyCount, 0);
    EXPECT_EQ(block.speedKeyCount, 0);
}

TEST_F(BehaviorBlockTest, AddGravityBehavior)
{
    block.behaviors[0].type = ParticleBehaviorType::GRAVITY;
    block.behaviors[0].params[0] = 0.0f;
    block.behaviors[0].params[1] = -9.81f;
    block.behaviors[0].params[2] = 0.0f;
    block.behaviorCount = 1;

    EXPECT_EQ(block.behaviorCount, 1);
    EXPECT_EQ(block.behaviors[0].type, ParticleBehaviorType::GRAVITY);
    EXPECT_FLOAT_EQ(block.behaviors[0].params[1], -9.81f);
}

TEST_F(BehaviorBlockTest, MultipleBehaviors)
{
    // Add gravity
    block.behaviors[0].type = ParticleBehaviorType::GRAVITY;
    block.behaviors[0].params[1] = -9.81f;

    // Add drag
    block.behaviors[1].type = ParticleBehaviorType::DRAG;
    block.behaviors[1].params[0] = 0.5f;

    // Add noise
    block.behaviors[2].type = ParticleBehaviorType::NOISE;
    block.behaviors[2].params[0] = 2.0f;  // frequency
    block.behaviors[2].params[1] = 0.3f;  // amplitude

    block.behaviorCount = 3;

    EXPECT_EQ(block.behaviorCount, 3);
    EXPECT_EQ(block.behaviors[1].type, ParticleBehaviorType::DRAG);
    EXPECT_FLOAT_EQ(block.behaviors[2].params[0], 2.0f);
}

TEST_F(BehaviorBlockTest, ColorGradientStops)
{
    block.colorStopCount = 3;
    block.colorStops[0] = glm::vec4(1, 1, 0, 1);   // Yellow
    block.colorStopTimes[0] = 0.0f;
    block.colorStops[1] = glm::vec4(1, 0.3f, 0, 1); // Orange
    block.colorStopTimes[1] = 0.5f;
    block.colorStops[2] = glm::vec4(0, 0, 0, 0);    // Black/transparent
    block.colorStopTimes[2] = 1.0f;

    EXPECT_EQ(block.colorStopCount, 3);
    EXPECT_FLOAT_EQ(block.colorStopTimes[1], 0.5f);
    EXPECT_FLOAT_EQ(block.colorStops[0].r, 1.0f);
}

TEST_F(BehaviorBlockTest, SizeOverLifetimeKeys)
{
    block.sizeKeyCount = 4;
    block.sizeKeys[0] = 0.0f;
    block.sizeKeyTimes[0] = 0.0f;
    block.sizeKeys[1] = 1.0f;
    block.sizeKeyTimes[1] = 0.2f;
    block.sizeKeys[2] = 1.0f;
    block.sizeKeyTimes[2] = 0.8f;
    block.sizeKeys[3] = 0.0f;
    block.sizeKeyTimes[3] = 1.0f;

    EXPECT_EQ(block.sizeKeyCount, 4);
    EXPECT_FLOAT_EQ(block.sizeKeys[1], 1.0f);
}

TEST_F(BehaviorBlockTest, MaxBehaviors)
{
    // Fill all 16 slots
    for (int i = 0; i < 16; ++i)
    {
        block.behaviors[i].type = ParticleBehaviorType::GRAVITY;
        block.behaviors[i].params[1] = -1.0f * (i + 1);
    }
    block.behaviorCount = 16;

    EXPECT_EQ(block.behaviorCount, 16);
    EXPECT_FLOAT_EQ(block.behaviors[15].params[1], -16.0f);
}

// ===========================================================================
// EmissionParams tests
// ===========================================================================

class EmissionParamsTest : public ::testing::Test
{
protected:
    EmissionParams params;
};

TEST_F(EmissionParamsTest, DefaultValues)
{
    EXPECT_FLOAT_EQ(params.worldPosition.x, 0.0f);
    EXPECT_FLOAT_EQ(params.worldPosition.y, 0.0f);
    EXPECT_FLOAT_EQ(params.worldPosition.z, 0.0f);
    EXPECT_EQ(params.shapeType, 0u);
    EXPECT_FLOAT_EQ(params.startLifetimeMin, 1.0f);
    EXPECT_FLOAT_EQ(params.startLifetimeMax, 3.0f);
    EXPECT_FLOAT_EQ(params.startColor.a, 1.0f);
}

TEST_F(EmissionParamsTest, PointShape)
{
    params.shapeType = 0; // Point
    EXPECT_EQ(params.shapeType, 0u);
}

TEST_F(EmissionParamsTest, SphereShape)
{
    params.shapeType = 1;
    params.shapeRadius = 2.5f;
    EXPECT_EQ(params.shapeType, 1u);
    EXPECT_FLOAT_EQ(params.shapeRadius, 2.5f);
}

TEST_F(EmissionParamsTest, ConeShape)
{
    params.shapeType = 2;
    params.shapeRadius = 0.5f;
    params.shapeConeAngle = glm::radians(30.0f);
    EXPECT_EQ(params.shapeType, 2u);
    EXPECT_NEAR(params.shapeConeAngle, 0.5236f, 0.001f);
}

TEST_F(EmissionParamsTest, BoxShape)
{
    params.shapeType = 3;
    params.shapeBoxSize = glm::vec3(2.0f, 1.0f, 3.0f);
    EXPECT_EQ(params.shapeType, 3u);
    EXPECT_FLOAT_EQ(params.shapeBoxSize.y, 1.0f);
}

TEST_F(EmissionParamsTest, CustomStartProperties)
{
    params.startLifetimeMin = 0.5f;
    params.startLifetimeMax = 1.5f;
    params.startSpeedMin = 2.0f;
    params.startSpeedMax = 5.0f;
    params.startSizeMin = 0.01f;
    params.startSizeMax = 0.1f;
    params.startColor = glm::vec4(1.0f, 0.5f, 0.0f, 0.8f);

    EXPECT_FLOAT_EQ(params.startLifetimeMin, 0.5f);
    EXPECT_FLOAT_EQ(params.startSpeedMax, 5.0f);
    EXPECT_FLOAT_EQ(params.startColor.g, 0.5f);
}

// ===========================================================================
// ParticleBehaviorType tests
// ===========================================================================

TEST(ParticleBehaviorTypeTest, EnumValues)
{
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::GRAVITY), 0u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::DRAG), 1u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::NOISE), 2u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::ORBIT), 3u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::ATTRACT), 4u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::VORTEX), 5u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::TURBULENCE), 6u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::WIND), 7u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::DEPTH_COLLISION), 10u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::GROUND_PLANE), 11u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::SPHERE_COLLIDER), 12u);
    EXPECT_EQ(static_cast<uint32_t>(ParticleBehaviorType::KILL_ON_COLLISION), 20u);
}

// ===========================================================================
// GPUParticleEmitter tests (CPU-side logic only)
// ===========================================================================

class GPUParticleEmitterTest : public ::testing::Test
{
protected:
    GPUParticleEmitter emitter;
};

TEST_F(GPUParticleEmitterTest, DefaultConfig)
{
    const auto& config = emitter.getConfig();
    EXPECT_FLOAT_EQ(config.emissionRate, 10.0f);
    EXPECT_EQ(config.maxParticles, 1000);
    EXPECT_TRUE(config.looping);
}

TEST_F(GPUParticleEmitterTest, SetConfig)
{
    ParticleEmitterConfig config;
    config.emissionRate = 50.0f;
    config.maxParticles = 5000;
    config.startSizeMin = 0.2f;
    config.startSizeMax = 0.8f;
    config.gravity = glm::vec3(0, -5, 0);

    emitter.setConfig(config);

    const auto& result = emitter.getConfig();
    EXPECT_FLOAT_EQ(result.emissionRate, 50.0f);
    EXPECT_EQ(result.maxParticles, 5000);
    EXPECT_FLOAT_EQ(result.startSizeMin, 0.2f);
}

TEST_F(GPUParticleEmitterTest, AddBehavior)
{
    BehaviorParams gp;
    gp.values[0] = 0.0f;
    gp.values[1] = -9.81f;
    gp.values[2] = 0.0f;

    emitter.addBehavior(ParticleBehaviorType::GRAVITY, gp);
    EXPECT_EQ(emitter.getBehaviorCount(), 1);

    BehaviorParams dp;
    dp.values[0] = 0.5f;
    emitter.addBehavior(ParticleBehaviorType::DRAG, dp);
    EXPECT_EQ(emitter.getBehaviorCount(), 2);
}

TEST_F(GPUParticleEmitterTest, RemoveBehavior)
{
    BehaviorParams gp;
    gp.values[1] = -9.81f;
    emitter.addBehavior(ParticleBehaviorType::GRAVITY, gp);

    BehaviorParams dp;
    dp.values[0] = 0.5f;
    emitter.addBehavior(ParticleBehaviorType::DRAG, dp);

    EXPECT_EQ(emitter.getBehaviorCount(), 2);

    emitter.removeBehavior(ParticleBehaviorType::GRAVITY);
    EXPECT_EQ(emitter.getBehaviorCount(), 1);
}

TEST_F(GPUParticleEmitterTest, ClearBehaviors)
{
    BehaviorParams gp;
    emitter.addBehavior(ParticleBehaviorType::GRAVITY, gp);
    emitter.addBehavior(ParticleBehaviorType::DRAG, gp);
    emitter.addBehavior(ParticleBehaviorType::NOISE, gp);

    emitter.clearBehaviors();
    EXPECT_EQ(emitter.getBehaviorCount(), 0);
}

TEST_F(GPUParticleEmitterTest, PauseResume)
{
    EXPECT_FALSE(emitter.isPaused());
    emitter.setPaused(true);
    EXPECT_TRUE(emitter.isPaused());
    emitter.setPaused(false);
    EXPECT_FALSE(emitter.isPaused());
}

TEST_F(GPUParticleEmitterTest, NotInitializedIsNotGPU)
{
    // Without init(), GPU path is unavailable
    EXPECT_FALSE(emitter.isGPUPath());
}

TEST_F(GPUParticleEmitterTest, BlendModeDefault)
{
    EXPECT_EQ(emitter.getBlendMode(), ParticleEmitterConfig::BlendMode::ADDITIVE);
}

TEST_F(GPUParticleEmitterTest, NeedsSortingForAlphaBlend)
{
    // Default is ADDITIVE — no sorting needed
    EXPECT_FALSE(emitter.needsSorting());

    // Switch to ALPHA_BLEND — sorting needed
    auto config = emitter.getConfig();
    config.blendMode = ParticleEmitterConfig::BlendMode::ALPHA_BLEND;
    emitter.setConfig(config);
    EXPECT_TRUE(emitter.needsSorting());
}

TEST_F(GPUParticleEmitterTest, TexturePath)
{
    EXPECT_TRUE(emitter.getTexturePath().empty());

    auto config = emitter.getConfig();
    config.texturePath = "textures/fire.png";
    emitter.setConfig(config);
    EXPECT_EQ(emitter.getTexturePath(), "textures/fire.png");
}

TEST_F(GPUParticleEmitterTest, Clone)
{
    auto config = emitter.getConfig();
    config.emissionRate = 100.0f;
    config.maxParticles = 10000;
    emitter.setConfig(config);

    // Add a drag behavior on top of the auto-generated gravity
    BehaviorParams dp;
    dp.values[0] = 0.5f;
    emitter.addBehavior(ParticleBehaviorType::DRAG, dp);

    int originalCount = emitter.getBehaviorCount();

    auto cloned = emitter.clone();
    auto* gpuClone = dynamic_cast<GPUParticleEmitter*>(cloned.get());
    ASSERT_NE(gpuClone, nullptr);

    EXPECT_FLOAT_EQ(gpuClone->getConfig().emissionRate, 100.0f);
    EXPECT_EQ(gpuClone->getConfig().maxParticles, 10000);
    EXPECT_EQ(gpuClone->getBehaviorCount(), originalCount);
}

TEST_F(GPUParticleEmitterTest, BuildBehaviorsFromConfig)
{
    auto config = emitter.getConfig();
    config.gravity = glm::vec3(0, -5, 0);
    emitter.setConfig(config);

    // setConfig calls buildBehaviorsFromConfig internally
    // Should have gravity behavior
    EXPECT_GE(emitter.getBehaviorCount(), 1);
}

TEST_F(GPUParticleEmitterTest, BuildBehaviorsNoGravityWhenZero)
{
    auto config = emitter.getConfig();
    config.gravity = glm::vec3(0, 0, 0);
    emitter.setConfig(config);

    EXPECT_EQ(emitter.getBehaviorCount(), 0);
}

// ===========================================================================
// GPUParticleSystem tests (CPU-side only, no OpenGL context)
// ===========================================================================

class GPUParticleSystemTest : public ::testing::Test
{
protected:
    GPUParticleSystem system;
};

TEST_F(GPUParticleSystemTest, NotInitializedByDefault)
{
    EXPECT_FALSE(system.isInitialized());
    EXPECT_EQ(system.getMaxParticles(), 0u);
}

TEST_F(GPUParticleSystemTest, ReadAliveCountWhenNotInitialized)
{
    EXPECT_EQ(system.readAliveCount(), 0u);
}

TEST_F(GPUParticleSystemTest, ReadDeadCountWhenNotInitialized)
{
    EXPECT_EQ(system.readDeadCount(), 0u);
}

TEST_F(GPUParticleSystemTest, GetParticleSSBOWhenNotInitialized)
{
    EXPECT_EQ(system.getParticleSSBO(), 0u);
}

TEST_F(GPUParticleSystemTest, GetIndirectBufferWhenNotInitialized)
{
    EXPECT_EQ(system.getIndirectBuffer(), 0u);
}

// ===========================================================================
// Integration: behavior composition for common effects
// ===========================================================================

TEST(GPUParticleBehaviorComposition, FireEffect)
{
    GPUParticleEmitter emitter;

    // Configure like a fire
    auto config = emitter.getConfig();
    config.emissionRate = 80.0f;
    config.maxParticles = 200;
    config.shape = ParticleEmitterConfig::Shape::CONE;
    config.shapeConeAngle = 15.0f;
    config.startSpeedMin = 1.0f;
    config.startSpeedMax = 2.0f;
    config.startSizeMin = 0.1f;
    config.startSizeMax = 0.3f;
    config.gravity = glm::vec3(0, 0.5f, 0); // Slight upward
    config.blendMode = ParticleEmitterConfig::BlendMode::ADDITIVE;
    emitter.setConfig(config);

    // Add turbulence for flickering
    BehaviorParams turbParams;
    turbParams.values[0] = 3.0f;  // scale
    turbParams.values[1] = 1.5f;  // intensity
    emitter.addBehavior(ParticleBehaviorType::TURBULENCE, turbParams);

    EXPECT_GE(emitter.getBehaviorCount(), 2); // gravity + turbulence
    EXPECT_FALSE(emitter.needsSorting()); // ADDITIVE doesn't need sorting
}

TEST(GPUParticleBehaviorComposition, SmokeEffect)
{
    GPUParticleEmitter emitter;

    auto config = emitter.getConfig();
    config.emissionRate = 20.0f;
    config.maxParticles = 100;
    config.shape = ParticleEmitterConfig::Shape::CONE;
    config.shapeConeAngle = 10.0f;
    config.gravity = glm::vec3(0, 0.3f, 0);
    config.blendMode = ParticleEmitterConfig::BlendMode::ALPHA_BLEND;
    config.useSizeOverLifetime = true;
    config.sizeOverLifetime.keyframes = {
        {0.0f, 1.0f}, {0.5f, 2.5f}, {1.0f, 4.0f}
    };
    emitter.setConfig(config);

    // Add drag for slow expansion
    BehaviorParams dragParams;
    dragParams.values[0] = 2.0f;
    emitter.addBehavior(ParticleBehaviorType::DRAG, dragParams);

    // Add wind
    BehaviorParams windParams;
    windParams.values[0] = 1.0f;  // X direction
    windParams.values[1] = 0.0f;
    windParams.values[2] = 0.0f;
    windParams.values[3] = 0.5f;  // Strength
    emitter.addBehavior(ParticleBehaviorType::WIND, windParams);

    EXPECT_TRUE(emitter.needsSorting()); // ALPHA_BLEND needs sorting
    EXPECT_GE(emitter.getBehaviorCount(), 3); // gravity + drag + wind
}

TEST(GPUParticleBehaviorComposition, SparkEffect)
{
    GPUParticleEmitter emitter;

    auto config = emitter.getConfig();
    config.emissionRate = 30.0f;
    config.maxParticles = 100;
    config.shape = ParticleEmitterConfig::Shape::SPHERE;
    config.shapeRadius = 0.1f;
    config.startSpeedMin = 3.0f;
    config.startSpeedMax = 6.0f;
    config.gravity = glm::vec3(0, -9.81f, 0);
    config.blendMode = ParticleEmitterConfig::BlendMode::ADDITIVE;
    emitter.setConfig(config);

    // Add ground collision with bounce
    BehaviorParams groundParams;
    groundParams.values[0] = 0.0f;   // Ground Y
    groundParams.values[1] = 0.6f;   // Restitution
    emitter.addBehavior(ParticleBehaviorType::GROUND_PLANE, groundParams);

    EXPECT_GE(emitter.getBehaviorCount(), 2); // gravity + ground
}

TEST(GPUParticleBehaviorComposition, VortexEffect)
{
    GPUParticleEmitter emitter;

    auto config = emitter.getConfig();
    config.emissionRate = 100.0f;
    config.maxParticles = 2000;
    config.shape = ParticleEmitterConfig::Shape::SPHERE;
    config.shapeRadius = 1.0f;
    config.gravity = glm::vec3(0, 0, 0); // No gravity
    emitter.setConfig(config);

    // Add vortex behavior
    BehaviorParams vortexParams;
    vortexParams.values[0] = 0.0f;  // Axis X
    vortexParams.values[1] = 1.0f;  // Axis Y (vertical spiral)
    vortexParams.values[2] = 0.0f;  // Axis Z
    vortexParams.values[3] = 5.0f;  // Rotational speed
    vortexParams.values[4] = 1.0f;  // Pull toward center
    emitter.addBehavior(ParticleBehaviorType::VORTEX, vortexParams);

    // Add attract toward center
    BehaviorParams attractParams;
    attractParams.values[0] = 0.0f;  // Target X
    attractParams.values[1] = 2.0f;  // Target Y
    attractParams.values[2] = 0.0f;  // Target Z
    attractParams.values[3] = 3.0f;  // Strength
    attractParams.values[4] = 5.0f;  // Range
    emitter.addBehavior(ParticleBehaviorType::ATTRACT, attractParams);

    // No gravity behavior since gravity is zero
    EXPECT_EQ(emitter.getBehaviorCount(), 2); // vortex + attract
}

// ===========================================================================
// BehaviorParams tests
// ===========================================================================

TEST(BehaviorParamsTest, DefaultZero)
{
    BehaviorParams params;
    for (int i = 0; i < 6; ++i)
        EXPECT_FLOAT_EQ(params.values[i], 0.0f);
}

// ===========================================================================
// GPU particle struct size validation
// ===========================================================================

TEST(GPUParticleStructTest, StructSizes)
{
    // BehaviorEntry must be 32 bytes for GPU alignment
    EXPECT_EQ(sizeof(BehaviorEntry), 32u);

    // ParticleBehaviorType is a uint32_t enum
    EXPECT_EQ(sizeof(ParticleBehaviorType), 4u);
}

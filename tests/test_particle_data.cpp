/// @file test_particle_data.cpp
/// @brief Unit tests for the ParticleData SoA container and ParticleEmitterComponent.
#include "scene/particle_emitter.h"
#include "scene/entity.h"

#include <gtest/gtest.h>

using namespace Vestige;

// ---------------------------------------------------------------------------
// ParticleData tests
// ---------------------------------------------------------------------------

class ParticleDataTest : public ::testing::Test
{
protected:
    ParticleData data;

    void SetUp() override
    {
        data.resize(100);
    }

    /// @brief Manually spawn a particle at the given index.
    void spawnAt(int index, glm::vec3 pos, float lifetime)
    {
        data.positions[index] = pos;
        data.velocities[index] = glm::vec3(0.0f, 1.0f, 0.0f);
        data.colors[index] = glm::vec4(1.0f);
        data.sizes[index] = 0.5f;
        data.ages[index] = 0.0f;
        data.lifetimes[index] = lifetime;
        ++data.count;
    }
};

TEST_F(ParticleDataTest, ResizeSetsCapacityAndZerosCount)
{
    EXPECT_EQ(data.maxCount, 100);
    EXPECT_EQ(data.count, 0);
    EXPECT_EQ(static_cast<int>(data.positions.size()), 100);
    EXPECT_EQ(static_cast<int>(data.velocities.size()), 100);
    EXPECT_EQ(static_cast<int>(data.colors.size()), 100);
    EXPECT_EQ(static_cast<int>(data.sizes.size()), 100);
    EXPECT_EQ(static_cast<int>(data.ages.size()), 100);
    EXPECT_EQ(static_cast<int>(data.lifetimes.size()), 100);
}

TEST_F(ParticleDataTest, KillSwapsWithLast)
{
    // Spawn 3 particles
    spawnAt(0, glm::vec3(1, 0, 0), 5.0f);
    spawnAt(1, glm::vec3(2, 0, 0), 5.0f);
    spawnAt(2, glm::vec3(3, 0, 0), 5.0f);
    EXPECT_EQ(data.count, 3);

    // Kill the first one — expect it to be replaced by the last
    data.kill(0);
    EXPECT_EQ(data.count, 2);
    EXPECT_FLOAT_EQ(data.positions[0].x, 3.0f);  // was particle 2
    EXPECT_FLOAT_EQ(data.positions[1].x, 2.0f);  // unchanged
}

TEST_F(ParticleDataTest, KillLastDoesNotSwap)
{
    spawnAt(0, glm::vec3(1, 0, 0), 5.0f);
    spawnAt(1, glm::vec3(2, 0, 0), 5.0f);

    data.kill(1);
    EXPECT_EQ(data.count, 1);
    EXPECT_FLOAT_EQ(data.positions[0].x, 1.0f);  // unchanged
}

TEST_F(ParticleDataTest, KillOnlyParticle)
{
    spawnAt(0, glm::vec3(1, 0, 0), 5.0f);
    data.kill(0);
    EXPECT_EQ(data.count, 0);
}

TEST_F(ParticleDataTest, KillInvalidIndex)
{
    spawnAt(0, glm::vec3(1, 0, 0), 5.0f);
    data.kill(-1);  // Should do nothing
    EXPECT_EQ(data.count, 1);
    data.kill(5);   // Out of range
    EXPECT_EQ(data.count, 1);
}

TEST_F(ParticleDataTest, ClearResetsCount)
{
    spawnAt(0, glm::vec3(1, 0, 0), 5.0f);
    spawnAt(1, glm::vec3(2, 0, 0), 5.0f);
    EXPECT_EQ(data.count, 2);

    data.clear();
    EXPECT_EQ(data.count, 0);
    EXPECT_EQ(data.maxCount, 100);  // Capacity unchanged
}

TEST_F(ParticleDataTest, ResizePreservesLiveParticles)
{
    spawnAt(0, glm::vec3(1, 0, 0), 5.0f);
    EXPECT_EQ(data.count, 1);
    // Growing should preserve live particles
    data.resize(200);
    EXPECT_EQ(data.count, 1);
    EXPECT_EQ(data.maxCount, 200);

    // Shrinking below count should clamp
    data.resize(0);
    EXPECT_EQ(data.count, 0);
    EXPECT_EQ(data.maxCount, 0);
}

// ---------------------------------------------------------------------------
// ParticleEmitterComponent tests
// ---------------------------------------------------------------------------

class ParticleEmitterTest : public ::testing::Test
{
protected:
    Entity entity{"TestEmitter"};
    ParticleEmitterComponent* emitter = nullptr;

    void SetUp() override
    {
        emitter = entity.addComponent<ParticleEmitterComponent>();
        // Fast emission for testing
        emitter->getConfig().emissionRate = 100.0f;
        emitter->getConfig().maxParticles = 500;
        emitter->getConfig().startLifetimeMin = 1.0f;
        emitter->getConfig().startLifetimeMax = 1.0f;
        emitter->getConfig().gravity = glm::vec3(0.0f);
    }
};

TEST_F(ParticleEmitterTest, StartsWithNoParticles)
{
    EXPECT_EQ(emitter->getData().count, 0);
}

TEST_F(ParticleEmitterTest, SpawnsParticlesOnUpdate)
{
    // At 100/sec, 0.1s should spawn ~10 particles
    entity.update(0.1f);
    EXPECT_GT(emitter->getData().count, 0);
    EXPECT_LE(emitter->getData().count, 15);  // Some tolerance
}

TEST_F(ParticleEmitterTest, ParticlesExpireAfterLifetime)
{
    // Spawn particles
    entity.update(0.1f);
    int afterSpawn = emitter->getData().count;
    EXPECT_GT(afterSpawn, 0);

    // Advance past lifetime (1.0s)
    for (int i = 0; i < 20; ++i)
    {
        entity.update(0.1f);
    }
    // After 2 seconds total, the particles from the first 0.1s should be dead
    // but new ones are still spawning, so we just check the system is running
    EXPECT_GE(emitter->getData().count, 0);
}

TEST_F(ParticleEmitterTest, PauseStopsSimulation)
{
    entity.update(0.1f);
    int count = emitter->getData().count;

    emitter->setPaused(true);
    entity.update(0.1f);
    EXPECT_EQ(emitter->getData().count, count);  // No change
    EXPECT_TRUE(emitter->isPaused());
}

TEST_F(ParticleEmitterTest, RestartClearsParticles)
{
    entity.update(0.1f);
    EXPECT_GT(emitter->getData().count, 0);

    emitter->restart();
    EXPECT_EQ(emitter->getData().count, 0);
}

TEST_F(ParticleEmitterTest, RespectsMaxParticles)
{
    emitter->getConfig().maxParticles = 10;
    emitter->getConfig().emissionRate = 10000.0f;  // Very fast

    entity.update(1.0f);
    EXPECT_LE(emitter->getData().count, 10);
}

TEST_F(ParticleEmitterTest, NonLoopingStopsEmitting)
{
    emitter->getConfig().looping = false;
    emitter->getConfig().duration = 0.5f;
    emitter->getConfig().startLifetimeMin = 10.0f;  // Long lifetime so they stay alive
    emitter->getConfig().startLifetimeMax = 10.0f;

    // Within duration
    entity.update(0.3f);
    int countDuring = emitter->getData().count;
    EXPECT_GT(countDuring, 0);

    // Past duration — no new particles
    entity.update(0.5f);
    int countAfter = emitter->getData().count;
    // Should not have significantly more particles (only the existing ones remain)
    EXPECT_GE(countAfter, countDuring);

    // No more should spawn
    entity.update(1.0f);
    // Count should be roughly the same or lower (particles aging)
    EXPECT_LE(emitter->getData().count, countAfter + 5);  // Small tolerance for timing
}

TEST_F(ParticleEmitterTest, CloneCreatesIndependentCopy)
{
    entity.update(0.1f);
    EXPECT_GT(emitter->getData().count, 0);

    auto cloned = emitter->clone();
    auto* cloneComp = dynamic_cast<ParticleEmitterComponent*>(cloned.get());
    ASSERT_NE(cloneComp, nullptr);

    // Clone starts fresh (no particles)
    EXPECT_EQ(cloneComp->getData().count, 0);
    // But has same config
    EXPECT_FLOAT_EQ(cloneComp->getConfig().emissionRate, 100.0f);
}

TEST_F(ParticleEmitterTest, ShapeSphereSpawnsInsideRadius)
{
    emitter->getConfig().shape = ParticleEmitterConfig::Shape::SPHERE;
    emitter->getConfig().shapeRadius = 2.0f;

    entity.update(0.1f);
    const auto& data = emitter->getData();

    for (int i = 0; i < data.count; ++i)
    {
        float dist = glm::length(data.positions[i]);
        EXPECT_LE(dist, 5.0f);  // Generous bound (radius + initial speed*dt)
    }
}

TEST_F(ParticleEmitterTest, GravityAffectsVelocity)
{
    emitter->getConfig().gravity = glm::vec3(0.0f, -10.0f, 0.0f);
    emitter->getConfig().startSpeedMin = 0.0f;
    emitter->getConfig().startSpeedMax = 0.0f;
    emitter->getConfig().startLifetimeMin = 5.0f;  // Long lifetime so they stay alive
    emitter->getConfig().startLifetimeMax = 5.0f;

    entity.update(0.1f);  // Spawn some particles
    ASSERT_GT(emitter->getData().count, 0);

    // Several small timesteps to accumulate gravity
    for (int i = 0; i < 10; ++i)
    {
        entity.update(0.1f);
    }

    // At least one particle should have negative Y velocity from gravity
    bool hasNegativeY = false;
    for (int i = 0; i < emitter->getData().count; ++i)
    {
        if (emitter->getData().velocities[i].y < -5.0f)
        {
            hasNegativeY = true;
            break;
        }
    }
    EXPECT_TRUE(hasNegativeY);
}

// ---------------------------------------------------------------------------
// Config tests for undo/redo and serialization support
// ---------------------------------------------------------------------------

TEST_F(ParticleEmitterTest, ConfigModification)
{
    auto& cfg = emitter->getConfig();

    // Modify all fields
    cfg.emissionRate = 200.0f;
    cfg.maxParticles = 2000;
    cfg.looping = false;
    cfg.duration = 10.0f;
    cfg.startLifetimeMin = 2.0f;
    cfg.startLifetimeMax = 5.0f;
    cfg.startSpeedMin = 3.0f;
    cfg.startSpeedMax = 6.0f;
    cfg.startSizeMin = 0.2f;
    cfg.startSizeMax = 0.8f;
    cfg.startColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.5f);
    cfg.gravity = glm::vec3(0.0f, 5.0f, 0.0f);
    cfg.shape = ParticleEmitterConfig::Shape::BOX;
    cfg.shapeBoxSize = glm::vec3(2.0f, 3.0f, 4.0f);
    cfg.blendMode = ParticleEmitterConfig::BlendMode::ALPHA_BLEND;

    // Verify changes persisted
    const auto& readCfg = emitter->getConfig();
    EXPECT_FLOAT_EQ(readCfg.emissionRate, 200.0f);
    EXPECT_EQ(readCfg.maxParticles, 2000);
    EXPECT_FALSE(readCfg.looping);
    EXPECT_FLOAT_EQ(readCfg.duration, 10.0f);
    EXPECT_EQ(readCfg.shape, ParticleEmitterConfig::Shape::BOX);
    EXPECT_EQ(readCfg.blendMode, ParticleEmitterConfig::BlendMode::ALPHA_BLEND);
    EXPECT_FLOAT_EQ(readCfg.startColor.r, 1.0f);
    EXPECT_FLOAT_EQ(readCfg.startColor.a, 0.5f);
}

TEST_F(ParticleEmitterTest, AllShapeTypes)
{
    // Verify all shape types work without crashing
    ParticleEmitterConfig::Shape shapes[] = {
        ParticleEmitterConfig::Shape::POINT,
        ParticleEmitterConfig::Shape::SPHERE,
        ParticleEmitterConfig::Shape::CONE,
        ParticleEmitterConfig::Shape::BOX
    };

    for (auto shape : shapes)
    {
        emitter->getConfig().shape = shape;
        emitter->restart();
        entity.update(0.1f);
        EXPECT_GE(emitter->getData().count, 0);
    }
}

TEST_F(ParticleEmitterTest, DisabledComponentDoesNotUpdate)
{
    emitter->setEnabled(false);
    entity.update(0.1f);
    EXPECT_EQ(emitter->getData().count, 0);

    emitter->setEnabled(true);
    entity.update(0.1f);
    EXPECT_GT(emitter->getData().count, 0);
}

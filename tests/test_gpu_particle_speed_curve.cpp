// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_particle_speed_curve.cpp
/// @brief 3D_E-0874 — the GPU speed-over-lifetime curve scales the start
///        speed, as the CPU emitter does, instead of compounding.
///
/// particle_emitter.cpp sets a particle's speed to startSpeed × curve(age).
/// The simulate shader multiplied the CURRENT speed by curve(age) every
/// frame, so a constant 0.5 curve halved the speed each frame.
#include "renderer/gpu_particle_system.h"

#include "gl_test_fixture.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace Vestige;

namespace
{

struct SpeedTestParticle
{
    glm::vec4 position;
    glm::vec4 velocity;
    glm::vec4 color;
    float     age;
    float     lifetime;
    float     startSize;
    uint32_t  flags;
};
static_assert(sizeof(SpeedTestParticle) == 64, "must match the shader's GPUParticle");

constexpr uint32_t MAX_PARTICLES = 16;

class GpuParticleSpeedCurveTest : public ::Vestige::Test::GLTestFixture {};

} // namespace

TEST_F(GpuParticleSpeedCurveTest, ConstantCurveScalesTheStartSpeedOnce)
{
    GPUParticleSystem sys;
    ASSERT_TRUE(sys.init(VESTIGE_SHADER_DIR, MAX_PARTICLES))
        << "particle compute shaders failed to load";

    EmissionParams params;
    params.shapeType = 0;  // point
    params.startLifetimeMin = 10.0f;
    params.startLifetimeMax = 10.0f;
    params.startSpeedMin = 2.0f;
    params.startSpeedMax = 2.0f;

    BehaviorBlock behaviors;  // no forces: only the curve changes speed
    behaviors.speedKeys[0] = 0.5f;
    behaviors.speedKeyTimes[0] = 0.0f;
    behaviors.speedKeys[1] = 0.5f;
    behaviors.speedKeyTimes[1] = 1.0f;
    behaviors.speedKeyCount = 2;

    const float dt = 0.1f;
    sys.beginFrame();
    sys.emit(1, params);
    for (int frame = 0; frame < 10; ++frame)
    {
        if (frame > 0)
        {
            sys.beginFrame();
        }
        sys.simulate(dt, behaviors, dt * static_cast<float>(frame + 1));
        sys.compact();
    }
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    ASSERT_EQ(sys.readAliveCount(), 1u);

    std::vector<SpeedTestParticle> particles(MAX_PARTICLES);
    glGetNamedBufferSubData(sys.getParticleSSBO(), 0,
                            static_cast<GLsizeiptr>(particles.size() * sizeof(SpeedTestParticle)),
                            particles.data());
    int found = 0;
    for (const SpeedTestParticle& p : particles)
    {
        if ((p.flags & 1u) != 0u)
        {
            ++found;
            EXPECT_NEAR(glm::length(glm::vec3(p.velocity)), 1.0f, 1e-3f)
                << "speed should be start speed 2 x curve 0.5, after "
                << p.age << " s";
        }
    }
    EXPECT_EQ(found, 1);
}

// A falling curve: 1 at birth to 0 at death. One second into a ten-second
// life the curve reads 0.9, so the speed is 2 x 0.9.
TEST_F(GpuParticleSpeedCurveTest, FallingCurveTracksTheCurveValue)
{
    GPUParticleSystem sys;
    ASSERT_TRUE(sys.init(VESTIGE_SHADER_DIR, MAX_PARTICLES))
        << "particle compute shaders failed to load";

    EmissionParams params;
    params.shapeType = 0;
    params.startLifetimeMin = 10.0f;
    params.startLifetimeMax = 10.0f;
    params.startSpeedMin = 2.0f;
    params.startSpeedMax = 2.0f;

    BehaviorBlock behaviors;
    behaviors.speedKeys[0] = 1.0f;
    behaviors.speedKeyTimes[0] = 0.0f;
    behaviors.speedKeys[1] = 0.0f;
    behaviors.speedKeyTimes[1] = 1.0f;
    behaviors.speedKeyCount = 2;

    const float dt = 0.1f;
    for (int frame = 0; frame < 10; ++frame)
    {
        sys.beginFrame();
        if (frame == 0)
        {
            sys.emit(1, params);
        }
        sys.simulate(dt, behaviors, dt * static_cast<float>(frame + 1));
        sys.compact();
    }
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    ASSERT_EQ(sys.readAliveCount(), 1u);

    std::vector<SpeedTestParticle> particles(MAX_PARTICLES);
    glGetNamedBufferSubData(sys.getParticleSSBO(), 0,
                            static_cast<GLsizeiptr>(particles.size() * sizeof(SpeedTestParticle)),
                            particles.data());
    int found = 0;
    for (const SpeedTestParticle& p : particles)
    {
        if ((p.flags & 1u) != 0u)
        {
            ++found;
            const float curve = 1.0f - p.age / p.lifetime;
            EXPECT_NEAR(glm::length(glm::vec3(p.velocity)), 2.0f * curve, 1e-3f)
                << "after " << p.age << " s";
        }
    }
    EXPECT_EQ(found, 1);
}

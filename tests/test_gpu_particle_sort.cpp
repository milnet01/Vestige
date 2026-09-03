// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_particle_sort.cpp
/// @brief GL tests for the bitonic particle sort (3D_E-0629).
///
/// These run the real `particle_sort.comp.glsl` against a known particle
/// set and read the key buffer back, because all three defects the sort
/// carried were invisible from the CPU side: the dispatch was never issued,
/// the key buffer was narrower than the network dispatched over, and the
/// compare direction was derived from the step rather than the stage, so
/// the network did not sort.
///
/// `VESTIGE_SHADER_DIR` is wired by tests/CMakeLists.txt to
/// `${CMAKE_SOURCE_DIR}/assets/shaders` so the test runs from any cwd.

#include "renderer/gpu_particle_system.h"

#include <gtest/gtest.h>

#include "gl_test_fixture.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace Vestige;

namespace
{

/// @brief Mirrors `GPUParticle` in particle_sort.comp.glsl. Declared here
///        rather than shared, so the test pins the layout the shader reads
///        instead of inheriting whatever the engine happens to define.
struct SortTestParticle
{
    glm::vec4 position;
    glm::vec4 velocity;
    glm::vec4 color;
    float     age;
    float     lifetime;
    float     startSize;
    uint32_t  flags;
};
static_assert(sizeof(SortTestParticle) == 64, "must match the shader's GPUParticle");

constexpr uint32_t ALIVE_FLAG = 1u;

/// Not a power of two, so the network's padding lanes are exercised: the
/// key buffer is 16 wide while only 12 particles exist.
constexpr uint32_t MAX_PARTICLES = 12;
constexpr uint32_t ALIVE_COUNT   = 8;

/// nextPowerOf2(MAX_PARTICLES). The engine's helper is private, so the width
/// is spelled out here — which is the point: the test asserts the buffer the
/// engine allocates matches the width the network dispatches over.
constexpr uint32_t NETWORK_WIDTH = 16;

struct KeyEntry
{
    uint32_t key;
    uint32_t index;
};

} // namespace

class GpuParticleSortTest : public ::Vestige::Test::GLTestFixture
{
};

TEST_F(GpuParticleSortTest, SortsAliveParticlesBackToFrontAndParksTheRest)
{
    GPUParticleSystem sys;
    ASSERT_TRUE(sys.init(VESTIGE_SHADER_DIR, MAX_PARTICLES))
        << "particle compute shaders failed to load";

    const uint32_t width = NETWORK_WIDTH;

    // Particle i sits at z = -(i + 1). With an identity view matrix the
    // shader's depth is -viewPos.z, so particle i is (i + 1) units away and
    // particle ALIVE_COUNT-1 is the farthest. Indices [ALIVE_COUNT, MAX) are
    // dead and must sort past every live one.
    std::vector<SortTestParticle> particles(MAX_PARTICLES, SortTestParticle{});
    for (uint32_t i = 0; i < MAX_PARTICLES; ++i)
    {
        particles[i].position = glm::vec4(0.0f, 0.0f, -static_cast<float>(i + 1), 1.0f);
        particles[i].flags    = (i < ALIVE_COUNT) ? ALIVE_FLAG : 0u;
    }
    glNamedBufferSubData(sys.getParticleSSBO(), 0,
                         static_cast<GLsizeiptr>(particles.size() * sizeof(SortTestParticle)),
                         particles.data());

    sys.sort(glm::mat4(1.0f));
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    std::vector<KeyEntry> keys(width);
    glGetNamedBufferSubData(sys.getSortKeySSBO(), 0,
                            static_cast<GLsizeiptr>(keys.size() * sizeof(KeyEntry)),
                            keys.data());
    ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR))
        << "reading the key buffer errored — is it sized to the network width?";

    // The live particles occupy the leading slots, farthest first. The
    // indirect draw takes its instance count from aliveCount and indexes
    // sortKeys[gl_InstanceID], so anything else puts a dead particle on screen.
    for (uint32_t slot = 0; slot < ALIVE_COUNT; ++slot)
    {
        const uint32_t expected = ALIVE_COUNT - 1 - slot;
        EXPECT_EQ(keys[slot].index, expected)
            << "slot " << slot << " should hold particle " << expected;
    }

    // Every dead particle and every padding lane is parked behind them.
    for (uint32_t slot = ALIVE_COUNT; slot < width; ++slot)
    {
        EXPECT_GE(keys[slot].index, ALIVE_COUNT)
            << "slot " << slot << " holds a live particle behind the dead ones";
    }

    // A sort is a permutation. This is the assertion that catches the
    // undersized buffer and the uninitialised padding directly: a dropped or
    // duplicated index means a lane read or wrote memory it did not own.
    std::vector<uint32_t> seen;
    seen.reserve(width);
    for (const auto& entry : keys)
        seen.push_back(entry.index);
    std::sort(seen.begin(), seen.end());
    for (uint32_t i = 0; i < width; ++i)
        EXPECT_EQ(seen[i], i) << "sort output is not a permutation of [0, " << width << ")";

    sys.shutdown();
}

TEST_F(GpuParticleSortTest, KeyBufferSpansTheFullNetworkWidth)
{
    GPUParticleSystem sys;
    ASSERT_TRUE(sys.init(VESTIGE_SHADER_DIR, MAX_PARTICLES));

    GLint sizeBytes = 0;
    glGetNamedBufferParameteriv(sys.getSortKeySSBO(), GL_BUFFER_SIZE, &sizeBytes);

    // Sizing to MAX_PARTICLES leaves the merge passes addressing past the end.
    EXPECT_EQ(static_cast<uint32_t>(sizeBytes), NETWORK_WIDTH * sizeof(KeyEntry));

    sys.shutdown();
}

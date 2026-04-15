// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_compact.comp.glsl
/// @brief Stream compaction — removes dead particles and rebuilds the free list.
///
/// Two-pass approach:
/// Pass 1 (this shader): Each thread checks one particle slot. Dead particles
/// are returned to the free list via atomic counter. Alive particles are
/// counted to update aliveCount.
///
/// This uses a simple atomic scatter approach rather than prefix-sum, which is
/// sufficient for typical particle counts (< 1M). The free list is rebuilt
/// each frame so dead slots can be reused by the emission shader next frame.
#version 450 core

layout(local_size_x = 256) in;

struct GPUParticle
{
    vec4 position;
    vec4 velocity;
    vec4 color;
    float age;
    float lifetime;
    float startSize;
    uint flags;
};

layout(std430, binding = 0) buffer Particles
{
    GPUParticle particles[];
};

layout(std430, binding = 1) buffer Counters
{
    uint aliveCount;
    uint deadCount;
    uint emitCount;
    uint maxParticles;
};

layout(std430, binding = 2) buffer FreeList
{
    uint freeIndices[];
};

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= maxParticles)
        return;

    uint flags = particles[idx].flags;
    bool alive = (flags & 1u) != 0u;

    if (!alive)
    {
        // Return this slot to the free list
        uint freeIdx = atomicAdd(deadCount, 1u);
        freeIndices[freeIdx] = idx;
    }
    else
    {
        atomicAdd(aliveCount, 1u);
    }
}

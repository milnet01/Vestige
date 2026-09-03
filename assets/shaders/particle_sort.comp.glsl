// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_sort.comp.glsl
/// @brief Bitonic merge sort for back-to-front particle transparency rendering.
///
/// Sorts particles by camera-space depth using a parallel bitonic sort.
/// Only used for ALPHA_BLEND particles — ADDITIVE particles skip sorting.
///
/// Two-phase approach:
/// 1. Generate sort keys (camera depth + particle index) — this shader, pass 0
/// 2. Bitonic sort passes — this shader, pass 1+
///
/// The sort operates on (depth, index) pairs. After sorting, a reorder pass
/// copies particles to sorted positions, or the rendering shader uses the
/// sorted index buffer.
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

// Sort key buffer: each entry is (depth as uint bits, particle index)
struct SortKey
{
    uint depth;      // Float bits (reinterpreted for sorting)
    uint index;      // Original particle index
};

layout(std430, binding = 5) buffer SortKeys
{
    SortKey sortKeys[];
};

layout(std430, binding = 1) buffer Counters
{
    uint aliveCount;
    uint deadCount;
    uint emitCount;
    uint maxParticles;
};

uniform mat4 u_viewMatrix;
uniform int u_sortPass;        // 0 = generate keys, 1+ = bitonic passes
uniform int u_sortStage;       // Bitonic stage parameter
uniform int u_sortStep;        // Bitonic step parameter
uniform int u_sortCount;       // Number of elements to sort (rounded to power of 2)

// Key reserved for dead particles and for the padding lanes between
// maxParticles and u_sortCount. Maximum value == sorts last.
const uint SENTINEL_KEY = 0xFFFFFFFFu;

void main()
{
    uint idx = gl_GlobalInvocationID.x;

    if (u_sortPass == 0)
    {
        // Pass 0: Generate sort keys from alive particles
        if (idx >= uint(u_sortCount))
            return;

        // The network is a power of two wide, so the lanes past the particle
        // array are padding. They must still be seeded, or the merge passes
        // shuffle uninitialised keys into the live range (3D_E-0629).
        if (idx >= maxParticles)
        {
            sortKeys[idx].depth = SENTINEL_KEY;
            sortKeys[idx].index = idx;
            return;
        }

        GPUParticle p = particles[idx];
        if ((p.flags & 1u) == 0u)
        {
            // Dead particles get the maximum key, so they sort past every
            // live particle and never land inside the indirect draw's
            // aliveCount instances.
            sortKeys[idx].depth = SENTINEL_KEY;
            sortKeys[idx].index = idx;
            return;
        }

        // Compute view-space depth
        vec4 viewPos = u_viewMatrix * vec4(p.position.xyz, 1.0);
        float depth = -viewPos.z; // Negate for back-to-front (larger = farther)

        // Convert to sortable uint (IEEE 754 float → ordered uint)
        uint depthBits = floatBitsToUint(depth);
        // Flip sign bit for correct unsigned comparison of positive floats
        // Negative floats need all bits flipped
        if ((depthBits & 0x80000000u) != 0u)
            depthBits = ~depthBits; // Negative: flip all bits
        else
            depthBits |= 0x80000000u; // Positive: flip sign bit only

        // The network sorts ASCENDING. Back-to-front means farthest first,
        // so the key is the complement of the depth ordering: the largest
        // depth becomes the smallest key. This also keeps SENTINEL_KEY (the
        // maximum) as "sorts last", which is what the dead/padding lanes
        // above rely on.
        sortKeys[idx].depth = ~depthBits;
        sortKeys[idx].index = idx;
    }
    else
    {
        // Bitonic sort pass: compare and swap
        if (idx >= uint(u_sortCount))
            return;

        uint halfBlock = 1u << uint(u_sortStep - 1);
        uint block = halfBlock << 1u;

        // Determine partner index
        uint groupIdx = idx / block;
        uint localIdx = idx % block;

        // Direction comes from the STAGE, not the step. Deriving it from the
        // step (which halves on every inner pass) flips the compare direction
        // at the wrong granularity, and the network does not sort
        // (3D_E-0629). This is the classic `(i & k) == 0` test, k = 1 << stage.
        bool ascending = (idx & (1u << uint(u_sortStage))) == 0u;

        uint partnerOffset;
        if (localIdx < halfBlock)
            partnerOffset = localIdx + halfBlock;
        else
            partnerOffset = localIdx - halfBlock;

        uint partnerIdx = groupIdx * block + partnerOffset;

        if (partnerIdx >= uint(u_sortCount))
            return;

        // Only process if this is the lower index of the pair
        if (idx > partnerIdx)
            return;

        SortKey a = sortKeys[idx];
        SortKey b = sortKeys[partnerIdx];

        bool shouldSwap;
        // Keys ascend; the key itself is the complement of depth, so an
        // ascending sort here is a back-to-front draw order.
        if (ascending)
            shouldSwap = a.depth > b.depth;
        else
            shouldSwap = a.depth < b.depth;

        if (shouldSwap)
        {
            sortKeys[idx] = b;
            sortKeys[partnerIdx] = a;
        }
    }
}

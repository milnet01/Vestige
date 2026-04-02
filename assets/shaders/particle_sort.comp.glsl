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

void main()
{
    uint idx = gl_GlobalInvocationID.x;

    if (u_sortPass == 0)
    {
        // Pass 0: Generate sort keys from alive particles
        if (idx >= maxParticles)
            return;

        GPUParticle p = particles[idx];
        if ((p.flags & 1u) == 0u)
        {
            // Dead particles get maximum depth value (sorted to end)
            sortKeys[idx].depth = 0xFFFFFFFFu;
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

        sortKeys[idx].depth = depthBits;
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
        bool ascending = (groupIdx % 2u) == 0u;

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
        if (ascending)
            shouldSwap = a.depth > b.depth; // Back-to-front: descending depth
        else
            shouldSwap = a.depth < b.depth;

        if (shouldSwap)
        {
            sortKeys[idx] = b;
            sortKeys[partnerIdx] = a;
        }
    }
}

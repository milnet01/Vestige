/// @file particle_emit.comp.glsl
/// @brief GPU particle emission — spawns new particles into free pool slots.
///
/// Each thread spawns one particle. Uses atomic counter to allocate from free list.
/// CPU provides spawn count, shape parameters, and a per-frame random seed.
#version 450 core

layout(local_size_x = 64) in;

// GPU particle data (64 bytes per particle, cache-line aligned)
struct GPUParticle
{
    vec4 position;       // xyz = world pos, w = size
    vec4 velocity;       // xyz = velocity, w = rotation angle
    vec4 color;          // rgba
    float age;           // seconds since spawn
    float lifetime;      // total lifetime in seconds
    float startSize;     // initial size (for over-lifetime scaling)
    uint flags;          // bit 0 = alive
};

layout(std430, binding = 0) buffer Particles
{
    GPUParticle particles[];
};

layout(std430, binding = 1) buffer Counters
{
    uint aliveCount;
    uint deadCount;
    uint emitCount;      // Set by CPU: how many to spawn this frame
    uint maxParticles;
};

layout(std430, binding = 2) buffer FreeList
{
    uint freeIndices[];
};

// Emission parameters (set by CPU each frame)
uniform vec3 u_emitterPos;
uniform mat3 u_emitterRotation;
uniform int u_shapeType;           // 0=point, 1=sphere, 2=cone, 3=box, 4=ring
uniform float u_shapeRadius;
uniform float u_shapeConeAngle;    // radians
uniform vec3 u_shapeBoxSize;
uniform float u_startLifetimeMin;
uniform float u_startLifetimeMax;
uniform float u_startSpeedMin;
uniform float u_startSpeedMax;
uniform float u_startSizeMin;
uniform float u_startSizeMax;
uniform vec4 u_startColor;
uniform int u_randomSeed;

// --- PCG random number generator (public domain, O'Neill 2014) ---
uint pcgHash(uint input)
{
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Returns [0, 1)
float randomFloat(inout uint seed)
{
    seed = pcgHash(seed);
    return float(seed) / 4294967296.0;
}

// Returns [lo, hi)
float randomRange(inout uint seed, float lo, float hi)
{
    return lo + randomFloat(seed) * (hi - lo);
}

// Returns a random unit vector on the sphere
vec3 randomUnitSphere(inout uint seed)
{
    float z = randomRange(seed, -1.0, 1.0);
    float phi = randomRange(seed, 0.0, 6.283185307);
    float r = sqrt(1.0 - z * z);
    return vec3(r * cos(phi), r * sin(phi), z);
}

// Returns a random point inside a unit sphere
vec3 randomInSphere(inout uint seed)
{
    vec3 dir = randomUnitSphere(seed);
    float t = pow(randomFloat(seed), 1.0 / 3.0); // Uniform distribution in volume
    return dir * t;
}

void main()
{
    uint threadIdx = gl_GlobalInvocationID.x;
    if (threadIdx >= emitCount)
        return;

    // Allocate a free slot using atomic decrement on dead count
    uint slotIdx = atomicAdd(deadCount, 0xFFFFFFFFu); // Atomic decrement (add -1 wrapping)
    if (slotIdx == 0u || slotIdx > maxParticles)
    {
        // No free slots — undo the decrement
        atomicAdd(deadCount, 1u);
        return;
    }
    slotIdx -= 1u; // deadCount was pre-decremented, index is deadCount-1

    uint particleIdx = freeIndices[slotIdx];

    // Per-thread random seed
    uint seed = uint(u_randomSeed) ^ pcgHash(threadIdx * 1099087573u + particleIdx);

    // Generate spawn position based on shape
    vec3 offset = vec3(0.0);
    vec3 direction = vec3(0.0, 1.0, 0.0); // Default upward

    if (u_shapeType == 0) // POINT
    {
        offset = vec3(0.0);
        direction = randomUnitSphere(seed);
    }
    else if (u_shapeType == 1) // SPHERE
    {
        offset = randomInSphere(seed) * u_shapeRadius;
        direction = randomUnitSphere(seed);
    }
    else if (u_shapeType == 2) // CONE
    {
        // Random direction within cone angle
        float cosAngle = cos(u_shapeConeAngle);
        float z = randomRange(seed, cosAngle, 1.0);
        float phi = randomRange(seed, 0.0, 6.283185307);
        float sinTheta = sqrt(1.0 - z * z);
        direction = vec3(sinTheta * cos(phi), z, sinTheta * sin(phi));

        // Random offset within cone base
        float r = sqrt(randomFloat(seed)) * u_shapeRadius;
        float angle = randomRange(seed, 0.0, 6.283185307);
        offset = vec3(r * cos(angle), 0.0, r * sin(angle));
    }
    else if (u_shapeType == 3) // BOX
    {
        offset = vec3(
            randomRange(seed, -u_shapeBoxSize.x, u_shapeBoxSize.x) * 0.5,
            randomRange(seed, -u_shapeBoxSize.y, u_shapeBoxSize.y) * 0.5,
            randomRange(seed, -u_shapeBoxSize.z, u_shapeBoxSize.z) * 0.5
        );
        direction = vec3(0.0, 1.0, 0.0) + randomUnitSphere(seed) * 0.1;
        direction = normalize(direction);
    }
    else if (u_shapeType == 4) // RING
    {
        float angle = randomRange(seed, 0.0, 6.283185307);
        offset = vec3(cos(angle), 0.0, sin(angle)) * u_shapeRadius;
        direction = normalize(offset); // Outward from ring center
    }

    // Apply emitter transform
    vec3 worldOffset = u_emitterRotation * offset;
    vec3 worldDir = normalize(u_emitterRotation * direction);

    // Randomize start properties
    float lifetime = randomRange(seed, u_startLifetimeMin, u_startLifetimeMax);
    float speed = randomRange(seed, u_startSpeedMin, u_startSpeedMax);
    float size = randomRange(seed, u_startSizeMin, u_startSizeMax);

    // Write particle
    GPUParticle p;
    p.position = vec4(u_emitterPos + worldOffset, size);
    p.velocity = vec4(worldDir * speed, 0.0);
    p.color = u_startColor;
    p.age = 0.0;
    p.lifetime = lifetime;
    p.startSize = size;
    p.flags = 1u; // Alive

    particles[particleIdx] = p;

    // Increment alive count
    atomicAdd(aliveCount, 1u);
}

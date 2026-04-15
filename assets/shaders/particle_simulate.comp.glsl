// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file particle_simulate.comp.glsl
/// @brief GPU particle simulation — applies composable behaviors, physics, and aging.
///
/// Processes all alive particles. Applies force behaviors (gravity, drag, noise, etc.),
/// integrates velocity → position, evaluates over-lifetime modifiers, and marks
/// expired particles as dead.
#version 450 core

layout(local_size_x = 256) in;

struct GPUParticle
{
    vec4 position;       // xyz = world pos, w = size
    vec4 velocity;       // xyz = velocity, w = rotation angle
    vec4 color;          // rgba
    float age;
    float lifetime;
    float startSize;
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
    uint emitCount;
    uint maxParticles;
};

// Behavior data (composable force/modifier system)
// Each behavior is 32 bytes: type(4) + flags(4) + params[6](24)
struct BehaviorData
{
    uint type;
    uint behaviorFlags;
    float params[6];
};

layout(std140, binding = 4) uniform BehaviorBlock
{
    BehaviorData behaviors[16];
    int behaviorCount;
    int colorStopCount;
    int sizeKeyCount;
    int speedKeyCount;
    // Color gradient (up to 8 RGBA stops with times)
    vec4 colorStops[8];
    float colorStopTimes[8];
    // Size over lifetime (up to 8 keyframes)
    float sizeKeys[8];
    float sizeKeyTimes[8];
    // Speed over lifetime (up to 8 keyframes)
    float speedKeys[8];
    float speedKeyTimes[8];
};

uniform float u_deltaTime;
uniform float u_elapsed;       // Total elapsed time (for noise animation)

// Depth buffer collision (optional)
uniform bool u_depthCollision;
uniform sampler2D u_depthTexture;
uniform mat4 u_viewProjection;
uniform vec2 u_screenSize;
uniform float u_cameraNear;

// --- Behavior type constants ---
const uint BH_GRAVITY     = 0u;
const uint BH_DRAG        = 1u;
const uint BH_NOISE       = 2u;
const uint BH_ORBIT       = 3u;
const uint BH_ATTRACT     = 4u;
const uint BH_VORTEX      = 5u;
const uint BH_TURBULENCE  = 6u;
const uint BH_WIND        = 7u;
const uint BH_DEPTH_COLL  = 10u;
const uint BH_GROUND      = 11u;
const uint BH_SPHERE_COLL = 12u;
const uint BH_KILL_COLL   = 20u;

// --- Simplex noise (Ashima Arts, public domain) ---
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 mod289(vec4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec4 permute(vec4 x) { return mod289(((x * 34.0) + 10.0) * x); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v)
{
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod289(i);
    vec4 p = permute(permute(permute(
             i.z + vec4(0.0, i1.z, i2.z, 1.0))
           + i.y + vec4(0.0, i1.y, i2.y, 1.0))
           + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;

    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);

    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);

    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);

    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;

    vec4 m = max(0.5 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 105.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// Curl noise — divergence-free velocity field for realistic turbulence
vec3 curlNoise(vec3 p)
{
    float eps = 0.01;
    vec3 dx = vec3(eps, 0, 0);
    vec3 dy = vec3(0, eps, 0);
    vec3 dz = vec3(0, 0, eps);

    float x = snoise(p + dy) - snoise(p - dy)
            - (snoise(p + dz) - snoise(p - dz));
    float y = snoise(p + dz) - snoise(p - dz)
            - (snoise(p + dx) - snoise(p - dx));
    float z = snoise(p + dx) - snoise(p - dx)
            - (snoise(p + dy) - snoise(p - dy));

    return vec3(x, y, z) / (2.0 * eps);
}

// --- Over-lifetime interpolation ---

vec4 sampleColorGradient(float t)
{
    if (colorStopCount <= 0)
        return vec4(1.0);
    if (colorStopCount == 1)
        return colorStops[0];

    t = clamp(t, 0.0, 1.0);

    // Find segment
    for (int i = 0; i < colorStopCount - 1; i++)
    {
        if (t <= colorStopTimes[i + 1])
        {
            float segT = (t - colorStopTimes[i]) / max(colorStopTimes[i + 1] - colorStopTimes[i], 0.0001);
            return mix(colorStops[i], colorStops[i + 1], clamp(segT, 0.0, 1.0));
        }
    }
    return colorStops[colorStopCount - 1];
}

float sampleCurve(float t, float keys[8], float times[8], int keyCount)
{
    if (keyCount <= 0)
        return 1.0;
    if (keyCount == 1)
        return keys[0];

    t = clamp(t, 0.0, 1.0);

    for (int i = 0; i < keyCount - 1; i++)
    {
        if (t <= times[i + 1])
        {
            float segT = (t - times[i]) / max(times[i + 1] - times[i], 0.0001);
            return mix(keys[i], keys[i + 1], clamp(segT, 0.0, 1.0));
        }
    }
    return keys[keyCount - 1];
}

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= maxParticles)
        return;

    GPUParticle p = particles[idx];

    // Skip dead particles
    if ((p.flags & 1u) == 0u)
        return;

    // Age the particle
    p.age += u_deltaTime;
    if (p.age >= p.lifetime)
    {
        p.flags &= ~1u; // Mark dead
        particles[idx] = p;
        return;
    }

    float normalizedAge = (p.lifetime > 0.0) ? (p.age / p.lifetime) : 1.0;
    vec3 pos = p.position.xyz;
    vec3 vel = p.velocity.xyz;

    // Apply behaviors
    bool killParticle = false;

    for (int i = 0; i < behaviorCount && i < 16; i++)
    {
        uint bType = behaviors[i].type;

        if (bType == BH_GRAVITY)
        {
            // params[0..2] = acceleration xyz
            vel += vec3(behaviors[i].params[0], behaviors[i].params[1], behaviors[i].params[2]) * u_deltaTime;
        }
        else if (bType == BH_DRAG)
        {
            // params[0] = drag coefficient
            float drag = behaviors[i].params[0];
            vel *= max(0.0, 1.0 - drag * u_deltaTime);
        }
        else if (bType == BH_NOISE)
        {
            // params[0] = frequency, params[1] = amplitude
            float freq = behaviors[i].params[0];
            float amp = behaviors[i].params[1];
            vec3 noisePos = pos * freq + vec3(u_elapsed * 0.5);
            vel += curlNoise(noisePos) * amp * u_deltaTime;
        }
        else if (bType == BH_ORBIT)
        {
            // params[0..2] = center xyz, params[3] = speed, params[4] = radius
            vec3 center = vec3(behaviors[i].params[0], behaviors[i].params[1], behaviors[i].params[2]);
            float orbitSpeed = behaviors[i].params[3];
            vec3 toCenter = center - pos;
            float dist = length(toCenter);
            if (dist > 0.001)
            {
                vec3 crossResult = cross(vec3(0, 1, 0), toCenter);
                float crossLen = length(crossResult);
                if (crossLen > 0.001)
                {
                    vec3 tangent = crossResult / crossLen;
                    vel += tangent * orbitSpeed * u_deltaTime;
                }
            }
        }
        else if (bType == BH_ATTRACT)
        {
            // params[0..2] = target xyz, params[3] = strength, params[4] = range
            vec3 target = vec3(behaviors[i].params[0], behaviors[i].params[1], behaviors[i].params[2]);
            float strength = behaviors[i].params[3];
            float range = behaviors[i].params[4];
            vec3 toTarget = target - pos;
            float dist = length(toTarget);
            if (dist > 0.001 && dist < range)
            {
                float falloff = 1.0 - (dist / range);
                vel += normalize(toTarget) * strength * falloff * u_deltaTime;
            }
        }
        else if (bType == BH_VORTEX)
        {
            // params[0..2] = axis direction, params[3] = rotational speed, params[4] = pull
            vec3 axis = normalize(vec3(behaviors[i].params[0], behaviors[i].params[1], behaviors[i].params[2]));
            float rotSpeed = behaviors[i].params[3];
            float pull = behaviors[i].params[4];
            vec3 radial = pos - axis * dot(pos, axis);
            float radialDist = length(radial);
            if (radialDist > 0.001)
            {
                vec3 tangent = normalize(cross(axis, radial));
                vel += tangent * rotSpeed * u_deltaTime;
                vel -= normalize(radial) * pull * u_deltaTime;
            }
        }
        else if (bType == BH_TURBULENCE)
        {
            // params[0] = scale, params[1] = intensity
            float scale = behaviors[i].params[0];
            float intensity = behaviors[i].params[1];
            vec3 turbPos = pos * scale + vec3(u_elapsed * 0.3, u_elapsed * 0.7, u_elapsed * 0.5);
            vec3 force = vec3(
                snoise(turbPos),
                snoise(turbPos + vec3(31.4, 0, 0)),
                snoise(turbPos + vec3(0, 0, 47.2))
            );
            vel += force * intensity * u_deltaTime;
        }
        else if (bType == BH_WIND)
        {
            // params[0..2] = wind direction, params[3] = strength
            vec3 windDir = vec3(behaviors[i].params[0], behaviors[i].params[1], behaviors[i].params[2]);
            float windStrength = behaviors[i].params[3];
            vel += windDir * windStrength * u_deltaTime;
        }
        else if (bType == BH_GROUND)
        {
            // params[0] = ground Y height, params[1] = restitution
            float groundY = behaviors[i].params[0];
            float restitution = behaviors[i].params[1];
            if (pos.y < groundY)
            {
                pos.y = groundY;
                if (vel.y < 0.0)
                    vel.y = -vel.y * restitution;
            }
        }
        else if (bType == BH_SPHERE_COLL)
        {
            // params[0..2] = center xyz, params[3] = radius, params[4] = restitution
            vec3 center = vec3(behaviors[i].params[0], behaviors[i].params[1], behaviors[i].params[2]);
            float radius = behaviors[i].params[3];
            float restitution = behaviors[i].params[4];
            vec3 delta = pos - center;
            float dist = length(delta);
            if (dist < radius && dist > 0.001)
            {
                vec3 normal = delta / dist;
                pos = center + normal * radius;
                float vn = dot(vel, normal);
                if (vn < 0.0)
                    vel -= normal * vn * (1.0 + restitution);
            }
        }
        else if (bType == BH_KILL_COLL)
        {
            // Kill on any collision event (handled after collision behaviors)
            // This is processed after ground/sphere collision detection above
        }
    }

    // Depth buffer collision
    if (u_depthCollision)
    {
        vec4 clipPos = u_viewProjection * vec4(pos, 1.0);
        if (abs(clipPos.w) < 0.0001) { /* skip — particle at camera */ }
        else {
        vec3 ndc = clipPos.xyz / clipPos.w;
        vec2 screenUV = ndc.xy * 0.5 + 0.5;

        if (screenUV.x >= 0.0 && screenUV.x <= 1.0 &&
            screenUV.y >= 0.0 && screenUV.y <= 1.0)
        {
            float sceneDepth = texture(u_depthTexture, screenUV).r;
            float particleDepth = ndc.z * 0.5 + 0.5;

            // Reverse-Z: particle behind geometry when particleDepth < sceneDepth
            if (particleDepth < sceneDepth)
            {
                // Push particle to surface and bounce
                vel = reflect(vel, vec3(0, 1, 0)) * 0.3; // Simple bounce
                killParticle = true; // Check for kill-on-collision behavior
            }
        }
        } // clipPos.w guard
    }

    // Check kill-on-collision
    if (killParticle)
    {
        for (int i = 0; i < behaviorCount && i < 16; i++)
        {
            if (behaviors[i].type == BH_KILL_COLL)
            {
                p.flags &= ~1u;
                particles[idx] = p;
                return;
            }
        }
    }

    // Integrate position
    pos += vel * u_deltaTime;

    // Apply over-lifetime modifiers
    if (colorStopCount > 0)
        p.color = sampleColorGradient(normalizedAge);

    float sizeMultiplier = 1.0;
    if (sizeKeyCount > 0)
        sizeMultiplier = sampleCurve(normalizedAge, sizeKeys, sizeKeyTimes, sizeKeyCount);

    if (speedKeyCount > 0)
    {
        float speedMult = sampleCurve(normalizedAge, speedKeys, speedKeyTimes, speedKeyCount);
        float currentSpeed = length(vel);
        if (currentSpeed > 0.001)
            vel = normalize(vel) * currentSpeed * speedMult;
    }

    // Write back
    p.position = vec4(pos, p.startSize * sizeMultiplier);
    p.velocity = vec4(vel, p.velocity.w);
    particles[idx] = p;
}

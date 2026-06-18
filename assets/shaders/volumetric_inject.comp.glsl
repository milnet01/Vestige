// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_inject.comp.glsl
/// @brief Phase 10 pass 1/3 — inject the participating medium.
///
/// Writes per-froxel (scattering_rgb = sigma_s, extinction_a = sigma_t) into
/// the froxel volume. Slice 11.6 injected a uniform medium; slice 11.8 adds a
/// value-noise density field (mist/ground-fog volumes, 11.11, layer in here
/// later). When `u_noiseEnabled == 0` the pass writes the uniform medium
/// byte-for-byte (the pre-11.8 behaviour the equivalence tests pin). One
/// thread per froxel.
///
/// Froxel coordinate math + the value-noise field mirror
/// engine/renderer/volumetric_fog.cpp (CLAUDE.md Rule 7: CPU spec pins GPU
/// runtime). The noise helpers are standalone so the GPU parity test can
/// extract them and pin them against the CPU `fogDensityNoise()`.
#version 450 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(rgba16f, binding = 0) uniform writeonly image3D u_volumeImage;

uniform vec3  u_froxelRes;    // (resX, resY, resZ) as float; exact for our sizes
uniform vec3  u_scattering;   // sigma_s per channel, 1/m
uniform float u_extinction;   // sigma_t, 1/m

// Density-noise inputs (slice 11.8). u_noiseEnabled == 0 → uniform medium.
uniform vec2  u_froxelNearFar; // x = near, y = far (view-space metres)
uniform mat4  u_invProjection; // clip -> view, to place the froxel centre
uniform mat4  u_invView;       // view -> world, so the field is world-stable
uniform float u_elapsed;       // wall-clock seconds, for the wind scroll
uniform int   u_noiseEnabled;  // 0 = uniform medium (equivalence path)
uniform float u_noiseFreq;     // cycles per world metre
uniform float u_noiseStrength; // 0..1 modulation depth
uniform int   u_noiseOctaves;  // FBM octaves
uniform vec3  u_noiseWind;      // world m/s domain scroll

// View-space linear depth at the centre of depth slice `slice` (exponential
// distribution). Mirrors sliceToViewDepth() in the scatter pass and
// froxelSliceToViewDepth() in volumetric_fog.cpp.
float sliceToViewDepth(int slice, int resZ, float nearD, float farD)
{
    float t = (float(slice) + 0.5) / float(resZ);
    return nearD * pow(farD / nearD, t);
}

// --- Value-noise FBM density field (slice 11.8). Integer hash → bit-exact
// CPU↔GLSL; mirrors fogDensityNoise() in engine/renderer/volumetric_fog.cpp. ---

// 3D lattice cell → 32-bit hash. uint wraparound matches C++ uint32_t.
uint noiseHash3(int x, int y, int z)
{
    uint h = uint(x) * 0x8da6b343u + uint(y) * 0xd8163841u + uint(z) * 0xcb1ab31fu;
    h ^= h >> 15u;
    h *= 0x2c1b3c6du;
    h ^= h >> 12u;
    h *= 0x297a2d39u;
    h ^= h >> 15u;
    return h;
}

// Hash → [0, 1). Top 24 bits / 2^24 (exactly representable in float).
float hashToUnit(uint h) { return float(h >> 8u) * (1.0 / 16777216.0); }

float vlerp(float a, float b, float t) { return a + t * (b - a); }

// Trilinear value noise with a smoothstep fade. Returns [0, 1].
float valueNoise3(vec3 p)
{
    vec3  fp = floor(p);
    ivec3 i  = ivec3(fp);
    vec3  t  = p - fp;
    vec3  u  = t * t * (3.0 - 2.0 * t);

    float c000 = hashToUnit(noiseHash3(i.x,     i.y,     i.z));
    float c100 = hashToUnit(noiseHash3(i.x + 1, i.y,     i.z));
    float c010 = hashToUnit(noiseHash3(i.x,     i.y + 1, i.z));
    float c110 = hashToUnit(noiseHash3(i.x + 1, i.y + 1, i.z));
    float c001 = hashToUnit(noiseHash3(i.x,     i.y,     i.z + 1));
    float c101 = hashToUnit(noiseHash3(i.x + 1, i.y,     i.z + 1));
    float c011 = hashToUnit(noiseHash3(i.x,     i.y + 1, i.z + 1));
    float c111 = hashToUnit(noiseHash3(i.x + 1, i.y + 1, i.z + 1));

    float x00 = vlerp(c000, c100, u.x);
    float x10 = vlerp(c010, c110, u.x);
    float x01 = vlerp(c001, c101, u.x);
    float x11 = vlerp(c011, c111, u.x);
    float y0  = vlerp(x00, x10, u.y);
    float y1  = vlerp(x01, x11, u.y);
    return vlerp(y0, y1, u.z);
}

// FBM (lacunarity 2, gain 0.5), amplitude-normalised to [0, 1].
float fbm3(vec3 p, int octaves)
{
    float sum = 0.0, amp = 0.5, total = 0.0;
    for (int o = 0; o < octaves; ++o)
    {
        sum   += amp * valueNoise3(p);
        total += amp;
        p = p * 2.0 + vec3(19.19, 47.77, 74.13);
        amp *= 0.5;
    }
    return total > 0.0 ? sum / total : 0.0;
}

// Density multiplier (mean ≈ 1) at a world position. Pins CPU fogDensityNoise().
float fogDensityNoise(vec3 worldPos, float freq, float strength, int octaves,
                      vec3 wind, float t)
{
    vec3  d = worldPos * freq + wind * t;
    float n = fbm3(d, octaves);
    float m = 1.0 + strength * (2.0 * n - 1.0);
    return clamp(m, 0.0, 2.0);
}

void main()
{
    ivec3 res = ivec3(u_froxelRes);
    ivec3 c   = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, res)))
    {
        return;
    }

    vec3  scattering = u_scattering;
    float extinction = u_extinction;

    if (u_noiseEnabled != 0)
    {
        // Reconstruct the froxel-centre world position (mirrors the scatter
        // pass) so the noise field is anchored in world space, not screen space.
        vec2  uv  = (vec2(c.xy) + 0.5) / vec2(res.xy);
        vec2  ndc = uv * 2.0 - 1.0;
        vec4  vp  = u_invProjection * vec4(ndc, 1.0, 1.0);
        vec3  ray = vp.xyz / vp.w;
        float viewDepth = sliceToViewDepth(c.z, res.z,
                                           u_froxelNearFar.x, u_froxelNearFar.y);
        vec3  viewPos  = ray * (viewDepth / max(-ray.z, 1e-4));
        vec3  worldPos = (u_invView * vec4(viewPos, 1.0)).xyz;

        float m = fogDensityNoise(worldPos, u_noiseFreq, u_noiseStrength,
                                  u_noiseOctaves, u_noiseWind, u_elapsed);
        scattering *= m;
        extinction *= m;
    }

    imageStore(u_volumeImage, c, vec4(scattering, extinction));
}

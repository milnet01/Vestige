// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_fog.cpp
/// @brief Implementation of the froxel-grid coordinate math (slice 11.6).

#include "renderer/volumetric_fog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Vestige
{

namespace
{

/// True when the config can produce a well-defined exponential mapping.
bool isFinite(const FroxelGridConfig& cfg)
{
    return cfg.resZ > 0 && cfg.near > 0.0f && cfg.far > cfg.near;
}

// --- Value-noise FBM density field (slice 11.8). Integer-hash core so the
// CPU spec below and the GLSL `fogDensityNoise` in volumetric_inject.comp.glsl
// agree bit-for-bit on the hash (Jarzynski & Olano, JCGT 2020); the float
// interpolation matches within a few ULPs. Every op here is mirrored verbatim
// in the shader — keep the two in lockstep (Rule 7). NEVER sin-hashing.

/// 3D lattice cell → 32-bit hash. `uint32_t` wraparound matches GLSL `uint`.
std::uint32_t noiseHash3(int x, int y, int z)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u
                    + static_cast<std::uint32_t>(y) * 0xd8163841u
                    + static_cast<std::uint32_t>(z) * 0xcb1ab31fu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

/// Hash → [0, 1). Top 24 bits / 2^24: exactly representable in float, so the
/// conversion is bit-identical to the shader's `float(h >> 8u) / 16777216.0`.
float hashToUnit(std::uint32_t h)
{
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

float vlerp(float a, float b, float t)
{
    return a + t * (b - a);
}

/// Trilinear value noise with a smoothstep fade. Returns [0, 1].
float valueNoise3(float px, float py, float pz)
{
    const float fpx = std::floor(px);
    const float fpy = std::floor(py);
    const float fpz = std::floor(pz);
    const int ix = static_cast<int>(fpx);
    const int iy = static_cast<int>(fpy);
    const int iz = static_cast<int>(fpz);
    const float tx = px - fpx, ty = py - fpy, tz = pz - fpz;
    const float ux = tx * tx * (3.0f - 2.0f * tx);
    const float uy = ty * ty * (3.0f - 2.0f * ty);
    const float uz = tz * tz * (3.0f - 2.0f * tz);

    const float c000 = hashToUnit(noiseHash3(ix,     iy,     iz));
    const float c100 = hashToUnit(noiseHash3(ix + 1, iy,     iz));
    const float c010 = hashToUnit(noiseHash3(ix,     iy + 1, iz));
    const float c110 = hashToUnit(noiseHash3(ix + 1, iy + 1, iz));
    const float c001 = hashToUnit(noiseHash3(ix,     iy,     iz + 1));
    const float c101 = hashToUnit(noiseHash3(ix + 1, iy,     iz + 1));
    const float c011 = hashToUnit(noiseHash3(ix,     iy + 1, iz + 1));
    const float c111 = hashToUnit(noiseHash3(ix + 1, iy + 1, iz + 1));

    const float x00 = vlerp(c000, c100, ux);
    const float x10 = vlerp(c010, c110, ux);
    const float x01 = vlerp(c001, c101, ux);
    const float x11 = vlerp(c011, c111, ux);
    const float y0  = vlerp(x00, x10, uy);
    const float y1  = vlerp(x01, x11, uy);
    return vlerp(y0, y1, uz);
}

/// FBM (lacunarity 2, gain 0.5), amplitude-normalised to [0, 1].
float fbm3(float px, float py, float pz, int octaves)
{
    float sum = 0.0f, amp = 0.5f, total = 0.0f;
    for (int o = 0; o < octaves; ++o)
    {
        sum   += amp * valueNoise3(px, py, pz);
        total += amp;
        px = px * 2.0f + 19.19f;
        py = py * 2.0f + 47.77f;
        pz = pz * 2.0f + 74.13f;
        amp *= 0.5f;
    }
    return total > 0.0f ? sum / total : 0.0f;
}

} // namespace

int froxelCount(const FroxelGridConfig& cfg)
{
    if (cfg.resX <= 0 || cfg.resY <= 0 || cfg.resZ <= 0)
    {
        return 0;
    }
    return cfg.resX * cfg.resY * cfg.resZ;
}

float froxelSliceToViewDepth(const FroxelGridConfig& cfg, int slice)
{
    if (!isFinite(cfg))
    {
        return cfg.near;
    }

    // Clamp the slice into the valid range so the returned depth never
    // escapes [near, far] (callers may pass out-of-range indices).
    const int clamped = std::clamp(slice, 0, cfg.resZ - 1);
    const float t = (static_cast<float>(clamped) + 0.5f)
                  / static_cast<float>(cfg.resZ);

    // viewDepth = near * (far / near) ^ t
    return cfg.near * std::pow(cfg.far / cfg.near, t);
}

float viewDepthToFroxelSlice(const FroxelGridConfig& cfg, float viewDepth)
{
    if (!isFinite(cfg))
    {
        return 0.0f;
    }

    const float z = std::clamp(viewDepth, cfg.near, cfg.far);

    // slice = N * log(z / near) / log(far / near) - 0.5
    const float t = std::log(z / cfg.near) / std::log(cfg.far / cfg.near);
    return static_cast<float>(cfg.resZ) * t - 0.5f;
}

float froxelSliceBoundaryViewDepth(const FroxelGridConfig& cfg, int boundary)
{
    if (!isFinite(cfg))
    {
        return cfg.near;
    }

    // Integer boundary (no +0.5 centre offset): boundary(b) = near * (far/near)^(b/N).
    const int clamped = std::clamp(boundary, 0, cfg.resZ);
    const float t = static_cast<float>(clamped) / static_cast<float>(cfg.resZ);
    return cfg.near * std::pow(cfg.far / cfg.near, t);
}

float henyeyGreensteinPhase(float cosTheta, float g)
{
    const float gg = g * g;
    const float denom = 1.0f + gg - 2.0f * g * cosTheta;
    constexpr float PI = 3.14159265358979323846f;
    return (1.0f - gg) / (4.0f * PI * std::pow(std::max(denom, 1e-4f), 1.5f));
}

glm::vec2 froxelToScreenUV(const FroxelGridConfig& cfg, int i, int j)
{
    const float u = cfg.resX > 0
                  ? (static_cast<float>(i) + 0.5f) / static_cast<float>(cfg.resX)
                  : 0.0f;
    const float v = cfg.resY > 0
                  ? (static_cast<float>(j) + 0.5f) / static_cast<float>(cfg.resY)
                  : 0.0f;
    return {u, v};
}

float fogDensityNoise(const glm::vec3& worldPos, const FogNoiseParams& params, float time)
{
    const int octaves = std::clamp(params.octaves, 1, 5);
    const float dx = worldPos.x * params.frequency + params.windVelocity.x * time;
    const float dy = worldPos.y * params.frequency + params.windVelocity.y * time;
    const float dz = worldPos.z * params.frequency + params.windVelocity.z * time;
    const float n  = fbm3(dx, dy, dz, octaves);               // [0, 1]
    const float m  = 1.0f + params.strength * (2.0f * n - 1.0f);
    return std::clamp(m, 0.0f, 2.0f);
}

} // namespace Vestige

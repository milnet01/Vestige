// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file volumetric_fog.cpp
/// @brief Implementation of the froxel-grid coordinate math (slice 11.6).

#include "renderer/volumetric_fog.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

namespace
{

/// True when the config can produce a well-defined exponential mapping.
bool isFinite(const FroxelGridConfig& cfg)
{
    return cfg.resZ > 0 && cfg.near > 0.0f && cfg.far > cfg.near;
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

} // namespace Vestige

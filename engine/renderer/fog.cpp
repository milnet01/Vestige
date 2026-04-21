// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file fog.cpp
/// @brief Canonical fog primitives — closed-form distance / height /
///        sun-inscatter math. See fog.h for formula derivations and
///        citations.

#include "renderer/fog.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

// -----------------------------------------------------------------------
// Distance fog
// -----------------------------------------------------------------------

float computeFogFactor(FogMode mode,
                       const FogParams& params,
                       float distance)
{
    if (mode == FogMode::None) return 1.0f;

    // Surfaces at or behind the camera plane are never fogged. Guards
    // against NaN from exp(-0) edge cases and keeps linearised-depth
    // reconstruction stable at the near plane.
    if (distance <= 0.0f) return 1.0f;

    switch (mode)
    {
        case FogMode::Linear:
        {
            const float span = params.end - params.start;
            if (span <= 0.0f) return 1.0f;              // Degenerate → no fog
            if (distance <= params.start) return 1.0f;
            if (distance >= params.end)   return 0.0f;
            return (params.end - distance) / span;
        }
        case FogMode::Exponential:
        {
            const float density = std::max(0.0f, params.density);
            return std::exp(-density * distance);
        }
        case FogMode::ExponentialSquared:
        {
            const float density = std::max(0.0f, params.density);
            const float x = density * distance;
            return std::exp(-(x * x));
        }
        case FogMode::None:
            return 1.0f;
    }
    return 1.0f;
}

const char* fogModeLabel(FogMode mode)
{
    switch (mode)
    {
        case FogMode::None:               return "None";
        case FogMode::Linear:             return "Linear";
        case FogMode::Exponential:        return "Exponential";
        case FogMode::ExponentialSquared: return "ExponentialSquared";
    }
    return "None";
}

// -----------------------------------------------------------------------
// Exponential height fog (Quílez analytic integral)
// -----------------------------------------------------------------------

float computeHeightFogTransmittance(const HeightFogParams& params,
                                    float cameraY,
                                    float rayDirY,
                                    float rayLength)
{
    // Guard degenerate inputs early — callers can reach here with
    // zero-length rays (picking the camera origin) or disabled layers.
    if (rayLength <= 0.0f) return 1.0f;
    if (params.groundDensity <= 0.0f) return 1.0f;

    const float a = std::max(0.0f, params.groundDensity);
    const float b = std::max(0.0f, params.heightFalloff);
    const float h = cameraY - params.fogHeight;

    // Accumulated optical depth along the view ray. This is the
    // integral of d(y(s)) = a * exp(-b * (y(s) - fogHeight)) from
    // s=0 to s=rayLength where y(s) = cameraY + rayDirY*s.
    float fogAmount;

    // Horizontal ray or flat density (b -> 0) collapses to
    //   fogAmount = a * exp(-b*h) * rayLength
    // (standard Beer-Lambert along a single-density path).
    constexpr float kHorizontalRayEpsilon = 1e-5f;
    if (b <= 0.0f || std::fabs(rayDirY) < kHorizontalRayEpsilon)
    {
        fogAmount = a * std::exp(-b * h) * rayLength;
    }
    else
    {
        // Quílez analytic integral:
        //   fogAmount = (a/b) * exp(-b*h) * (1 - exp(-b*rayDirY*rayLength)) / rayDirY
        // Split to avoid cancellation when b*rayDirY*rayLength is
        // near zero (use -expm1 for stability) — expm1(x) = exp(x)-1.
        const float tau  = b * rayDirY * rayLength;
        const float base = (a / b) * std::exp(-b * h);
        // (1 - exp(-tau)) / rayDirY = -expm1(-tau) / rayDirY
        fogAmount = base * (-std::expm1(-tau)) / rayDirY;
    }

    // Guard non-physical negative values (can occur under extreme
    // downward rays into very deep negative altitudes where the
    // exp(-b*h) term explodes — treat as fully opaque).
    if (!(fogAmount >= 0.0f))
    {
        return 1.0f - std::clamp(params.maxOpacity, 0.0f, 1.0f);
    }

    const float transmittance = std::exp(-fogAmount);

    // Clamp by maxOpacity. Transmittance floor is (1 - maxOpacity):
    // maxOpacity 1.0 → floor 0 (fully fogged); 0.9 → floor 0.1.
    const float maxOpacity = std::clamp(params.maxOpacity, 0.0f, 1.0f);
    const float floorT     = 1.0f - maxOpacity;
    return std::max(transmittance, floorT);
}

// -----------------------------------------------------------------------
// Sun inscatter lobe
// -----------------------------------------------------------------------

float computeSunInscatterLobe(const SunInscatterParams& params,
                              const glm::vec3& viewDir,
                              const glm::vec3& sunDirection,
                              float viewDistance)
{
    if (viewDistance < params.startDistance) return 0.0f;

    // `sunDirection` points from scene toward the sun (engine
    // convention for directional lights). Align-with-sun lobe weight
    // is the cosine between viewDir and *-sunDirection* (toward sun
    // means looking into where the light comes from).
    const glm::vec3 towardSun = -sunDirection;
    const float cosAngle = glm::dot(glm::normalize(viewDir),
                                    glm::normalize(towardSun));
    if (cosAngle <= 0.0f) return 0.0f;

    const float exponent = std::max(0.0f, params.exponent);
    return std::pow(cosAngle, exponent);
}

// -----------------------------------------------------------------------
// Composite
// -----------------------------------------------------------------------

glm::vec3 applyFog(const glm::vec3& surfaceColour,
                   const glm::vec3& fogColour,
                   float factor)
{
    // factor = 1.0 → surface visible; 0.0 → fully fogged.
    // Matches GLSL `mix(fog, surface, factor)` byte-for-byte.
    const float t = std::clamp(factor, 0.0f, 1.0f);
    return fogColour * (1.0f - t) + surfaceColour * t;
}

glm::vec3 composeFog(const glm::vec3& surfaceColour,
                     const FogCompositeInputs& inputs,
                     const glm::vec3& worldPos)
{
    // Early-out when every layer is off — keeps the "fog disabled"
    // path a literal identity, which the shader also does via
    // `fogActive` gating.
    if (inputs.fogMode == FogMode::None
        && !inputs.heightFogEnabled
        && !inputs.sunInscatterEnabled)
    {
        return surfaceColour;
    }

    const glm::vec3 viewVec = worldPos - inputs.cameraWorldPos;
    const float viewDistance = glm::length(viewVec);
    const glm::vec3 viewDir = (viewDistance > 0.0f)
                               ? viewVec / viewDistance
                               : glm::vec3(0.0f, 0.0f, -1.0f);

    const float surfaceVisibility =
        computeFogFactor(inputs.fogMode, inputs.fogParams, viewDistance);

    float heightT = 1.0f;
    if (inputs.heightFogEnabled && viewDistance > 0.0f)
    {
        heightT = computeHeightFogTransmittance(inputs.heightFogParams,
                                                inputs.cameraWorldPos.y,
                                                viewDir.y,
                                                viewDistance);
    }

    glm::vec3 distanceFogColour = inputs.fogParams.colour;
    if (inputs.sunInscatterEnabled)
    {
        const float lobe = computeSunInscatterLobe(inputs.sunInscatterParams,
                                                   viewDir,
                                                   inputs.sunDirection,
                                                   viewDistance);
        // GLSL: mix(fogColour, sunColour, lobe)
        //       = fogColour*(1-lobe) + sunColour*lobe
        const float t = std::clamp(lobe, 0.0f, 1.0f);
        distanceFogColour = inputs.fogParams.colour * (1.0f - t)
                          + inputs.sunInscatterParams.colour * t;
    }

    const glm::vec3 fogged = applyFog(surfaceColour,
                                      distanceFogColour,
                                      surfaceVisibility);
    return applyFog(fogged,
                    inputs.heightFogParams.colour,
                    heightT);
}

} // namespace Vestige

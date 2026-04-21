// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_doppler.cpp
/// @brief Pure-function Doppler-shift pitch computation.
#include "audio/audio_doppler.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

float computeDopplerPitchRatio(const DopplerParams& params,
                               const glm::vec3& sourcePosition,
                               const glm::vec3& sourceVelocity,
                               const glm::vec3& listenerPosition,
                               const glm::vec3& listenerVelocity)
{
    // Disabled: dopplerFactor == 0 collapses the formula to f'/f = 1.
    if (params.dopplerFactor <= 0.0f)
    {
        return 1.0f;
    }

    // Speed of sound must be positive for the formula to make
    // physical sense; fall back to unity if the caller passed junk.
    if (!(params.speedOfSound > 0.0f))
    {
        return 1.0f;
    }

    // Unit vector from source to listener. If the two endpoints are
    // co-located there is no well-defined projection axis, so the
    // effect is undefined — return unity.
    const glm::vec3 toListener = listenerPosition - sourcePosition;
    const float axisLength     = glm::length(toListener);
    if (axisLength < 1e-6f)
    {
        return 1.0f;
    }
    const glm::vec3 axis = toListener / axisLength;

    // Velocity projections along the source→listener axis. Sign
    // convention (matches OpenAL 1.1 §3.5.2):
    //   vLs > 0 → listener moving AWAY from source (with the axis)
    //   vSs > 0 → source moving TOWARD listener    (with the axis)
    float vLs = glm::dot(listenerVelocity, axis);
    float vSs = glm::dot(sourceVelocity,   axis);

    // Clamp to (−SS/DF, SS/DF) so the denominator stays strictly
    // positive and the numerator strictly non-zero when either
    // endpoint approaches the speed of sound. A hard clamp to the
    // exact limit would land on denominator == 0 and the formula
    // would explode; shaving a small epsilon off the boundary
    // saturates at a large but finite ratio instead (matching how
    // the physical Mach-1 wall is modelled — large, not infinite).
    // OpenAL Soft does the same thing internally.
    const float limit = params.speedOfSound / params.dopplerFactor;
    const float safeLimit = limit * (1.0f - 1e-4f);
    vLs = std::max(-safeLimit, std::min(safeLimit, vLs));
    vSs = std::max(-safeLimit, std::min(safeLimit, vSs));

    const float numerator   = params.speedOfSound - params.dopplerFactor * vLs;
    const float denominator = params.speedOfSound - params.dopplerFactor * vSs;

    // After clamping the denominator cannot be zero, but guard the
    // pathological speedOfSound == 0 case we already handled above.
    if (denominator <= 0.0f)
    {
        return 1.0f;
    }

    const float ratio = numerator / denominator;

    // Ratio must remain strictly positive; a non-positive numerator
    // after clamping would imply invalid input — fall back to unity.
    if (!(ratio > 0.0f) || !std::isfinite(ratio))
    {
        return 1.0f;
    }

    return ratio;
}

} // namespace Vestige

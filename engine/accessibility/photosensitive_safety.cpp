// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file photosensitive_safety.cpp
/// @brief Pure-function clamp helpers for photosensitivity safe mode.

#include "accessibility/photosensitive_safety.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

namespace
{

// WCAG 2.2 SC 2.3.1 contract: NaN / ±inf / negative inputs must NOT reach the
// renderer regardless of safe-mode state. A single sanitiser drops non-finite
// and negative values to 0 before either the enabled cap or the disabled
// pass-through runs, so upstream arithmetic bugs cannot produce an
// uncontrolled flash / shake / strobe.
inline float sanitiseNonNegative(float x)
{
    return std::isfinite(x) ? std::max(x, 0.0f) : 0.0f;
}

} // namespace

float clampFlashAlpha(float alpha, bool enabled, const PhotosensitiveLimits& limits)
{
    const float safe = sanitiseNonNegative(alpha);
    if (!enabled)
    {
        return safe;
    }
    return std::min(safe, limits.maxFlashAlpha);
}

float clampShakeAmplitude(float amplitude, bool enabled, const PhotosensitiveLimits& limits)
{
    const float safe = sanitiseNonNegative(amplitude);
    if (!enabled)
    {
        return safe;
    }
    return safe * limits.shakeAmplitudeScale;
}

float clampStrobeHz(float hz, bool enabled, const PhotosensitiveLimits& limits)
{
    const float safe = sanitiseNonNegative(hz);
    if (!enabled)
    {
        return safe;
    }
    return std::min(safe, limits.maxStrobeHz);
}

float limitBloomIntensity(float intensity, bool enabled, const PhotosensitiveLimits& limits)
{
    const float safe = sanitiseNonNegative(intensity);
    if (!enabled)
    {
        return safe;
    }
    return safe * limits.bloomIntensityScale;
}

} // namespace Vestige

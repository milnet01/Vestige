// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file photosensitive_safety.cpp
/// @brief Pure-function clamp helpers for photosensitivity safe mode.

#include "accessibility/photosensitive_safety.h"

#include <algorithm>

namespace Vestige
{

float clampFlashAlpha(float alpha, bool enabled, const PhotosensitiveLimits& limits)
{
    if (!enabled)
    {
        return alpha;
    }
    return std::min(alpha, limits.maxFlashAlpha);
}

float clampShakeAmplitude(float amplitude, bool enabled, const PhotosensitiveLimits& limits)
{
    if (!enabled)
    {
        return amplitude;
    }
    return amplitude * limits.shakeAmplitudeScale;
}

float clampStrobeHz(float hz, bool enabled, const PhotosensitiveLimits& limits)
{
    if (!enabled)
    {
        return hz;
    }
    return std::min(hz, limits.maxStrobeHz);
}

float limitBloomIntensity(float intensity, bool enabled, const PhotosensitiveLimits& limits)
{
    if (!enabled)
    {
        return intensity;
    }
    return intensity * limits.bloomIntensityScale;
}

} // namespace Vestige

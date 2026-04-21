// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_reverb.cpp
/// @brief Reverb presets + zone weight / param blending.
#include "audio/audio_reverb.h"

#include <algorithm>

namespace Vestige
{

ReverbParams reverbPresetParams(ReverbPreset preset)
{
    // Values adapted from Creative Labs efx-presets.h. Kept to the
    // non-EAX subset so they round-trip through the standard EFX
    // reverb model.
    switch (preset)
    {
        case ReverbPreset::Generic:
            return {1.49f, 1.00f, 1.00f, 0.32f, 0.89f, 0.007f, 0.011f};

        case ReverbPreset::SmallRoom:
            // Short tail, strong early reflections, room-toned HF.
            return {0.40f, 1.00f, 1.00f, 0.50f, 0.83f, 0.003f, 0.004f};

        case ReverbPreset::LargeHall:
            // Long decay, dense modes, slightly brighter than cave.
            return {4.32f, 1.00f, 1.00f, 0.63f, 0.59f, 0.020f, 0.030f};

        case ReverbPreset::Cave:
            // Very long decay, high diffusion, more HF roll-off than hall.
            return {5.60f, 1.00f, 1.00f, 0.50f, 0.45f, 0.030f, 0.040f};

        case ReverbPreset::Outdoor:
            // Near-dry — minimal early reflections, moderate decay.
            return {1.50f, 1.00f, 0.50f, 0.20f, 0.95f, 0.005f, 0.012f};

        case ReverbPreset::Underwater:
            // Strong HF damping — voices read as muffled / distant.
            return {1.49f, 1.00f, 1.00f, 0.36f, 0.10f, 0.007f, 0.011f};
    }
    return {1.49f, 1.00f, 1.00f, 0.32f, 0.89f, 0.007f, 0.011f};
}

const char* reverbPresetLabel(ReverbPreset preset)
{
    switch (preset)
    {
        case ReverbPreset::Generic:    return "Generic";
        case ReverbPreset::SmallRoom:  return "SmallRoom";
        case ReverbPreset::LargeHall:  return "LargeHall";
        case ReverbPreset::Cave:       return "Cave";
        case ReverbPreset::Outdoor:    return "Outdoor";
        case ReverbPreset::Underwater: return "Underwater";
    }
    return "Unknown";
}

float computeReverbZoneWeight(float coreRadius,
                               float falloffBand,
                               float distance)
{
    const float r = std::max(0.0f, coreRadius);
    const float b = std::max(0.0f, falloffBand);
    const float d = std::max(0.0f, distance);

    if (d <= r)
    {
        return 1.0f;
    }
    if (b <= 0.0f || d >= r + b)
    {
        return 0.0f;
    }
    return 1.0f - (d - r) / b;
}

ReverbParams blendReverbParams(const ReverbParams& a,
                                const ReverbParams& b,
                                float t)
{
    const float c = std::max(0.0f, std::min(1.0f, t));
    const float u = 1.0f - c;
    ReverbParams out;
    out.decayTime        = a.decayTime        * u + b.decayTime        * c;
    out.density          = a.density          * u + b.density          * c;
    out.diffusion        = a.diffusion        * u + b.diffusion        * c;
    out.gain             = a.gain             * u + b.gain             * c;
    out.gainHf           = a.gainHf           * u + b.gainHf           * c;
    out.reflectionsDelay = a.reflectionsDelay * u + b.reflectionsDelay * c;
    out.lateReverbDelay  = a.lateReverbDelay  * u + b.lateReverbDelay  * c;
    return out;
}

} // namespace Vestige

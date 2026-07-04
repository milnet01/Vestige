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

ReverbPreset reverbPresetFromLabel(std::string_view label)
{
    if (label == "SmallRoom")  return ReverbPreset::SmallRoom;
    if (label == "LargeHall")  return ReverbPreset::LargeHall;
    if (label == "Cave")       return ReverbPreset::Cave;
    if (label == "Outdoor")    return ReverbPreset::Outdoor;
    if (label == "Underwater") return ReverbPreset::Underwater;
    return ReverbPreset::Generic;  // covers "Generic", "", and any stale name.
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

ReverbSelection selectReverbZone(const std::vector<ReverbZoneEval>& zones)
{
    ReverbSelection sel;  // dry default: winner = -1, targetWetGain = 0.

    // Single pass tracking the highest and second-highest weighted zones. A
    // strict `>` keeps the lower index on ties (deterministic selection).
    int   winner = -1, neighbour = -1;
    float wWin = 0.0f, wNbr = 0.0f;
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        const float w = computeReverbZoneWeight(zones[i].coreRadius,
                                                zones[i].falloffBand,
                                                zones[i].distance);
        if (w <= 0.0f)
        {
            continue;
        }
        if (w > wWin)
        {
            neighbour = winner;               wNbr = wWin;
            winner    = static_cast<int>(i);  wWin = w;
        }
        else if (w > wNbr)
        {
            neighbour = static_cast<int>(i); wNbr = w;
        }
    }

    if (winner < 0)
    {
        return sel;  // Listener outside every zone → dry.
    }

    const ReverbZoneEval& zWin = zones[static_cast<std::size_t>(winner)];
    const ReverbParams& pa = zWin.params;
    ReverbParams pb = pa;
    float gb = zWin.wetGain;
    float t  = 0.0f;
    if (neighbour >= 0)
    {
        const ReverbZoneEval& zNbr = zones[static_cast<std::size_t>(neighbour)];
        t  = wNbr / (wWin + wNbr);   // ∈ (0, 0.5] — winner always weighs more.
        pb = zNbr.params;
        gb = zNbr.wetGain;
    }

    sel.winner        = winner;
    sel.neighbour     = neighbour;
    sel.blendT        = t;
    sel.winnerWeight  = wWin;
    sel.blendedParams = blendReverbParams(pa, pb, t);

    // Blend the wet gain between the two rooms, then scale by the winner's
    // weight so leaving every zone fades the slot to silence (step 5 of §5.2).
    const float blendedWet = zWin.wetGain * (1.0f - t) + gb * t;
    sel.targetWetGain = blendedWet * wWin;
    return sel;
}

float slewReverbWetGain(float current, float target, float slewAmount)
{
    const float a = std::max(0.0f, std::min(1.0f, slewAmount));
    return current + (target - current) * a;
}

} // namespace Vestige

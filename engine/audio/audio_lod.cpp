// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_lod.cpp
/// @brief AX5 — pure audio LOD tier decision with hysteresis.
#include "audio/audio_lod.h"

#include "audio/audio_mixer.h"  // SoundPriority

#include <algorithm>

namespace Vestige
{

AudioLodTier audioLodTier(float distance, float maxDistance,
                          float occlusionFraction, SoundPriority priority,
                          AudioLodTier previousTier, const AudioLodConfig& cfg)
{
    if (!cfg.enabled || maxDistance <= 0.0f)
    {
        return AudioLodTier::Full;
    }

    const float distanceRatio = distance / maxDistance;
    const float occ           = std::clamp(occlusionFraction, 0.0f, 1.0f);
    // Effective audibility-loss ratio: distance OR occlusion can demote.
    const float ratio = std::max(distanceRatio, occ);

    const float h    = cfg.hysteresis;
    const int   prev = static_cast<int>(previousTier);

    // Walk the three ascending boundaries (enter CheapSpatial / Drop2D /
    // Mute). Each boundary is biased by ±h against the previous tier so a
    // source within the dead-band keeps its previous tier rather than
    // flapping. Boundaries stay ordered because h (0.05) is smaller than
    // half the smallest default gap (0.15 / 2), so the biased thresholds
    // never cross.
    const float bounds[3] = {
        cfg.cheapDistanceFactor, cfg.drop2DFactor, cfg.muteFactor,
    };
    int tier = 0;  // Full
    for (int k = 0; k < 3; ++k)
    {
        // Lower the bar to *stay* in tier (k+1) if already there-or-cheaper;
        // raise it to *enter* tier (k+1) otherwise.
        const float thresh = bounds[k] + ((prev >= k + 1) ? -h : +h);
        if (ratio >= thresh)
        {
            tier = k + 1;
        }
        else
        {
            break;  // boundaries are monotonic — no cheaper tier can apply
        }
    }

    // Critical audio (dialogue, boss stingers, objective cues) never drops
    // below CheapSpatial — it must keep its 3D position for the player.
    if (priority == SoundPriority::Critical)
    {
        tier = std::min(tier, static_cast<int>(AudioLodTier::CheapSpatial));
    }

    return static_cast<AudioLodTier>(tier);
}

const char* audioLodTierLabel(AudioLodTier tier)
{
    switch (tier)
    {
        case AudioLodTier::Full:         return "Full";
        case AudioLodTier::CheapSpatial: return "CheapSpatial";
        case AudioLodTier::Drop2D:       return "Drop2D";
        case AudioLodTier::Mute:         return "Mute";
    }
    return "Unknown";
}

} // namespace Vestige

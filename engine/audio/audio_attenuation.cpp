// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_attenuation.cpp
/// @brief Canonical distance-attenuation curves + OpenAL mapping.

#include "audio/audio_attenuation.h"

#include <AL/al.h>

#include <algorithm>
#include <cmath>

namespace Vestige
{

float computeAttenuation(AttenuationModel model,
                         const AttenuationParams& params,
                         float distance)
{
    // Clamp negative distance to zero — sources slightly behind the
    // listener plane shouldn't go negative due to float noise.
    const float d = std::max(0.0f, distance);

    // Below (or at) the reference distance, every clamped model
    // holds full gain. This matches AL's `_CLAMPED` variants and
    // avoids a sub-1.0 blip at d == refDist for the linear form.
    const float refDist = std::max(0.0f, params.referenceDistance);
    const float maxDist = std::max(refDist, params.maxDistance);
    const float rolloff = std::max(0.0f, params.rolloffFactor);

    if (model == AttenuationModel::None) return 1.0f;
    if (d <= refDist) return 1.0f;

    // Past maxDistance, every clamped model holds the gain fixed at
    // its value at maxDistance. Evaluate the curve at `dClamped`.
    const float dClamped = std::min(d, maxDist);

    switch (model)
    {
        case AttenuationModel::Linear:
        {
            // AL_LINEAR_DISTANCE_CLAMPED:
            //   gain = 1 - rolloff * (dClamped - refDist) / (maxDist - refDist)
            // OpenAL spec §3.4.3. Range: [0, 1].
            const float span = maxDist - refDist;
            if (span <= 0.0f) return 1.0f;
            const float t = rolloff * (dClamped - refDist) / span;
            return std::clamp(1.0f - t, 0.0f, 1.0f);
        }
        case AttenuationModel::InverseDistance:
        {
            // AL_INVERSE_DISTANCE_CLAMPED:
            //   gain = refDist / (refDist + rolloff * (dClamped - refDist))
            // OpenAL spec §3.4.1.
            const float denom = refDist + rolloff * (dClamped - refDist);
            if (denom <= 0.0f) return 1.0f;
            return refDist / denom;
        }
        case AttenuationModel::Exponential:
        {
            // AL_EXPONENT_DISTANCE_CLAMPED:
            //   gain = (dClamped / refDist) ^ (-rolloff)
            // OpenAL spec §3.4.2.
            if (refDist <= 0.0f) return 1.0f;
            return std::pow(dClamped / refDist, -rolloff);
        }
        case AttenuationModel::None:
            return 1.0f;
    }
    return 1.0f;
}

const char* attenuationModelLabel(AttenuationModel model)
{
    switch (model)
    {
        case AttenuationModel::None:            return "None";
        case AttenuationModel::Linear:          return "Linear";
        case AttenuationModel::InverseDistance: return "InverseDistance";
        case AttenuationModel::Exponential:     return "Exponential";
    }
    return "None";
}

int alDistanceModelFor(AttenuationModel model)
{
    switch (model)
    {
        case AttenuationModel::None:            return AL_NONE;
        case AttenuationModel::Linear:          return AL_LINEAR_DISTANCE_CLAMPED;
        case AttenuationModel::InverseDistance: return AL_INVERSE_DISTANCE_CLAMPED;
        case AttenuationModel::Exponential:     return AL_EXPONENT_DISTANCE_CLAMPED;
    }
    return AL_NONE;
}

} // namespace Vestige

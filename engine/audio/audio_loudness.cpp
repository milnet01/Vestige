// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_loudness.cpp
/// @brief Phase 10 AX9 — loudness measurement (libebur128) + pure makeup gain.
#include "audio/audio_loudness.h"

#include <ebur128.h>

#include <cmath>

namespace Vestige
{

float integratedLoudnessLufs(const int16_t* interleaved,
                             std::size_t    frames,
                             int            channels,
                             int            rate)
{
    if (interleaved == nullptr || frames == 0 || channels <= 0 || rate <= 0)
    {
        return kLoudnessSilenceGateLufs;  // nothing to measure → unity makeup
    }

    ebur128_state* st = ebur128_init(static_cast<unsigned int>(channels),
                                     static_cast<unsigned long>(rate),
                                     EBUR128_MODE_I);
    if (st == nullptr)
    {
        return kLoudnessSilenceGateLufs;
    }

    // AudioClip stores int16 interleaved; libebur128 ingests `short` natively.
    // int16_t aliases `short` on every platform Vestige targets (x86-64
    // LP64/LLP64), so the reinterpret is exact — no per-sample float
    // conversion or whole-clip copy is needed.
    static_assert(sizeof(int16_t) == sizeof(short),
                  "libebur128 short ingest assumes int16_t aliases short");

    double lufs = static_cast<double>(kLoudnessSilenceGateLufs);
    if (ebur128_add_frames_short(
            st, reinterpret_cast<const short*>(interleaved), frames)
            == EBUR128_SUCCESS)
    {
        double measured = 0.0;
        if (ebur128_loudness_global(st, &measured) == EBUR128_SUCCESS &&
            std::isfinite(measured))
        {
            // libebur128 returns -HUGE_VAL (non-finite) for silence; the
            // isfinite guard keeps the gate value in that case.
            lufs = measured;
        }
    }

    ebur128_destroy(&st);
    return static_cast<float>(lufs);
}

float loudnessMakeupGain(float measuredLufs, float targetLufs, float maxBoostDb)
{
    // Silence gate: never amplify a clip at/below the −70 LUFS absolute
    // floor (digital silence + its dither noise), nor a non-finite reading.
    if (!std::isfinite(measuredLufs) || measuredLufs <= kLoudnessSilenceGateLufs)
    {
        return 1.0f;
    }

    float gainDb = targetLufs - measuredLufs;  // +ve = boost, −ve = attenuate
    if (gainDb > maxBoostDb)
    {
        gainDb = maxBoostDb;  // clamp the boost direction only
    }
    return std::pow(10.0f, gainDb / 20.0f);
}

} // namespace Vestige

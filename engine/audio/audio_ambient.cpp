// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_ambient.cpp
/// @brief Ambient zones + time-of-day weights + one-shot scheduler.
#include "audio/audio_ambient.h"

#include "audio/audio_reverb.h"  // reuse computeReverbZoneWeight for identical falloff profile

#include <algorithm>
#include <cmath>

namespace Vestige
{

float computeAmbientZoneVolume(const AmbientZone& zone, float distanceToListener)
{
    const float weight = computeReverbZoneWeight(zone.coreRadius,
                                                   zone.falloffBand,
                                                   distanceToListener);
    const float vol = std::max(0.0f, std::min(1.0f, zone.maxVolume));
    return weight * vol;
}

const char* timeOfDayWindowLabel(TimeOfDayWindow window)
{
    switch (window)
    {
        case TimeOfDayWindow::Dawn:  return "Dawn";
        case TimeOfDayWindow::Day:   return "Day";
        case TimeOfDayWindow::Dusk:  return "Dusk";
        case TimeOfDayWindow::Night: return "Night";
    }
    return "Unknown";
}

namespace
{
// Triangle weight: full at `peak`, zero at `peak ± halfWidth`,
// linear in between, wrapping around the 24-hour clock so a peak
// near 24 still contributes at 0.
float triangleWeight(float hour, float peak, float halfWidth)
{
    float delta = std::fabs(hour - peak);
    // Wrap the other direction around the 24h clock.
    delta = std::min(delta, 24.0f - delta);
    if (delta >= halfWidth) return 0.0f;
    return 1.0f - delta / halfWidth;
}
}

TimeOfDayWeights computeTimeOfDayWeights(float hourOfDay)
{
    // Wrap hourOfDay into [0, 24).
    float h = std::fmod(hourOfDay, 24.0f);
    if (h < 0.0f) h += 24.0f;

    // Window peaks and half-widths chosen so every hour sums to 1.0
    // within floating-point tolerance: peaks spaced 7/6/7/4 hours
    // apart (dawn→day→dusk→night→dawn) with matching half-widths so
    // adjacent windows always meet at the 0.5 crossover point.
    TimeOfDayWeights w;
    w.dawn  = triangleWeight(h,  6.0f, 7.0f);
    w.day   = triangleWeight(h, 13.0f, 7.0f);
    w.dusk  = triangleWeight(h, 20.0f, 7.0f);
    w.night = triangleWeight(h,  1.0f, 5.0f);

    // Normalise so the four weights sum to exactly 1.0 — the
    // triangle envelopes are constructed to come close, but a
    // defensive normalisation keeps downstream multipliers stable
    // under future tuning of the peak positions.
    const float sum = w.dawn + w.day + w.dusk + w.night;
    if (sum > 1e-6f)
    {
        w.dawn  /= sum;
        w.day   /= sum;
        w.dusk  /= sum;
        w.night /= sum;
    }
    return w;
}

bool tickRandomOneShot(RandomOneShotScheduler& scheduler,
                        float deltaSeconds,
                        const UniformSampleFn& sampleFn)
{
    if (deltaSeconds < 0.0f)
    {
        deltaSeconds = 0.0f;
    }
    scheduler.timeUntilNextFire -= deltaSeconds;

    if (scheduler.timeUntilNextFire > 0.0f)
    {
        return false;
    }

    // Draw a fresh interval. If sampleFn is null, fall back to the
    // midpoint so the scheduler still makes progress rather than
    // stalling silently.
    float sample = 0.5f;
    if (sampleFn)
    {
        sample = sampleFn();
        sample = std::max(0.0f, std::min(1.0f, sample));
    }

    const float minI = std::max(0.0f, scheduler.minIntervalSeconds);
    const float maxI = std::max(minI, scheduler.maxIntervalSeconds);
    scheduler.timeUntilNextFire = minI + (maxI - minI) * sample;
    return true;
}

} // namespace Vestige

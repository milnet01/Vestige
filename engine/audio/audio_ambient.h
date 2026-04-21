// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_ambient.h
/// @brief Phase 10 spatial audio — environmental ambient zones,
///        time-of-day weighting, and a random one-shot scheduler.
///
/// Three independent primitives that the engine-side AmbientSystem
/// composes into a full ambient-audio pipeline:
///
///   1. **Ambient zones** — positional loops (wind, water, hum)
///      gated by a sphere-with-linear-falloff in world space,
///      layered by ascending priority so multiple zones can stack
///      (cave + wind + river) without stepping on each other.
///
///   2. **Time-of-day weights** — 24-hour clock mapped to four
///      continuous windows (dawn / day / dusk / night) with smooth
///      triangle-shaped transitions, so crickets fade into birdsong
///      rather than hard-switching at midnight.
///
///   3. **Random one-shot scheduler** — a stateless-ish cooldown
///      machine that fires on a random interval drawn from
///      [minInterval, maxInterval]. Tests drive it with a
///      deterministic uniform-sample callback; the engine plugs in
///      `std::uniform_real_distribution` at the call site.
///
/// All three are pure-function / data-only. Weather-driven
/// modulation is expected to land as a thin multiplier at the
/// AmbientSystem layer once the Phase 15 weather controller
/// publishes its rain / wind intensity outputs — no coupling in
/// this module.
#pragma once

#include <functional>
#include <string>

namespace Vestige
{

/// @brief Positional ambient loop, weighted by a sphere-falloff
///        centered on the zone's world position.
///
/// `priority` orders overlapping zones — higher-priority zones are
/// evaluated first by the system, so a cave ambience can override
/// the outdoor wind that still fires in its falloff band. Two zones
/// with equal priority mix at their individual weights.
struct AmbientZone
{
    std::string clipPath;              ///< Loopable audio asset.
    float coreRadius    = 10.0f;       ///< Full-volume radius (m).
    float falloffBand   =  5.0f;       ///< Linear falloff thickness (m).
    float maxVolume     =  1.0f;       ///< Volume at the core (0..1).
    int   priority      =  0;          ///< Higher wins among overlaps.
};

/// @brief Weight this zone contributes at a given listener distance.
///        Equivalent to `maxVolume · sphereFalloff(coreRadius,
///        falloffBand, distance)` — delegated here so the ambient
///        system and reverb system share the same falloff profile.
float computeAmbientZoneVolume(const AmbientZone& zone, float distanceToListener);

// ----- Time-of-day weighting ------------------------------------

/// @brief The four discrete time windows ambient clips can key off.
///        The actual output is a continuous weight per window so
///        crossfades don't hard-cut.
enum class TimeOfDayWindow
{
    Dawn,      ///< ~04:00 – ~08:00, peak ~06:00.
    Day,       ///< ~08:00 – ~18:00, peak ~13:00.
    Dusk,      ///< ~18:00 – ~22:00, peak ~20:00.
    Night,     ///< ~22:00 – ~04:00, peak ~01:00.
};

/// @brief Continuous weighting across the four windows so that the
///        sum stays 1.0 and adjacent windows crossfade smoothly.
///        Useful as a multiplier for per-window ambient clips.
struct TimeOfDayWeights
{
    float dawn  = 0.0f;
    float day   = 0.0f;
    float dusk  = 0.0f;
    float night = 0.0f;
};

/// @brief Stable label for a `TimeOfDayWindow`.
const char* timeOfDayWindowLabel(TimeOfDayWindow window);

/// @brief Returns the four-window weight vector for a 24-hour clock
///        time. The mapping is a triangle function keyed to the
///        nominal peaks (06, 13, 20, 01); adjacent windows blend
///        linearly between peaks so the weights always sum to 1.0
///        within floating-point tolerance.
///
/// @param hourOfDay Time in hours (0.0 to 24.0). Values outside the
///        range are wrapped modulo 24.
TimeOfDayWeights computeTimeOfDayWeights(float hourOfDay);

// ----- Random one-shot scheduler --------------------------------

/// @brief Cooldown-based scheduler for environmental one-shots
///        (creaking metal, distant impacts, wildlife calls).
///
/// Advances a timer each `tick`, and when the cooldown expires
/// draws a fresh interval from [`minIntervalSeconds`,
/// `maxIntervalSeconds`] using the caller-provided uniform sample
/// in [0, 1]. Tests inject a deterministic sample sequence; the
/// engine plugs in `std::uniform_real_distribution<float>(0, 1)`
/// at the call site.
struct RandomOneShotScheduler
{
    float minIntervalSeconds = 15.0f;
    float maxIntervalSeconds = 45.0f;
    float timeUntilNextFire  =  0.0f;  ///< Live state — mutated by `tick`.
};

/// @brief Uniform-sample callable signature — returns a float in
///        [0, 1]. The scheduler calls this once per fire to pick
///        the next cooldown length.
using UniformSampleFn = std::function<float()>;

/// @brief Advances the scheduler by `deltaSeconds`. If the cooldown
///        reaches zero, draws a fresh interval from `sampleFn`
///        (mapping its [0, 1] output into
///        [`minIntervalSeconds`, `maxIntervalSeconds`]) and
///        returns `true`. Otherwise returns `false`.
///
/// @returns `true` at most once per call. A very large
///          `deltaSeconds` will not collapse multiple fires into
///          one call — it simply re-arms at the end. This keeps
///          the audio budget bounded under framerate stalls.
bool tickRandomOneShot(RandomOneShotScheduler& scheduler,
                        float deltaSeconds,
                        const UniformSampleFn& sampleFn);

} // namespace Vestige

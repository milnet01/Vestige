// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_air_absorption.h
/// @brief Phase 10 audio quick-wins (AX6) — atmospheric (air) absorption
///        as a per-source high-frequency gain.
///
/// Distant outdoor sounds lose their high frequencies before they lose
/// their loudness — a far-off horn is *dull*, not just quiet. This
/// module turns the listener↔source distance plus the local weather
/// (temperature + humidity) into a single high-frequency gain
/// multiplier in [0, 1] that the compose stage feeds into the EFX
/// low-pass filter (`AL_LOWPASS_GAINHF`).
///
/// The curve is the closed-form **ISO 9613-1** atmospheric absorption
/// coefficient `α(f, T, h, p)` evaluated at a single representative
/// high-frequency anchor (≈4 kHz — the band the ear most reads as
/// "air"), converted from dB/m to a linear gain over the travel
/// distance. The ISO formula is itself a cheap closed form (a handful
/// of `pow`/`exp` calls), so — unlike the design doc's fallback plan —
/// no placeholder constant is needed: the exact standard curve is
/// evaluated directly. The constants below (8.686, 1.84e-11, 0.01275,
/// 2239.1, 0.1068, 3352.0, …) are the **published ISO 9613-1 values**,
/// not fitted magic numbers.
///
/// TODO: revisit via Formula Workbench — a fitted cheap polynomial
/// approximation of this α(T, h) surface at 4 kHz (target ≤0.5 dB abs
/// error) would trade the per-source `pow`/`exp` calls for a couple of
/// multiplies. Only worth it if profiling ever shows the per-source
/// cost matters; at the 60 FPS / ≤64-source budget it does not today.
///
/// Pressure is held at the ISO sea-level reference (101.325 kPa) and
/// deliberately excluded — its effect on α is small over playable
/// altitudes and would add a third fit axis for negligible perceptual
/// gain (this is why `WeatherState::airDensity` exists but AX6 leaves
/// it unused).
#pragma once

namespace Vestige
{

/// @brief Per-frame weather snapshot driving the air-absorption curve.
///
/// Filled once per frame by the audio system from
/// `EnvironmentForces::getTemperature/getHumidity` (weather is global
/// today, so this is a snapshot, not a per-source query).
struct AirAbsorptionParams
{
    float humidity01   = 0.5f;   ///< Relative humidity [0, 1] (EnvironmentForces).
    float temperatureC = 20.0f;  ///< Air temperature in °C (EnvironmentForces).
    bool  enabled      = true;   ///< Master toggle (AudioSettings::airAbsorptionEnabled).
};

/// @brief Linear high-frequency gain multiplier in [0, 1] for a source
///        at @a distanceMeters from the listener.
///
/// 1.0 = no HF loss (near / disabled); → 0 as distance grows.
/// Monotonically non-increasing in distance. Returns exactly 1.0 when
/// `p.enabled` is false or the distance is ≤ 0.
float airAbsorptionHfGain(float distanceMeters, const AirAbsorptionParams& p);

} // namespace Vestige

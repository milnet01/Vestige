// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_curves.h
/// @brief Runtime closed-form of the Formula Workbench `audio` category (AX4 S5,
///        3D_E-0022) — the velocity→loudness/pitch and aggregate event-rate
///        curves the procedural-audio bank (`material_sound_bank.*`) applies per
///        strike.
///
/// These are the **fast runtime form** of the three FW definitions authored and
/// validated in `engine/formula/audio_templates.cpp`. The Workbench is the
/// source of truth (project Rule 6): it owns the fitted coefficients and the
/// reference-dataset validation. Evaluating the FW expression AST per strike
/// would mean walking a tree + pulling the whole formula library into the audio
/// hot path, so the closed forms are transcribed here as plain inline functions
/// and **pinned to the FW definitions by `tests/test_audio_curves_parity.cpp`**
/// (a grid of approach speeds; both forms must agree). If a coefficient changes
/// in `audio_templates.cpp`, the parity test fails until this header is updated
/// in lockstep — so the two cannot silently diverge.
///
/// Energy domain (cold-eyes H1/H2): the input is **raw approach speed in m/s** —
/// the same unit the FW curves declare. The speed→[0,1] normalisation lives
/// inside the fitted curve, so there is no loose `kRefSpeed` constant here.
#pragma once

#include <algorithm>
#include <cmath>

namespace Vestige::Procedural
{

/// @brief Impact approach speed (m/s) → linear loudness gain in [0, 1].
///        Saturating: ~0 at a graze, →1 for a hard strike. Drives the
///        synthesised strike amplitude (`*Strike::energyGain`).
///        FW: `impact_loudness_gain`, coeff `decayPerMps = 0.4`.
inline float impactLoudnessGain(float approachSpeed)
{
    constexpr float kDecayPerMps = 0.4f;
    const float g = 1.0f - std::exp(-approachSpeed * kDecayPerMps);
    return std::clamp(g, 0.0f, 1.0f);  // FW `saturate`
}

/// @brief Impact approach speed (m/s) → pitch multiplier (~0.85 … 1.25).
///        Harder strikes ring brighter. Multiplies every modal frequency
///        (`ModalStrike::pitchScale`).
///        FW: `impact_pitch_scale`, coeffs base 0.85 / range 0.40 / decay 0.30.
inline float impactPitchScale(float approachSpeed)
{
    constexpr float kBasePitch        = 0.85f;
    constexpr float kPitchRange       = 0.40f;
    constexpr float kPitchDecayPerMps = 0.30f;
    return kBasePitch
         + kPitchRange * (1.0f - std::exp(-approachSpeed * kPitchDecayPerMps));
}

/// @brief Aggregate contact speed (m/s) → PhISEM grain rate (Hz). Faster
///        contact spawns more micro-impact grains. Clamped at `maxRateHz`.
///        FW: `aggregate_event_rate`, coeffs base 400 / slope 300 / max 2200.
inline float aggregateEventRate(float approachSpeed)
{
    constexpr float kBaseRateHz    = 400.0f;
    constexpr float kSlopeHzPerMps = 300.0f;
    constexpr float kMaxRateHz     = 2200.0f;
    return std::min(kMaxRateHz, kBaseRateHz + kSlopeHzPerMps * approachSpeed);
}

/// @brief Declared default input of the FW `aggregate_event_rate` curve
///        (`audio_templates.cpp`, `inputs` default = 1.5 m/s). Used by the bank
///        as the per-material normalisation anchor: a bank entry's authored
///        `eventRateHz` is its grain rate at this nominal speed, and
///        `aggregateEventRate(speed) / aggregateEventRate(this)` scales it with
///        actual contact speed. Tied to FW metadata (not a loose constant).
inline constexpr float kAggregateRefSpeedMps = 1.5f;

} // namespace Vestige::Procedural

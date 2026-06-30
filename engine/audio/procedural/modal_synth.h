// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file modal_synth.h
/// @brief Modal synthesis for struck solids (AX4 S4): stone / wood / metal /
///        glass. A struck solid rings at a few resonant modes; each mode is a
///        damped sinusoid realised as a two-pole resonator recurrence so the
///        inner loop is multiply-adds only (the two transcendentals are
///        evaluated once per mode per strike, never per sample).
///
/// Pure functions — they fill a 16-bit mono PCM buffer at kSynthSampleRate and
/// touch no engine state, so they are unit-tested as plain numeric functions.
#pragma once

#include <cstdint>
#include <vector>

namespace Vestige::Procedural
{

/// @brief Synth sample rate (Hz). Matches the engine audio rate
///        (audio_music_stream.h:50). Every duration→sample-count derives from it.
inline constexpr int kSynthSampleRate = 48000;

/// @brief Hard ceiling on modes per strike (design §5a/§10) — bounds worst-case
///        per-strike cost so one strike fits inside a frame's synth budget.
inline constexpr int kMaxModes = 6;

/// @brief Hard ceiling on strike duration (s). Metal (naturally ~0.5 s) clamps
///        to this; audibly minor, keeps the worst strike inside the budget.
inline constexpr float kMaxDurationSec = 0.35f;

/// @brief One resonant mode of a struck solid: a damped sinusoid.
struct Mode
{
    float freqHz = 0.0f;  ///< Modal frequency (Hz).
    float decay  = 0.0f;  ///< Decay rate (1/s); larger = faster die-off.
    float gain   = 0.0f;  ///< Relative contribution before normalisation.
};

/// @brief Resolved per-strike modal parameters (the velocity→energy/pitch
///        curves are applied by the caller, S5).
struct ModalStrike
{
    std::vector<Mode> modes;     ///< Only the first kMaxModes are used.
    float durSec     = 0.18f;    ///< Clamped to [0, kMaxDurationSec].
    float pitchScale = 1.0f;     ///< Multiplies every modal frequency.
    float energyGain = 1.0f;     ///< Peak level in [0, 1]; louder strike = larger.
};

/// @brief Converts a float synthesis buffer to 16-bit mono PCM, peak-normalised
///        so the loudest sample maps to @a energyGain of full scale. Softer
///        strikes (smaller energyGain) stay quieter, preserving relative
///        loudness. Shared by the modal and PhISEM synths.
/// @return The number of samples written (== buf.size()).
std::size_t normalizeToInt16(const std::vector<float>& buf, float energyGain,
                             std::vector<std::int16_t>& out);

/// @brief Synthesises a struck-solid impulse response into 16-bit mono PCM at
///        kSynthSampleRate. Clears and fills @a out.
/// @return The number of samples written (0 if no modes / zero duration).
std::size_t synthesizeModal(const ModalStrike& strike, std::vector<std::int16_t>& out);

} // namespace Vestige::Procedural

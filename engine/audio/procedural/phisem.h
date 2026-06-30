// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file phisem.h
/// @brief PhISEM synthesis for aggregate surfaces (AX4 S4): sand / gravel /
///        grass / water / cloth. Cook's PhISEM models an aggregate as many
///        micro-impacts of small particles — a stochastic (Poisson) sequence of
///        grains over the strike, each a short resonator ping (reusing the
///        modal one-mode kernel). Grain energy leaks (decays) over the event.
///
/// Pure functions; the RNG is injected (a uniform in (0,1]) so tests are
/// deterministic and the engine gives each emitter its own generator.
#pragma once

#include "audio/procedural/modal_synth.h"  // kSynthSampleRate, kMaxDurationSec, normalizeToInt16

#include <cstdint>
#include <functional>
#include <vector>

namespace Vestige::Procedural
{

/// @brief Resolved per-strike PhISEM parameters.
struct PhisemStrike
{
    float centreHz    = 1500.0f;  ///< Grain resonator centre frequency (Hz).
    float qual        = 2.0f;     ///< Resonator quality — higher = longer ring.
    float eventRateHz = 1000.0f;  ///< Mean grain onsets per second (Poisson rate).
    float durSec      = 0.12f;    ///< Clamped to [0, kMaxDurationSec].
    float energyDecay = 18.0f;    ///< Per-second decay of grain energy over the event.
    float energyGain  = 1.0f;     ///< Peak level in [0, 1].
};

/// @brief Generates grain onset sample-indices via a Poisson process at
///        @a eventRateHz over @a durSec (exponential inter-arrival times).
/// @param sample Injected uniform in (0,1]; values are clamped away from 0.
/// @return Onset sample indices in ascending order; size ≈ eventRateHz·durSec.
std::vector<int> phisemGrainOnsets(float eventRateHz, float durSec, int sampleRate,
                                   const std::function<float()>& sample);

/// @brief Synthesises an aggregate-surface burst into 16-bit mono PCM at
///        kSynthSampleRate. Clears and fills @a out.
/// @return The number of grains synthesised.
std::size_t synthesizePhisem(const PhisemStrike& strike, std::vector<std::int16_t>& out,
                             const std::function<float()>& sample);

} // namespace Vestige::Procedural

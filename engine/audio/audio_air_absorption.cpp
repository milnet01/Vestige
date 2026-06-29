// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_air_absorption.cpp
/// @brief AX6 — ISO 9613-1 atmospheric absorption, evaluated at a 4 kHz
///        high-frequency anchor and converted to a linear HF gain.
#include "audio/audio_air_absorption.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

float airAbsorptionHfGain(float distanceMeters, const AirAbsorptionParams& p)
{
    if (!p.enabled || distanceMeters <= 0.0f)
    {
        return 1.0f;
    }

    // Representative high-frequency anchor. 4 kHz is where atmospheric
    // absorption first becomes audible over playable distances and is
    // the band the ear reads as "air"; lower bands lose far less.
    constexpr float kFreqHz = 4000.0f;

    // ISO 9613-1 reference constants.
    constexpr float kT0  = 293.15f;  // reference temperature (20 °C) in K
    constexpr float kT01 = 273.16f;  // triple-point temperature in K

    const float T = p.temperatureC + 273.15f;                 // ambient temp, K
    const float relHumidityPct =
        std::clamp(p.humidity01, 0.0f, 1.0f) * 100.0f;        // % RH

    // Molar concentration of water vapour, h (%). Pressure ratio pa/pr
    // is held at 1 (sea-level reference), so it drops out of every term.
    const float psatRatio =
        std::pow(10.0f, -6.8346f * std::pow(kT01 / T, 1.261f) + 4.6151f);
    const float h = relHumidityPct * psatRatio;

    // Relaxation frequencies of oxygen and nitrogen (Hz).
    const float frO = 24.0f + 4.04e4f * h * (0.02f + h) / (0.391f + h);
    const float frN = std::pow(T / kT0, -0.5f)
        * (9.0f + 280.0f * h * std::exp(-4.170f
            * (std::pow(T / kT0, -1.0f / 3.0f) - 1.0f)));

    // Absorption coefficient α at kFreqHz, in dB/m (ISO 9613-1, eq. 3-5).
    const float f2 = kFreqHz * kFreqHz;
    const float alpha = 8.686f * f2 * (
        1.84e-11f * std::pow(T / kT0, 0.5f)
        + std::pow(T / kT0, -2.5f) * (
            0.01275f * std::exp(-2239.1f / T) * (frO / (frO * frO + f2))
            + 0.1068f * std::exp(-3352.0f / T) * (frN / (frN * frN + f2))));

    // dB attenuation over the travel distance → linear gain.
    const float attenuationDb = alpha * distanceMeters;
    const float gain = std::pow(10.0f, -attenuationDb / 20.0f);
    return std::clamp(gain, 0.0f, 1.0f);
}

} // namespace Vestige

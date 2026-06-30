// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file phisem.cpp
/// @brief PhISEM synthesis implementation (AX4 S4).
#include "audio/procedural/phisem.h"

#include <algorithm>
#include <cmath>

namespace Vestige::Procedural
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
}

std::vector<int> phisemGrainOnsets(float eventRateHz, float durSec, int sampleRate,
                                   const std::function<float()>& sample)
{
    std::vector<int> onsets;
    if (eventRateHz <= 0.0f || durSec <= 0.0f || sampleRate <= 0)
    {
        return onsets;
    }

    // Poisson process: inter-arrival times are exponential, dt = -ln(U)/rate.
    double t = 0.0;
    const double dur = static_cast<double>(durSec);
    const double rate = static_cast<double>(eventRateHz);
    while (true)
    {
        float u = sample ? sample() : 0.5f;
        u = std::clamp(u, 1e-6f, 1.0f);  // avoid log(0).
        t += -std::log(static_cast<double>(u)) / rate;
        if (t >= dur)
        {
            break;
        }
        onsets.push_back(static_cast<int>(t * sampleRate));
    }
    return onsets;
}

std::size_t synthesizePhisem(const PhisemStrike& strike, std::vector<std::int16_t>& out,
                             const std::function<float()>& sample)
{
    out.clear();

    const float durSec = std::clamp(strike.durSec, 0.0f, kMaxDurationSec);
    const std::size_t n =
        static_cast<std::size_t>(durSec * static_cast<float>(kSynthSampleRate) + 0.5f);
    if (n == 0)
    {
        return 0;
    }

    const std::vector<int> onsets =
        phisemGrainOnsets(strike.eventRateHz, durSec, kSynthSampleRate, sample);

    const float sr = static_cast<float>(kSynthSampleRate);

    // Each grain injects an energy-scaled impulse into one shared resonator at
    // centreHz; energy leaks over the event so later grains are softer. The
    // grains overlap naturally as the resonator rings between onsets.
    std::vector<float> drive(n, 0.0f);
    for (int onset : onsets)
    {
        if (onset < 0 || static_cast<std::size_t>(onset) >= n)
        {
            continue;
        }
        const float tSec = static_cast<float>(onset) / sr;
        const float energy = std::exp(-std::max(0.0f, strike.energyDecay) * tSec);
        drive[static_cast<std::size_t>(onset)] += energy;
    }

    std::vector<float> buf(n, 0.0f);
    const float freq = std::clamp(strike.centreHz, 1.0f, 0.5f * sr - 1.0f);
    // Resonator decay from quality: bandwidth = centre / Q → decay = π·centre/Q.
    const float decay = kPi * freq / std::max(0.1f, strike.qual);
    const float r = std::exp(-decay / sr);
    const float c = 2.0f * r * std::cos(2.0f * kPi * freq / sr);
    const float r2 = r * r;

    float y1 = 0.0f;
    float y2 = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        const float y = c * y1 - r2 * y2 + drive[i];
        buf[i] = y;
        y2 = y1;
        y1 = y;
    }

    normalizeToInt16(buf, strike.energyGain, out);
    return onsets.size();
}

} // namespace Vestige::Procedural

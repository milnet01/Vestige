// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file modal_synth.cpp
/// @brief Modal synthesis implementation (AX4 S4).
#include "audio/procedural/modal_synth.h"

#include <algorithm>
#include <cmath>

namespace Vestige::Procedural
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
}

std::size_t normalizeToInt16(const std::vector<float>& buf, float energyGain,
                             std::vector<std::int16_t>& out)
{
    out.clear();
    if (buf.empty())
    {
        return 0;
    }

    float peak = 0.0f;
    for (float v : buf)
    {
        peak = std::max(peak, std::abs(v));
    }

    const float energy = std::clamp(energyGain, 0.0f, 1.0f);
    const float scale = (peak > 0.0f) ? (energy * 32767.0f / peak) : 0.0f;

    out.resize(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i)
    {
        const float s = std::clamp(buf[i] * scale, -32768.0f, 32767.0f);
        out[i] = static_cast<std::int16_t>(std::lround(s));
    }
    return out.size();
}

std::size_t synthesizeModal(const ModalStrike& strike, std::vector<std::int16_t>& out)
{
    out.clear();

    const float durSec = std::clamp(strike.durSec, 0.0f, kMaxDurationSec);
    const std::size_t n =
        static_cast<std::size_t>(durSec * static_cast<float>(kSynthSampleRate) + 0.5f);
    if (n == 0 || strike.modes.empty())
    {
        return 0;
    }

    const int modeCount = std::min<int>(static_cast<int>(strike.modes.size()), kMaxModes);
    const float sr = static_cast<float>(kSynthSampleRate);
    const float nyquist = 0.5f * sr;

    std::vector<float> buf(n, 0.0f);

    for (int m = 0; m < modeCount; ++m)
    {
        const Mode& mode = strike.modes[static_cast<std::size_t>(m)];
        const float freq = mode.freqHz * strike.pitchScale;
        if (freq <= 0.0f || freq >= nyquist)
        {
            continue;  // skip DC / above Nyquist — would alias.
        }

        // Two-pole resonator: poles at radius r = e^(-d/SR), angle 2π f/SR.
        const float r = std::exp(-std::max(0.0f, mode.decay) / sr);
        const float c = 2.0f * r * std::cos(2.0f * kPi * freq / sr);
        const float r2 = r * r;

        // Impulse-driven: y[n] = c·y[n-1] − r²·y[n-2] + x[n], x[0]=1 else 0.
        // The +x[0] term is essential — a zero-state recurrence with no input
        // stays silent forever. The impulse drives each section into its modal
        // response ∝ g·r^n·sin(2π f n/SR).
        float y1 = 0.0f;
        float y2 = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
        {
            const float x = (i == 0) ? 1.0f : 0.0f;
            const float y = c * y1 - r2 * y2 + x;
            buf[i] += mode.gain * y;
            y2 = y1;
            y1 = y;
        }
    }

    return normalizeToInt16(buf, strike.energyGain, out);
}

} // namespace Vestige::Procedural

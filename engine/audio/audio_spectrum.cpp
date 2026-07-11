// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_spectrum.cpp
/// @brief Shared radix-2 FFT + Hann-windowed magnitude spectrum (Phase 10 AX12).
#include "audio/audio_spectrum.h"

#include <cmath>

namespace Vestige
{

// π as a portable constant. <cmath>'s M_PI is not standard C++ and is absent on
// MSVC unless _USE_MATH_DEFINES is set before the (possibly transitive) <cmath>
// include; a local constexpr avoids that fragility (C++17, no std::numbers).
static constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Radix-2 Cooley-Tukey FFT (in-place, decimation-in-time).
// Moved verbatim from AudioAnalyzer::computeFFT so the two call sites share one
// implementation; behaviour is byte-for-byte identical (INV-5).
// ---------------------------------------------------------------------------
void computeFFT(std::vector<float>& real, std::vector<float>& imag)
{
    size_t n = real.size();

    // Bit-reversal permutation
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        while (j & bit)
        {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;

        if (i < j)
        {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    // Butterfly stages
    for (size_t len = 2; len <= n; len <<= 1)
    {
        float angle = -2.0f * static_cast<float>(kPi) / static_cast<float>(len);
        float wReal = std::cos(angle);
        float wImag = std::sin(angle);

        for (size_t i = 0; i < n; i += len)
        {
            float curReal = 1.0f;
            float curImag = 0.0f;

            for (size_t j = 0; j < len / 2; ++j)
            {
                size_t u = i + j;
                size_t v = i + j + len / 2;

                // Butterfly operation
                float tReal = curReal * real[v] - curImag * imag[v];
                float tImag = curReal * imag[v] + curImag * real[v];

                real[v] = real[u] - tReal;
                imag[v] = imag[u] - tImag;
                real[u] += tReal;
                imag[u] += tImag;

                // Advance twiddle factor
                float nextReal = curReal * wReal - curImag * wImag;
                float nextImag = curReal * wImag + curImag * wReal;
                curReal = nextReal;
                curImag = nextImag;
            }
        }
    }
}

void computeMagnitudeSpectrum(const float* in, std::size_t n,
                              std::vector<float>& outMag)
{
    // Require a non-zero power-of-two length (radix-2). Anything else is a
    // caller error; clear the output and bail rather than read out of range.
    if (in == nullptr || n == 0 || (n & (n - 1)) != 0)
    {
        outMag.clear();
        return;
    }

    std::vector<float> real(n);
    std::vector<float> imag(n, 0.0f);

    // Hann window (divisor n-1, matching the lip-sync analyzer exactly) to
    // reduce spectral leakage. Applied here once — callers must NOT window again.
    for (std::size_t i = 0; i < n; ++i)
    {
        float window = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(kPi) *
                       static_cast<float>(i) / static_cast<float>(n - 1)));
        real[i] = in[i] * window;
    }

    computeFFT(real, imag);

    // First n/2 bins (up to Nyquist); linear magnitude sqrt(re^2 + im^2).
    const std::size_t half = n / 2;
    outMag.resize(half);
    for (std::size_t i = 0; i < half; ++i)
    {
        outMag[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
    }
}

} // namespace Vestige

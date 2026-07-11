// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_spectrum.cpp
/// @brief Phase 10 AX12 — coverage for the shared FFT magnitude helper
///        (`computeMagnitudeSpectrum`). Pins INV-1 (pure; sine peaks in the
///        expected bin) and INV-8 (deterministic, single Hann window).

#include <gtest/gtest.h>

#include "audio/audio_spectrum.h"

#include <cmath>
#include <cstddef>
#include <vector>

using namespace Vestige;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Index of the largest magnitude bin.
std::size_t argMax(const std::vector<float>& v)
{
    std::size_t best = 0;
    for (std::size_t i = 1; i < v.size(); ++i)
    {
        if (v[i] > v[best]) best = i;
    }
    return best;
}
}  // namespace

// -- INV-1: a sine at an exact bin peaks in that bin ----------------

TEST(AudioSpectrum, SinePeaksInExpectedBin)
{
    constexpr std::size_t n = 1024;
    constexpr int sr = 48000;
    constexpr std::size_t k = 64;  // exact bin → f = k*sr/n = 3000 Hz

    std::vector<float> in(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        in[i] = std::sin(2.0 * kPi * static_cast<double>(k) *
                         static_cast<double>(i) / static_cast<double>(n));
    }

    std::vector<float> mag;
    computeMagnitudeSpectrum(in.data(), n, mag);

    ASSERT_EQ(mag.size(), n / 2);
    EXPECT_EQ(argMax(mag), k);

    // bin i maps to i*sr/n Hz — sanity on the claimed mapping.
    EXPECT_NEAR(static_cast<double>(k) * sr / n, 3000.0, 1e-9);
}

// -- INV-1: DC input peaks in bin 0 ---------------------------------

TEST(AudioSpectrum, DcPeaksInBinZero)
{
    constexpr std::size_t n = 512;
    std::vector<float> in(n, 1.0f);

    std::vector<float> mag;
    computeMagnitudeSpectrum(in.data(), n, mag);

    ASSERT_EQ(mag.size(), n / 2);
    EXPECT_EQ(argMax(mag), std::size_t{0});
}

// -- Silence → all-zero magnitudes ----------------------------------

TEST(AudioSpectrum, SilenceIsAllZero)
{
    constexpr std::size_t n = 256;
    std::vector<float> in(n, 0.0f);

    std::vector<float> mag;
    computeMagnitudeSpectrum(in.data(), n, mag);

    ASSERT_EQ(mag.size(), n / 2);
    for (float m : mag)
    {
        EXPECT_FLOAT_EQ(m, 0.0f);
    }
}

// -- INV-8: deterministic (identical input → identical output) ------

TEST(AudioSpectrum, IsPureAndDeterministic)
{
    constexpr std::size_t n = 512;
    std::vector<float> in(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        in[i] = std::sin(2.0 * kPi * 40.0 * static_cast<double>(i) /
                         static_cast<double>(n)) * 0.7f;
    }

    std::vector<float> a;
    std::vector<float> b;
    computeMagnitudeSpectrum(in.data(), n, a);
    computeMagnitudeSpectrum(in.data(), n, b);

    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_FLOAT_EQ(a[i], b[i]);
    }
}

// -- Guards: non-power-of-two / zero / null clear the output --------

TEST(AudioSpectrum, RejectsNonPowerOfTwo)
{
    std::vector<float> in(1000, 0.5f);
    std::vector<float> mag(7, 1.0f);  // pre-filled → must be cleared
    computeMagnitudeSpectrum(in.data(), in.size(), mag);
    EXPECT_TRUE(mag.empty());
}

TEST(AudioSpectrum, RejectsZeroLengthAndNull)
{
    std::vector<float> mag(3, 1.0f);
    computeMagnitudeSpectrum(nullptr, 0, mag);
    EXPECT_TRUE(mag.empty());

    float dummy = 0.0f;
    std::vector<float> mag2(3, 1.0f);
    computeMagnitudeSpectrum(&dummy, 0, mag2);
    EXPECT_TRUE(mag2.empty());
}

// -- computeFFT: FFT of an impulse is flat (unit magnitude) ---------

TEST(AudioSpectrum, ImpulseFftIsFlat)
{
    constexpr std::size_t n = 64;
    std::vector<float> real(n, 0.0f);
    std::vector<float> imag(n, 0.0f);
    real[0] = 1.0f;  // unit impulse at t=0 → flat spectrum, magnitude 1

    computeFFT(real, imag);

    for (std::size_t i = 0; i < n; ++i)
    {
        float m = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
        EXPECT_NEAR(m, 1.0f, 1e-4f);
    }
}

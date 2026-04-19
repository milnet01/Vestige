// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_analyzer.cpp
/// @brief Real-time audio analysis for amplitude-based lip sync.
///
/// Uses RMS energy and a radix-2 Cooley-Tukey FFT for spectral analysis.
/// No external dependencies — pure C++17 implementation.
#include "animation/audio_analyzer.h"

#include <algorithm>
#include <cmath>

namespace Vestige
{

// FFT size — must be power of 2. 512 is sufficient for lip sync
// (covers up to ~86 Hz resolution at 44100 Hz sample rate).
static constexpr size_t FFT_SIZE = 512;

AudioAnalyzer::AudioAnalyzer() = default;

void AudioAnalyzer::feedSamples(const float* samples, size_t count, int sampleRate)
{
    if (samples == nullptr || count == 0)
    {
        return;
    }

    m_sampleRate = sampleRate;

    // --- RMS energy (time-domain) ---
    float sumSquares = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        sumSquares += samples[i] * samples[i];
    }
    m_rms = std::sqrt(sumSquares / static_cast<float>(count));

    // Apply silence threshold
    if (m_rms < m_silenceThreshold)
    {
        m_rms = 0.0f;
    }

    // --- FFT for spectral centroid (if we have enough samples) ---
    if (count >= FFT_SIZE)
    {
        // Use the last FFT_SIZE samples
        const float* fftStart = samples + (count - FFT_SIZE);

        std::vector<float> real(FFT_SIZE);
        std::vector<float> imag(FFT_SIZE, 0.0f);

        // Apply Hann window to reduce spectral leakage
        for (size_t i = 0; i < FFT_SIZE; ++i)
        {
            float window = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) *
                           static_cast<float>(i) / static_cast<float>(FFT_SIZE - 1)));
            real[i] = fftStart[i] * window;
        }

        computeFFT(real, imag);

        // Compute spectral centroid from magnitude spectrum
        // Only use first half (Nyquist)
        float weightedSum = 0.0f;
        float magnitudeSum = 0.0f;
        size_t halfSize = FFT_SIZE / 2;

        for (size_t i = 1; i < halfSize; ++i)  // Skip DC bin
        {
            float magnitude = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
            float frequency = static_cast<float>(i) * static_cast<float>(sampleRate) /
                              static_cast<float>(FFT_SIZE);
            weightedSum += frequency * magnitude;
            magnitudeSum += magnitude;
        }

        if (magnitudeSum > 1e-6f)
        {
            float centroidHz = weightedSum / magnitudeSum;
            // Normalize to [0, 1] range. Speech typically 100-4000 Hz.
            // Map: 100 Hz -> 0.0, 4000 Hz -> 1.0
            m_spectralCentroid = std::clamp((centroidHz - 100.0f) / 3900.0f, 0.0f, 1.0f);
        }
        else
        {
            m_spectralCentroid = 0.0f;
        }

        m_hasSpectralData = true;
    }
    else
    {
        m_hasSpectralData = false;
    }
}

float AudioAnalyzer::getRMS() const
{
    return m_rms;
}

float AudioAnalyzer::getSpectralCentroid() const
{
    return m_spectralCentroid;
}

Viseme AudioAnalyzer::getEstimatedViseme() const
{
    // Silent
    if (m_rms < 1e-4f)
    {
        return Viseme::X;
    }

    // Without spectral data, use RMS-only estimation
    if (!m_hasSpectralData)
    {
        // Simple volume-to-jaw mapping
        if (m_rms < 0.05f) return Viseme::B;   // Barely audible → teeth
        if (m_rms < 0.15f) return Viseme::C;   // Moderate → open
        return Viseme::D;                       // Loud → wide open
    }

    // With spectral data: RMS + centroid for better estimation
    //
    // High centroid (bright) = sibilants, consonants → B (teeth)
    // Low centroid + low volume = soft vowel → E (rounded)
    // Low centroid + high volume = open vowel → D (wide)
    // Mid centroid + mid volume = neutral → C (open)

    if (m_spectralCentroid > 0.6f)
    {
        // Bright sounds: sibilants (S, T, K) or F/V
        return (m_rms > 0.1f) ? Viseme::G : Viseme::B;
    }
    else if (m_spectralCentroid < 0.25f)
    {
        // Dark sounds: nasal (M, N) or rounded vowels
        if (m_rms < 0.08f) return Viseme::A;  // Quiet + dark = bilabial
        if (m_rms < 0.2f)  return Viseme::E;  // Moderate + dark = rounded
        return Viseme::D;                       // Loud + dark = wide open
    }
    else
    {
        // Mid-range: typical vowels
        if (m_rms < 0.1f)  return Viseme::C;  // Moderate → open
        if (m_rms < 0.25f) return Viseme::C;  // Standard open mouth
        return Viseme::D;                       // Loud → wide
    }
}

float AudioAnalyzer::getJawOpenWeight() const
{
    // Smoothstep from silence threshold to 0.3 RMS
    float t = std::clamp((m_rms - m_silenceThreshold) / (0.3f - m_silenceThreshold),
                         0.0f, 1.0f);
    // Smoothstep: 3t^2 - 2t^3
    return t * t * (3.0f - 2.0f * t);
}

bool AudioAnalyzer::hasSpectralData() const
{
    return m_hasSpectralData;
}

void AudioAnalyzer::setSilenceThreshold(float threshold)
{
    m_silenceThreshold = std::max(0.0f, threshold);
}

float AudioAnalyzer::getSilenceThreshold() const
{
    return m_silenceThreshold;
}

void AudioAnalyzer::reset()
{
    m_rms = 0.0f;
    m_spectralCentroid = 0.0f;
    m_hasSpectralData = false;
}

// ---------------------------------------------------------------------------
// Radix-2 Cooley-Tukey FFT (in-place, decimation-in-time)
// ---------------------------------------------------------------------------

/*static*/ void AudioAnalyzer::computeFFT(std::vector<float>& real, std::vector<float>& imag)
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
        float angle = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
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

} // namespace Vestige

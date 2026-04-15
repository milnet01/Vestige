// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_analyzer.h
/// @brief Real-time audio analysis for amplitude-based lip sync fallback.
#pragma once

#include "animation/viseme_map.h"

#include <cstddef>
#include <vector>

namespace Vestige
{

/// @brief Analyzes audio samples to extract features for lip sync.
///
/// Supports two levels of analysis:
///   1. RMS energy (time-domain, trivial cost) — drives jaw opening
///   2. Spectral centroid via FFT (frequency-domain) — distinguishes vowels from consonants
///
/// Feed normalized float PCM samples via feedSamples(). Query features after feeding.
/// No external dependencies — uses a built-in radix-2 Cooley-Tukey FFT.
class AudioAnalyzer
{
public:
    AudioAnalyzer();

    /// @brief Feeds normalized float PCM samples (range [-1, 1]).
    ///
    /// @param samples    Array of mono float samples.
    /// @param count      Number of samples.
    /// @param sampleRate Sample rate in Hz (e.g. 44100).
    void feedSamples(const float* samples, size_t count, int sampleRate);

    /// @brief Returns the current RMS energy [0, 1].
    float getRMS() const;

    /// @brief Returns the spectral centroid [0, 1] (0 = low/dark, 1 = high/bright).
    /// Only valid after feedSamples() with enough data for FFT.
    float getSpectralCentroid() const;

    /// @brief Estimates the best viseme from current audio features.
    Viseme getEstimatedViseme() const;

    /// @brief Returns a jaw-open weight [0, 1] derived from RMS energy.
    float getJawOpenWeight() const;

    /// @brief Returns true if spectral analysis (FFT) data is available.
    bool hasSpectralData() const;

    /// @brief Sets the silence threshold below which RMS maps to zero.
    void setSilenceThreshold(float threshold);

    /// @brief Gets the silence threshold.
    float getSilenceThreshold() const;

    /// @brief Resets all analysis state.
    void reset();

private:
    /// @brief Performs radix-2 Cooley-Tukey FFT in-place.
    void computeFFT(std::vector<float>& real, std::vector<float>& imag);

    float m_rms = 0.0f;
    float m_spectralCentroid = 0.0f;
    bool m_hasSpectralData = false;
    float m_silenceThreshold = 0.01f;
    int m_sampleRate = 44100;
};

} // namespace Vestige

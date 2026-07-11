// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_spectrum.h
/// @brief Shared pure-function FFT + magnitude-spectrum helper (Phase 10 AX12).
///        No OpenAL, no ImGui — unit-testable in isolation. The radix-2 FFT is
///        the one extracted from AudioAnalyzer (lip sync); both the audio
///        spectrum viewer and the analyzer call it, so the FFT lives in exactly
///        one place (reuse rule 3b).
#pragma once

#include <cstddef>
#include <vector>

namespace Vestige
{

/// @brief In-place radix-2 Cooley-Tukey FFT (decimation-in-time). @a real and
///        @a imag must be the same length and a power of two. Moved verbatim
///        from the former `AudioAnalyzer::computeFFT`.
void computeFFT(std::vector<float>& real, std::vector<float>& imag);

/// @brief Hann-window @a in (@a n samples, @a n a power of two), FFT it, and
///        write the first @a n/2 linear magnitude bins to @a outMag (resized to
///        n/2). Pure/deterministic. The Hann divisor is (n - 1), matching the
///        analyzer's window exactly. Bin i maps to frequency `i * sampleRate / n`.
///        If @a n is 0 or not a power of two, @a outMag is cleared and nothing
///        else happens.
void computeMagnitudeSpectrum(const float* in, std::size_t n,
                              std::vector<float>& outMag);

} // namespace Vestige

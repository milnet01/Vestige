// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_loudness.h
/// @brief Phase 10 audio quick-wins (AX9) — EBU R128 / ITU-R BS.1770
///        integrated-loudness measurement + makeup-gain computation.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Vestige
{

/// @brief The absolute silence gate (LUFS). A clip whose integrated
///        loudness is at or below this is treated as silence and gets
///        unity makeup — matches libebur128's −70 LUFS absolute gate,
///        below which integrated loudness is undefined.
inline constexpr float kLoudnessSilenceGateLufs = -70.0f;

/// @brief Measures the integrated loudness (LUFS) of a decoded PCM clip.
///
/// Thin wrapper over libebur128 (EBU R128 / ITU-R BS.1770 K-weighting +
/// gating). `interleaved` is signed-16-bit interleaved PCM exactly as
/// `AudioClip::getSamples()` stores it — libebur128's native `short`
/// ingest path is used, so no int16→float copy of the whole clip is made.
///
/// @param interleaved Interleaved int16 PCM (`frames * channels` samples).
/// @param frames      Number of sample frames (samples per channel).
/// @param channels    Channel count (1 = mono, 2 = stereo).
/// @param rate        Sample rate in Hz.
/// @returns Integrated loudness in LUFS. Returns `kLoudnessSilenceGateLufs`
///          for silent / near-silent / empty / invalid input (libebur128
///          reports such input as negative infinity), which
///          `loudnessMakeupGain` maps to unity makeup.
float integratedLoudnessLufs(const int16_t* interleaved,
                             std::size_t    frames,
                             int            channels,
                             int            rate);

/// @brief Linear makeup gain that moves a clip's measured loudness toward
///        the reference target, clamped so it never boosts more than
///        `maxBoostDb` (default +12 dB) and never amplifies the noise
///        floor of a silent clip.
///
/// Pure function (no libebur128 dependency) — testable without PCM:
///   gainDb = min(targetLufs − measuredLufs, maxBoostDb)
///   gain   = 10^(gainDb / 20)
/// A clip quieter than the target is boosted (up to the clamp); a clip
/// louder than the target is attenuated (unclamped downward — a hot clip
/// is pulled all the way down to match). A `measuredLufs` at or below the
/// silence gate (or non-finite) returns unity (1.0) so digital silence
/// and its dither floor are never amplified.
///
/// @param measuredLufs Integrated loudness from `integratedLoudnessLufs`.
/// @param targetLufs   Reference loudness (e.g. −16 game, −23 streamer).
/// @param maxBoostDb   Maximum upward gain in dB (default 12).
/// @returns Linear gain multiplier in (0, 10^(maxBoostDb/20)].
float loudnessMakeupGain(float measuredLufs,
                         float targetLufs,
                         float maxBoostDb = 12.0f);

} // namespace Vestige

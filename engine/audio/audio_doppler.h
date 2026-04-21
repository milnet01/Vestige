// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_doppler.h
/// @brief Phase 10 spatial audio — Doppler-shift pitch calculation for
///        fast-moving sources / listeners.
///
/// The pitch heard by a listener differs from the source pitch when
/// either endpoint has a velocity component along the source-listener
/// axis. Approaching endpoints raise the pitch; receding endpoints
/// lower it. The OpenAL 1.1 specification §3.5.2 formalises this as:
///
///     f' = f · (SS − DF · vLs) / (SS − DF · vSs)
///
/// where:
///   SS  = speed of sound (m/s, air ≈ 343.3 at 20 °C)
///   DF  = Doppler factor (global scale on the effect)
///   vLs = listener velocity projected onto the unit vector
///         pointing from source to listener
///   vSs = source velocity projected onto that same vector
///   vLs, vSs are each clamped to [−SS/DF, SS/DF] to prevent the
///   formula's denominator (or numerator) going non-positive when an
///   endpoint travels at or past the speed of sound.
///
/// The sign convention: a positive vLs means the listener is moving
/// *away* from the source (same direction as source→listener), which
/// — per the formula — *lowers* the pitch. Likewise a positive vSs
/// means the source is moving *toward* the listener, which *raises*
/// the pitch (denominator shrinks).
///
/// This module is pure-function. The engine consults
/// `computeDopplerPitchRatio` for CPU-side pitch decisions (editor
/// preview, virtual-voice priority); OpenAL evaluates the same
/// formula natively for playback when `AL_VELOCITY` is set on the
/// listener and on each source. The two implementations must agree —
/// enforced by the unit tests in `test_audio_doppler.cpp`.
///
/// Reference: OpenAL 1.1 Specification §3.5 ("Doppler Shift") and
/// OpenAL Soft Programmer's Guide §5.4. The formula is canonical
/// textbook — no coefficients to fit — so the engine-wide Formula
/// Workbench rule (author via fit + export) does not apply. See
/// CLAUDE.md Rule 11 note.
#pragma once

#include <glm/glm.hpp>

namespace Vestige
{

/// @brief Doppler-shift tuning parameters.
///
/// Defaults match the OpenAL 1.1 specification defaults (speedOfSound
/// = 343.3 m/s, dopplerFactor = 1.0). Setting `dopplerFactor = 0.0`
/// disables the effect entirely — `computeDopplerPitchRatio` returns
/// 1.0 for any velocity pair.
struct DopplerParams
{
    /// Speed of sound, in engine units (meters) per second. Air at
    /// 20 °C ≈ 343.3 m/s; water ≈ 1480; underwater scenes may lower
    /// this to make the effect more pronounced at slow speeds.
    float speedOfSound = 343.3f;

    /// Global scale on the Doppler effect. 0.0 disables; 1.0 matches
    /// the canonical formula; >1.0 exaggerates (cinematic), <1.0
    /// attenuates (subtle).
    float dopplerFactor = 1.0f;
};

/// @brief Computes the pitch multiplier due to Doppler shift.
///
/// @param params Tuning parameters — speed of sound and factor.
/// @param sourcePosition World position of the emitter.
/// @param sourceVelocity Emitter velocity (m/s).
/// @param listenerPosition World position of the listener (typically
///        the active camera / player head).
/// @param listenerVelocity Listener velocity (m/s).
/// @returns A strictly positive ratio f' / f. Values > 1 raise pitch
///          (approach), < 1 lower it (recede). Returns 1.0 when
///          `dopplerFactor == 0`, when source and listener are
///          co-located (no well-defined axis), or when both
///          velocities are zero.
float computeDopplerPitchRatio(const DopplerParams& params,
                               const glm::vec3& sourcePosition,
                               const glm::vec3& sourceVelocity,
                               const glm::vec3& listenerPosition,
                               const glm::vec3& listenerVelocity);

} // namespace Vestige

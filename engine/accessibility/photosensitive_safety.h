// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file photosensitive_safety.h
/// @brief Phase 10 accessibility — caps camera shake, strobe frequency,
///        flash alpha, and bloom intensity to photosensitive-epilepsy-safe
///        levels.
///
/// Grounded in WCAG 2.2 Success Criterion 2.3.1 ("Three Flashes or Below
/// Threshold"), the Epilepsy Society's "photosensitive epilepsy and
/// computer games" guidance, and the IGDA GA-SIG / Ubisoft / Microsoft
/// accessibility best-practice bullets. The limits below are deliberately
/// conservative: they sit well inside the published unsafe thresholds so
/// content authored against reasonable defaults stays comfortable even
/// when users toggle safe mode without per-scene retuning.
///
/// The module is policy-only. Subsystems consult `clampFlashAlpha`,
/// `clampShakeAmplitude`, `clampStrobeHz`, and `limitBloomIntensity`
/// before handing their values to the renderer/camera. When safe mode is
/// disabled every helper is an identity pass-through — zero runtime cost.
#pragma once

namespace Vestige
{

/// @brief Published caps applied when photosensitivity safe mode is on.
///
/// Fields are public + default-initialised so game code can override any
/// single cap (e.g. a scene that wants a slightly-higher shake ceiling)
/// without rebuilding a custom struct.
struct PhotosensitiveLimits
{
    /// Hard ceiling on per-frame screen-flash alpha (0..1). WCAG 2.2 SC
    /// 2.3.1 flags general flash risk above ~25% peak-luminance jumps on
    /// a red background; Xbox / Ubisoft accessibility guidance recommends
    /// capping overlay flash alpha to ≤0.25 in safe mode.
    float maxFlashAlpha       = 0.25f;

    /// Scalar multiplier applied to camera-shake amplitude (not an
    /// absolute cap — shake is unitless). 0.25 matches the
    /// Hades / Celeste "reduced screen shake" defaults.
    float shakeAmplitudeScale = 0.25f;

    /// Hard ceiling on strobe / flicker frequency (Hz). WCAG flags
    /// >3 Hz as unsafe on red content; 2.0 gives comfortable margin and
    /// still reads as a flicker rather than a jitter.
    float maxStrobeHz         = 2.0f;

    /// Scalar multiplier applied to bloom intensity to prevent
    /// sunburst-style highlight spikes that manifest as unannounced
    /// full-screen flashes. 0.6 is what AAA post-process stacks (Doom
    /// Eternal, Cyberpunk 2077 accessibility notes) land on in their
    /// reduced-flashing presets.
    float bloomIntensityScale = 0.6f;
};

/// @brief Clamps a one-shot overlay-flash alpha to the safe-mode ceiling.
/// @param alpha     The alpha the effect wants to apply this frame (0..1).
/// @param enabled   Current safe-mode state.
/// @param limits    Caps to apply (defaults to the published limits).
/// @returns `alpha` unchanged when `enabled` is false; otherwise
///          `min(alpha, limits.maxFlashAlpha)`.
float clampFlashAlpha(float alpha,
                      bool enabled,
                      const PhotosensitiveLimits& limits = {});

/// @brief Scales a camera-shake amplitude by the safe-mode multiplier.
/// @param amplitude Shake amplitude (unitless; >= 0).
/// @param enabled   Current safe-mode state.
/// @param limits    Caps to apply.
/// @returns `amplitude` unchanged when `enabled` is false; otherwise
///          `amplitude * limits.shakeAmplitudeScale`.
float clampShakeAmplitude(float amplitude,
                          bool enabled,
                          const PhotosensitiveLimits& limits = {});

/// @brief Clamps a strobe / flicker frequency to the safe-mode ceiling.
/// @param hz      Requested strobe frequency (Hz, >= 0).
/// @param enabled Current safe-mode state.
/// @param limits  Caps to apply.
/// @returns `hz` unchanged when `enabled` is false; otherwise
///          `min(hz, limits.maxStrobeHz)`.
float clampStrobeHz(float hz,
                    bool enabled,
                    const PhotosensitiveLimits& limits = {});

/// @brief Scales a bloom-intensity value by the safe-mode multiplier.
/// @param intensity Requested bloom intensity (>= 0).
/// @param enabled   Current safe-mode state.
/// @param limits    Caps to apply.
/// @returns `intensity` unchanged when `enabled` is false; otherwise
///          `intensity * limits.bloomIntensityScale`.
float limitBloomIntensity(float intensity,
                          bool enabled,
                          const PhotosensitiveLimits& limits = {});

} // namespace Vestige

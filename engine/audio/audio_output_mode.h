// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_output_mode.h
/// @brief Phase 10 audio quick-wins (AX8) — speaker-layout selection.
///
/// Lets the user pick the channel layout OpenAL Soft renders to
/// (mono / stereo / 5.1 / 7.1) instead of always taking the driver's
/// default downmix. The choice is routed through the same
/// `alcResetDeviceSOFT` reset path HRTF already uses (the
/// `ALC_SOFT_output_mode` extension).
///
/// Like `audio_hrtf.h`, this module is headless — it carries no
/// OpenAL linkage so the test suite can validate the layout enum +
/// the HRTF-precedence rule without opening an audio device. The
/// actual `ALC_OUTPUT_MODE_SOFT` token mapping lives in the `.cpp`
/// (which includes `<AL/alext.h>`), mirroring `alDistanceModelFor`
/// in `audio_attenuation.{h,cpp}`.
///
/// Reference: OpenAL Soft extension specification `ALC_SOFT_output_mode`
/// (https://openal-soft.org/openal-extensions/SOFT_output_mode.txt).
#pragma once

#include <string>

namespace Vestige
{

/// @brief Requested speaker layout. Persists in user audio
///        preferences and surfaces in the audio-settings panel.
///
/// `Mono` is offered deliberately: a single-channel output is a real
/// accessibility configuration for single-sided-hearing users, and
/// `ALC_MONO_SOFT` exists, so it costs one enum value.
enum class AudioOutputLayout
{
    Auto,        ///< Let the driver pick (`ALC_ANY_SOFT`).
    Mono,        ///< Force single-channel (`ALC_MONO_SOFT`).
    Stereo,      ///< Force two-channel (`ALC_STEREO_SOFT`).
    Surround51,  ///< 5.1 surround (`ALC_5POINT1_SOFT`).
    Surround71,  ///< 7.1 surround (`ALC_7POINT1_SOFT`).
};

/// @brief Stable label for an `AudioOutputLayout` — tests, settings
///        inspector, debug logging. Unknown values yield "Auto".
const char* audioOutputLayoutLabel(AudioOutputLayout layout);

/// @brief Wire-format string for an `AudioOutputLayout` (the token
///        written to settings JSON). Inverse of
///        `audioOutputLayoutFromString`.
std::string audioOutputLayoutToString(AudioOutputLayout layout);

/// @brief Parses a wire-format string back to an `AudioOutputLayout`.
///        Unknown tokens return @a fallback — mirrors
///        `qualityPresetFromString`'s unknown-token policy so old /
///        hand-edited settings files degrade to a safe default.
AudioOutputLayout audioOutputLayoutFromString(const std::string& s,
                                              AudioOutputLayout fallback = AudioOutputLayout::Auto);

/// @brief Resolves the `ALC_OUTPUT_MODE_SOFT` attribute value to
///        request for @a layout, honouring the HRTF precedence rule.
///
/// The return is an `ALCenum` value (typed `int` here to keep the
/// header headless, exactly as `alDistanceModelFor` returns `int`).
///
/// Precedence (the load-bearing interaction): HRTF is a *separate*
/// ALC attribute (`ALC_HRTF_SOFT`). When the persisted HRTF setting
/// is enabled the layout is ignored and this returns `ALC_ANY_SOFT`
/// so the driver resolves stereo-HRTF on headphones — surround is
/// never co-requested with HRTF. `hrtfEnabledSetting` is the
/// PERSISTED bool (`AudioSettings::hrtfEnabled`), known at apply
/// time, not the post-reset driver status.
///
/// Never returns `ALC_STEREO_HRTF_SOFT`: HRTF goes through the
/// separate `ALC_HRTF_SOFT` attribute, so it is never double-specified.
int resolveOutputMode(AudioOutputLayout layout, bool hrtfEnabledSetting);

} // namespace Vestige

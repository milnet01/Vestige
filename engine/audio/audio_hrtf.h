// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_hrtf.h
/// @brief Phase 10 spatial audio — HRTF (Head-Related Transfer
///        Function) selection + status enums.
///
/// HRTF applies a per-ear impulse-response convolution to each 3D
/// source so that positional cues (front/back, up/down) survive
/// stereo mixdown on headphones. It is markedly *worse* than plain
/// panning when played through speakers because the listener's own
/// ears re-convolve the signal — so the engine treats HRTF as a
/// headphone-only option, driven by an explicit user preference
/// rather than being on by default.
///
/// This module is headless — it carries no OpenAL linkage so the
/// test suite can validate mode/status labels and dataset selection
/// without opening an audio device. `AudioEngine` drives the
/// OpenAL-Soft `ALC_SOFT_HRTF` extension with these values:
///   - `HrtfMode::Disabled` forces HRTF off.
///   - `HrtfMode::Auto` lets the driver decide (typically: on when
///     stereo headphones are detected, off on speakers / surround).
///   - `HrtfMode::Forced` requests HRTF unconditionally. The driver
///     may still refuse (e.g. on multichannel output) — inspect
///     `AudioEngine::getHrtfStatus()` afterwards.
///
/// Reference: OpenAL Soft extension specification `ALC_SOFT_HRTF`
/// (https://openal-soft.org/openal-extensions/SOFT_HRTF.txt) and
/// the accompanying example `openal-soft/examples/alhrtf.c`.
#pragma once

#include <string>
#include <vector>

namespace Vestige
{

/// @brief HRTF activation policy. Surfaces in the audio-settings UI
///        and persists in user preferences.
enum class HrtfMode
{
    Disabled,   ///< HRTF always off — fastest, stereo-speaker-friendly.
    Auto,       ///< Driver decides — on for headphones, off for speakers.
    Forced,     ///< Request HRTF regardless; driver may still deny.
};

/// @brief Runtime state reported by the audio driver after a
///        context reset. Mirrors OpenAL's `ALC_HRTF_STATUS_SOFT`
///        values but stays in the engine namespace so the headless
///        tests don't need to pull OpenAL headers.
enum class HrtfStatus
{
    Disabled,             ///< HRTF off (user choice or no output match).
    Enabled,              ///< HRTF on and rendering.
    Denied,               ///< Driver rejected HRTF (e.g. wrong output format).
    Required,             ///< Output is headphones-only and HRTF is always on.
    HeadphonesDetected,   ///< Driver detected headphones and auto-enabled HRTF.
    UnsupportedFormat,    ///< Output format (surround, 5.1, etc.) doesn't support HRTF.
    Unknown,              ///< Query failed or the extension is unavailable.
};

/// @brief Per-engine HRTF configuration — paired with the settings
///        panel and persisted alongside the user's audio preferences.
struct HrtfSettings
{
    HrtfMode mode = HrtfMode::Auto;

    /// @brief Preferred dataset name — exact match against the
    ///        strings reported by `AudioEngine::getAvailableHrtfDatasets`.
    ///        Empty means "let the driver pick the default" (usually
    ///        dataset 0, a generic KEMAR recording).
    std::string preferredDataset;

    bool operator==(const HrtfSettings& other) const
    {
        return mode == other.mode
            && preferredDataset == other.preferredDataset;
    }

    bool operator!=(const HrtfSettings& other) const
    {
        return !(*this == other);
    }
};

/// @brief Stable label for an `HrtfMode` value — used by tests, the
///        settings inspector, and debug logging. Unknown enum values
///        yield the literal "Unknown" rather than a crash.
const char* hrtfModeLabel(HrtfMode mode);

/// @brief Stable label for an `HrtfStatus` value.
const char* hrtfStatusLabel(HrtfStatus status);

/// @brief Phase 10.9 Slice 2 P8 — payload delivered to the
///        AudioEngine HRTF status listener every time the engine
///        applies its HRTF settings.
///
/// Pairs the engine-stored request (`HrtfSettings`) with the
/// driver's resolved state (`HrtfStatus`). A downgrade — e.g.
/// `requestedMode = Forced` but `actualStatus = UnsupportedFormat`
/// — surfaces as different values in the same event, so the
/// Settings UI can show "Requested: Forced / Actual: Denied
/// (UnsupportedFormat)" from a single callback invocation.
struct HrtfStatusEvent
{
    HrtfMode    requestedMode    = HrtfMode::Auto;
    std::string requestedDataset;
    HrtfStatus  actualStatus     = HrtfStatus::Unknown;
};

/// @brief Pure composition: build an `HrtfStatusEvent` from the
///        engine's current `HrtfSettings` plus the driver's
///        resolved `HrtfStatus`. Kept free-function so the
///        headless test suite can exercise every combination
///        without opening an audio device.
HrtfStatusEvent composeHrtfStatusEvent(const HrtfSettings& settings,
                                       HrtfStatus actualStatus);

/// @brief Resolves the `preferredDataset` name against the list of
///        datasets the audio driver reports.
///
/// Sign convention for the return value:
///   -  >= 0   → valid index into `available`; pass to OpenAL via
///              `ALC_HRTF_ID_SOFT` to select this dataset.
///   -  -1     → dataset not selectable. Either `available` is empty
///              (driver reports no datasets) or `preferred` was set
///              to a name that does not appear in the list.
///
/// Empty `preferred` with a non-empty list returns 0 so the caller
/// can pass "take the first dataset" directly to OpenAL. The match
/// is case-sensitive to mirror OpenAL's own string comparison.
int resolveHrtfDatasetIndex(const std::vector<std::string>& available,
                            const std::string& preferred);

} // namespace Vestige

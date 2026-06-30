// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_device_hotswap.h
/// @brief Phase 10 audio quick-wins (AX11) — audio-device hot-swap policy.
///
/// When the OS default playback device changes mid-session (USB headphones
/// plugged in, a Bluetooth speaker disconnected), the engine can react
/// without a restart via OpenAL Soft's `ALC_SOFT_reopen_device` +
/// `ALC_SOFT_system_events` extensions. This module owns the *policy* — the
/// pure decision of what to do on a detected change — and is headless (no
/// OpenAL linkage) so the decision table and the settings enum round-trip
/// can be validated without opening an audio device. The actual ALC calls
/// live in `audio_engine.cpp`, mirroring how `resolveOutputMode` keeps the
/// `ALC_OUTPUT_MODE_SOFT` token mapping in `audio_output_mode.cpp`.
///
/// References: OpenAL Soft extensions `ALC_SOFT_reopen_device`
/// (https://openal-soft.org/openal-extensions/SOFT_reopen_device.txt) and
/// `ALC_SOFT_system_events`
/// (https://openal-soft.org/openal-extensions/SOFT_system_events.txt).
#pragma once

#include <string>

namespace Vestige
{

/// @brief What the engine does when the default playback device changes.
///        Persisted in user audio preferences; surfaces in the audio-
///        settings panel.
enum class DeviceHotSwapMode
{
    Off,     ///< Ignore device changes (pre-AX11 behaviour).
    Notify,  ///< Raise the device-change listener; swap on user confirm (default).
    Auto,    ///< Swap to the new default device silently.
};

/// @brief The action `AudioEngine::pollAndHandleDeviceChange` takes for a
///        pending change under a given policy. Pure result of
///        `decideDeviceSwapAction` — no device state involved, so the
///        decision table is unit-testable without an audio device.
enum class DeviceSwapAction
{
    Ignore,  ///< Drop the event (mode Off).
    Notify,  ///< Fire the device-change listener; defer the swap to confirm.
    Swap,    ///< Reopen onto the new default device now (mode Auto).
};

/// @brief Maps a hot-swap policy to the action for a detected change.
///        The decision table behind the slice.
DeviceSwapAction decideDeviceSwapAction(DeviceHotSwapMode mode);

/// @brief Human-readable label for a `DeviceHotSwapMode` — settings UI,
///        debug logging. Unknown values yield "Notify".
const char* deviceHotSwapModeLabel(DeviceHotSwapMode mode);

/// @brief Wire-format token written to settings JSON. Inverse of
///        `deviceHotSwapModeFromString`.
std::string deviceHotSwapModeToString(DeviceHotSwapMode mode);

/// @brief Parses a wire-format token back to a `DeviceHotSwapMode`.
///        Unknown tokens return @a fallback, so hand-edited / older
///        settings files degrade to a safe default.
DeviceHotSwapMode deviceHotSwapModeFromString(const std::string& s,
                                              DeviceHotSwapMode fallback = DeviceHotSwapMode::Notify);

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_apply.h
/// @brief Apply layer between `Settings` (the persisted struct) and the
///        engine subsystems that carry user-tunable state.
///
/// Slice 13.2 introduces the **display** apply path: pushing a
/// `DisplaySettings` block at a `Window` (or anything shaped like a
/// `Window`). Later slices add audio, accessibility, input bindings,
/// and the full `Settings::applyToEngine` orchestrator that calls
/// these in the right order.
///
/// Why a sink interface instead of a direct `Window&` forwarder?
/// Tests can't construct a real `Window` without a display — GLFW
/// initialisation + GL context creation require a connected screen.
/// The `DisplayApplySink` abstract base lets `test_settings.cpp`
/// supply a recording mock and verify that `applyDisplay` invokes
/// the sink with exactly the settings values. The real engine
/// bootstrap path uses `WindowDisplaySink` which forwards to a
/// live `Window`.
#pragma once

#include "audio/audio_mixer.h"   // AudioBus enum
#include "ui/ui_theme.h"         // UIScalePreset enum

namespace Vestige
{

struct DisplaySettings;          // core/settings.h
struct AudioSettings;            // core/settings.h
struct AccessibilitySettings;    // core/settings.h
class Window;                    // core/window.h
class UISystem;                  // systems/ui_system.h

/// @brief Minimal interface for anything that can accept a video-mode
///        change from the Settings apply path. Implemented by
///        `WindowDisplaySink` in production; mocked by tests.
class DisplayApplySink
{
public:
    virtual ~DisplayApplySink() = default;

    /// @brief Apply the requested video mode. Implementations are
    ///        expected to normalise and propagate the change (fire a
    ///        framebuffer-resize event, call the GLFW APIs, etc.).
    virtual void setVideoMode(int width, int height,
                              bool fullscreen, bool vsync) = 0;
};

/// @brief Concrete sink that forwards to a `Window`. Thin wrapper —
///        the real work lives in `Window::setVideoMode`.
class WindowDisplaySink final : public DisplayApplySink
{
public:
    explicit WindowDisplaySink(Window& window);

    void setVideoMode(int width, int height,
                      bool fullscreen, bool vsync) override;

private:
    Window& m_window;
};

/// @brief Pushes the display block of a `Settings` onto a sink.
///
/// @param display Resolution / vsync / fullscreen to apply.
/// @param sink    Destination — usually `WindowDisplaySink`, occasionally
///                a test mock.
///
/// @note Intentionally does **not** touch the quality preset or render
///       scale — those feed the renderer and are wired in later slices
///       (render scale is a Renderer concern; quality preset governs
///       shader variants / LOD bias / shadow resolution and is consumed
///       by individual subsystems).
void applyDisplay(const DisplaySettings& display, DisplayApplySink& sink);

// ================================================================
// Slice 13.3 — Audio apply path
// ================================================================

/// @brief Sink for audio settings (bus gains). HRTF apply is a
///        follow-on slice and goes through `AudioEngine` rather
///        than `AudioMixer`.
class AudioApplySink
{
public:
    virtual ~AudioApplySink() = default;

    /// @brief Set the stored gain for `bus`. Implementations clamp
    ///        to [0, 1] so a hand-edited settings.json cannot push
    ///        out-of-range values downstream.
    virtual void setBusGain(AudioBus bus, float gain) = 0;
};

/// @brief Production sink wrapping a live `AudioMixer`. Thin forwarder.
class AudioMixerApplySink final : public AudioApplySink
{
public:
    explicit AudioMixerApplySink(AudioMixer& mixer);
    void setBusGain(AudioBus bus, float gain) override;

private:
    AudioMixer& m_mixer;
};

/// @brief Pushes the audio block of a `Settings` onto a sink.
///
/// Applies every bus gain in enum order. HRTF is intentionally NOT
/// forwarded here; it lives on `AudioEngine` and gets its own slice.
void applyAudio(const AudioSettings& audio, AudioApplySink& sink);

// ================================================================
// Slice 13.3 — UI accessibility apply path
// ================================================================

/// @brief Sink for the three-toggle UI accessibility triad.
///
/// The string wire values (`"1.0x"`, `"1.25x"`, …) are translated to
/// `UIScalePreset` inside `applyUIAccessibility` so sinks only see
/// the typed enum.
class UIAccessibilityApplySink
{
public:
    virtual ~UIAccessibilityApplySink() = default;

    /// @brief Apply scale + contrast + motion as one batch so the
    ///        underlying theme rebuild runs exactly once.
    virtual void applyScaleContrastMotion(UIScalePreset scale,
                                           bool highContrast,
                                           bool reducedMotion) = 0;
};

/// @brief Production sink wrapping a live `UISystem`. Delegates to
///        `UISystem::applyAccessibilityBatch` (slice 13.3 addition).
class UISystemAccessibilityApplySink final : public UIAccessibilityApplySink
{
public:
    explicit UISystemAccessibilityApplySink(UISystem& ui);
    void applyScaleContrastMotion(UIScalePreset scale,
                                   bool highContrast,
                                   bool reducedMotion) override;

private:
    UISystem& m_ui;
};

/// @brief Pushes the UI accessibility fields of a `Settings` onto
///        a sink. Translates the wire-format scale-preset string
///        (`"1.0x"` / `"1.25x"` / `"1.5x"` / `"2.0x"`) to the
///        `UIScalePreset` enum. Unknown strings fall back to 1.0×.
///
/// @note Subtitle size, colour-vision filter, post-process
///       accessibility, and photosensitive safety fields are wired
///       in a follow-on slice (Renderer + SubtitleQueue sinks).
void applyUIAccessibility(const AccessibilitySettings& access,
                           UIAccessibilityApplySink& sink);

} // namespace Vestige

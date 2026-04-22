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

#include "accessibility/photosensitive_safety.h"     // PhotosensitiveLimits
#include "accessibility/post_process_accessibility.h" // PostProcessAccessibilitySettings
#include "audio/audio_hrtf.h"                         // HrtfMode
#include "audio/audio_mixer.h"                        // AudioBus enum
#include "renderer/color_vision_filter.h"             // ColorVisionMode
#include "ui/subtitle.h"                              // SubtitleSizePreset
#include "ui/ui_theme.h"                              // UIScalePreset enum

#include <vector>

namespace Vestige
{

struct DisplaySettings;          // core/settings.h
struct AudioSettings;            // core/settings.h
struct AccessibilitySettings;    // core/settings.h
struct ActionBindingWire;        // core/settings.h
class InputActionMap;            // input/input_bindings.h
class Window;                    // core/window.h
class UISystem;                  // systems/ui_system.h
class Renderer;                  // renderer/renderer.h
class SubtitleQueue;             // ui/subtitle.h

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

// ================================================================
// Slice 13.3b — Renderer accessibility apply path
//   (color vision + post-process effect toggles + fog scale)
// ================================================================

/// @brief Sink for the renderer-side accessibility fields: color
///        vision simulation mode + post-process effect toggles (DoF,
///        motion blur, fog + fog intensity + reduce-motion fog).
class RendererAccessibilityApplySink
{
public:
    virtual ~RendererAccessibilityApplySink() = default;

    virtual void setColorVisionMode(ColorVisionMode mode) = 0;

    /// @brief Applies the DoF / motion-blur / fog toggles as a
    ///        single struct push — matches
    ///        `Renderer::setPostProcessAccessibility` which stores
    ///        them collectively.
    virtual void setPostProcessAccessibility(
        const PostProcessAccessibilitySettings& pp) = 0;
};

/// @brief Production sink wrapping a live `Renderer`.
class RendererAccessibilityApplySinkImpl final
    : public RendererAccessibilityApplySink
{
public:
    explicit RendererAccessibilityApplySinkImpl(Renderer& renderer);
    void setColorVisionMode(ColorVisionMode mode) override;
    void setPostProcessAccessibility(
        const PostProcessAccessibilitySettings& pp) override;

private:
    Renderer& m_renderer;
};

/// @brief Pushes renderer-relevant accessibility fields onto a sink.
///
/// Translates the wire-format color-vision string
/// (`"none"` / `"protanopia"` / `"deuteranopia"` / `"tritanopia"`)
/// to the typed `ColorVisionMode` enum. Unknown strings fall back
/// to `Normal` to match the Settings validation policy. Maps the
/// `PostProcessAccessibilityWire` fields carried by
/// `AccessibilitySettings` onto the `PostProcessAccessibilitySettings`
/// struct consumed by the renderer.
void applyRendererAccessibility(const AccessibilitySettings& access,
                                 RendererAccessibilityApplySink& sink);

// ================================================================
// Slice 13.3b — Subtitle apply path
// ================================================================

/// @brief Sink for subtitle enable toggle + size preset.
class SubtitleApplySink
{
public:
    virtual ~SubtitleApplySink() = default;
    virtual void setSubtitlesEnabled(bool enabled) = 0;
    virtual void setSubtitleSize(SubtitleSizePreset preset) = 0;
};

/// @brief Production sink wrapping a live `SubtitleQueue`.
///
/// The queue doesn't currently expose an "enabled" flag — the engine
/// layer drains the queue whether or not subtitles are on. This sink
/// stores the enabled state for the engine layer to query; slice 14
/// (UI wiring) will route the query at render time.
class SubtitleQueueApplySink final : public SubtitleApplySink
{
public:
    explicit SubtitleQueueApplySink(SubtitleQueue& queue);
    void setSubtitlesEnabled(bool enabled) override { m_enabled = enabled; }
    void setSubtitleSize(SubtitleSizePreset preset) override;
    bool subtitlesEnabled() const { return m_enabled; }

private:
    SubtitleQueue& m_queue;
    bool m_enabled = true;
};

/// @brief Pushes the subtitle fields onto a sink. Translates the
///        wire-format size-preset string (`"small"` / `"medium"` /
///        `"large"` / `"xl"`) to the typed `SubtitleSizePreset`.
///        Unknown strings fall back to `Medium`.
void applySubtitleSettings(const AccessibilitySettings& access,
                            SubtitleApplySink& sink);

// ================================================================
// Slice 13.3b — HRTF apply path
// ================================================================

/// @brief Sink for the HRTF mode. `AudioSettings::hrtfEnabled` is a
///        bool; the apply function translates to `HrtfMode`
///        (`Disabled` when false, `Auto` when true — `Auto` lets the
///        driver decide, matching the AudioEngine default).
class AudioHrtfApplySink
{
public:
    virtual ~AudioHrtfApplySink() = default;
    virtual void setHrtfMode(HrtfMode mode) = 0;
};

/// @brief Pushes the HRTF toggle onto a sink.
void applyAudioHrtf(const AudioSettings& audio, AudioHrtfApplySink& sink);

// ================================================================
// Slice 13.3b — Photosensitive safety apply path
// ================================================================

/// @brief Sink for the photosensitive-safety caps. Effects that
///        consume `PhotosensitiveLimits` (camera shake, flash
///        overlays, strobes, bloom) read from wherever this sink
///        writes — typically a central engine-side store.
class PhotosensitiveApplySink
{
public:
    virtual ~PhotosensitiveApplySink() = default;
    virtual void setPhotosensitiveEnabled(bool enabled) = 0;
    virtual void setPhotosensitiveLimits(const PhotosensitiveLimits& limits) = 0;
};

/// @brief Pushes the photosensitive-safety fields onto a sink.
///
/// Maps the `PhotosensitiveSafetyWire` fields carried by
/// `AccessibilitySettings` onto the `PhotosensitiveLimits` struct.
void applyPhotosensitiveSafety(const AccessibilitySettings& access,
                                PhotosensitiveApplySink& sink);

// ================================================================
// Slice 13.4 — Input bindings apply + extract
// ================================================================

/// @brief Convert every registered action in `map` to its wire form
///        for serialisation. Wire order matches `map.actions()`.
///
/// **Wire format note:** the wire `scancode` field currently carries
/// the GLFW *key code* from `InputBinding::code`, not a true
/// scan code. Layout-preserving scancode translation (WASD stable
/// across AZERTY / Dvorak — the design-doc intent) requires GLFW's
/// `glfwGetKeyScancode` + a reverse lookup, and lands in a follow-on
/// slice. Stored-value-same-as-in-memory keeps 13.4's round-trip
/// testable without a GLFW context and matches what the user will
/// see if they hand-edit `settings.json`.
std::vector<ActionBindingWire> extractInputBindings(
    const InputActionMap& map);

/// @brief Apply `wires` to `map`'s already-registered actions.
///
/// Init-order contract: game code registers every action on the
/// map **before** settings load. An id in `wires` that doesn't
/// resolve to a registered action is dropped with a logged warning
/// — prevents a typo in a hand-edited `settings.json` from creating
/// a ghost action, and protects against stale saves referencing
/// actions removed from a newer engine build. Actions registered
/// on the map but absent from `wires` keep their current bindings.
///
/// Wire `device` strings: `"keyboard"` / `"mouse"` / `"gamepad"`
/// map to the corresponding `InputDevice`; `"none"` and unknown
/// strings both map to `InputDevice::None`.
void applyInputBindings(const std::vector<ActionBindingWire>& wires,
                         InputActionMap& map);

} // namespace Vestige

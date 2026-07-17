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
#include "audio/audio_device_hotswap.h"               // DeviceHotSwapMode
#include "audio/audio_hrtf.h"                         // HrtfMode
#include "audio/audio_mixer.h"                        // AudioBus enum
#include "audio/audio_output_mode.h"                  // AudioOutputLayout
#include "renderer/color_vision_filter.h"             // ColorVisionMode
#include "renderer/taa.h"                             // AntiAliasMode
#include "ui/subtitle.h"                              // SubtitleSizePreset
#include "ui/ui_theme.h"                              // UIScalePreset enum

#include <vector>

namespace Vestige
{

struct DisplaySettings;          // core/settings.h
enum class QualityPreset;        // core/settings.h
struct AudioSettings;            // core/settings.h
struct AccessibilitySettings;    // core/settings.h
struct LocalizationSettings;     // core/settings.h
struct ActionBindingWire;        // core/settings.h
class InputActionMap;            // input/input_bindings.h
class LocalizationService;       // localization/localization_service.h
class Window;                    // core/window.h
class UISystem;                  // systems/ui_system.h
class Renderer;                  // renderer/renderer.h
class SubtitleQueue;             // ui/subtitle.h
class AudioEngine;               // audio/audio_engine.h

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
// Tier 1 (Phase 10) — quality-preset apply path
//   render scale → DisplaySettings ; AA / SSAO / bloom / heavy-post → Renderer
// ================================================================

/// @brief Sink for the renderer-side knobs a quality preset drives:
///        anti-alias mode, SSAO, bloom, and the heavy-post perf gate
///        (volumetric fog + dynamic GI, §4.2).
///
/// @note `renderScale` is intentionally NOT on this sink. It is not
///       renderer state — it is a persisted `DisplaySettings` field the
///       engine's play-mode resize reads per frame (design §4.1). So
///       `applyQualityPreset` writes it into the `DisplaySettings`
///       object and pushes only the four renderer toggles here.
class RendererQualitySink
{
public:
    virtual ~RendererQualitySink() = default;
    virtual void setAntiAliasMode(AntiAliasMode mode) = 0;
    virtual void setSsaoEnabled(bool enabled) = 0;
    virtual void setBloomEnabled(bool enabled) = 0;
    virtual void setHeavyPostEnabled(bool enabled) = 0;
};

/// @brief Production sink wrapping a live `Renderer`. Thin forwarder to
///        the four existing setters (`setAntiAliasMode` / `setSsaoEnabled`
///        / `setBloomEnabled` / `setHeavyPostEnabled`).
class RendererQualityApplySinkImpl final : public RendererQualitySink
{
public:
    explicit RendererQualityApplySinkImpl(Renderer& renderer);
    void setAntiAliasMode(AntiAliasMode mode) override;
    void setSsaoEnabled(bool enabled) override;
    void setBloomEnabled(bool enabled) override;
    void setHeavyPostEnabled(bool enabled) override;

private:
    Renderer& m_renderer;
};

/// @brief Applies a quality preset (design §4.1). Writes the preset's
///        render-scale value into `display.renderScale` and pushes the
///        anti-alias mode + SSAO + bloom + heavy-post toggles onto
///        `sink`. `Custom` applies **nothing** — the player's individual
///        hand-tuned knobs stand (design §4.1 "Custom transition").
///
/// The preset does NOT set `display.qualityPreset` — the caller (the
/// settings panel / load path) owns that field, so hand-editing a knob
/// can flip it to `Custom` without this function clobbering it back.
void applyQualityPreset(QualityPreset preset, DisplaySettings& display,
                        RendererQualitySink& sink);

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
    /// Phase 10.9 P5: forwards to the live queue so the accessibility
    /// toggle reaches every consumer through
    /// `SubtitleQueue::activeSubtitles()`. Prior shipping code stored
    /// a local `m_enabled` flag that nothing read.
    void setSubtitlesEnabled(bool enabled) override { m_queue.setEnabled(enabled); }
    void setSubtitleSize(SubtitleSizePreset preset) override;
    bool subtitlesEnabled() const { return m_queue.isEnabled(); }

private:
    SubtitleQueue& m_queue;
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

/// @brief Production sink wrapping a live `AudioEngine`. Forwards to
///        `AudioEngine::setHrtfMode` — the driver may still refuse the
///        request (e.g. on multichannel output); inspect
///        `AudioEngine::getHrtfStatus()` afterwards if that matters.
class AudioEngineHrtfApplySink final : public AudioHrtfApplySink
{
public:
    explicit AudioEngineHrtfApplySink(AudioEngine& engine);
    void setHrtfMode(HrtfMode mode) override;

private:
    AudioEngine& m_engine;
};

// ================================================================
// AX8 — speaker-layout apply path (sibling to the HRTF path)
// ================================================================

/// @brief Sink for the speaker layout. Mirrors `AudioHrtfApplySink`:
///        the actual `alcResetDeviceSOFT` lives in `AudioEngine`; this
///        is the abstract seam the headless tests mock.
class AudioOutputApplySink
{
public:
    virtual ~AudioOutputApplySink() = default;
    virtual void setOutputLayout(AudioOutputLayout layout) = 0;
};

/// @brief Pushes the speaker-layout setting onto a sink.
void applyAudioOutput(const AudioSettings& audio, AudioOutputApplySink& sink);

/// @brief Production sink wrapping a live `AudioEngine`. Forwards to
///        `AudioEngine::setOutputLayout`. The driver falls back to the
///        nearest layout if it can't honour the request (or when HRTF
///        forces stereo) — see `resolveOutputMode`.
class AudioEngineOutputApplySink final : public AudioOutputApplySink
{
public:
    explicit AudioEngineOutputApplySink(AudioEngine& engine);
    void setOutputLayout(AudioOutputLayout layout) override;

private:
    AudioEngine& m_engine;
};

// ================================================================
// AX6 — air-absorption toggle apply path (sibling to the HRTF /
// output-layout paths; stored on AudioEngine, read by AudioSystem)
// ================================================================

/// @brief Sink for the air-absorption master toggle. Mirrors the HRTF /
///        output-layout sinks — the flag lives on `AudioEngine`; this is
///        the abstract seam the headless tests mock.
class AudioAirAbsorptionApplySink
{
public:
    virtual ~AudioAirAbsorptionApplySink() = default;
    virtual void setAirAbsorptionEnabled(bool enabled) = 0;
};

/// @brief Pushes the air-absorption toggle onto a sink.
void applyAudioAirAbsorption(const AudioSettings& audio,
                             AudioAirAbsorptionApplySink& sink);

/// @brief Production sink wrapping a live `AudioEngine`. Forwards to
///        `AudioEngine::setAirAbsorptionEnabled` — a plain stored flag,
///        no device reset (the per-frame compose reads it).
class AudioEngineAirAbsorptionApplySink final : public AudioAirAbsorptionApplySink
{
public:
    explicit AudioEngineAirAbsorptionApplySink(AudioEngine& engine);
    void setAirAbsorptionEnabled(bool enabled) override;

private:
    AudioEngine& m_engine;
};

// ================================================================
// AX5 — audio LOD-ladder toggle apply path (sibling to AX6's path)
// ================================================================

/// @brief Sink for the audio LOD master toggle. The flag lives on
///        `AudioEngine`; this is the abstract seam the tests mock.
class AudioLodApplySink
{
public:
    virtual ~AudioLodApplySink() = default;
    virtual void setLodEnabled(bool enabled) = 0;
};

/// @brief Pushes the LOD toggle onto a sink.
void applyAudioLod(const AudioSettings& audio, AudioLodApplySink& sink);

/// @brief Production sink wrapping a live `AudioEngine`. Forwards to
///        `AudioEngine::setLodEnabled` — a stored flag, no device reset.
class AudioEngineLodApplySink final : public AudioLodApplySink
{
public:
    explicit AudioEngineLodApplySink(AudioEngine& engine);
    void setLodEnabled(bool enabled) override;

private:
    AudioEngine& m_engine;
};

// ================================================================
// AX11 — audio device hot-swap policy apply path (sibling to AX5)
// ================================================================

/// @brief Sink for the audio device hot-swap policy. The mode lives on
///        `AudioEngine`; this is the abstract seam the tests mock.
class AudioDeviceHotSwapApplySink
{
public:
    virtual ~AudioDeviceHotSwapApplySink() = default;
    virtual void setDeviceHotSwapMode(DeviceHotSwapMode mode) = 0;
};

/// @brief Pushes the device hot-swap policy onto a sink.
void applyAudioDeviceHotSwap(const AudioSettings& audio,
                             AudioDeviceHotSwapApplySink& sink);

/// @brief Production sink wrapping a live `AudioEngine`. Forwards to
///        `AudioEngine::setDeviceHotSwapMode` — a stored policy enum, no
///        device reset (the next frame's poll reads it).
class AudioEngineDeviceHotSwapApplySink final : public AudioDeviceHotSwapApplySink
{
public:
    explicit AudioEngineDeviceHotSwapApplySink(AudioEngine& engine);
    void setDeviceHotSwapMode(DeviceHotSwapMode mode) override;

private:
    AudioEngine& m_engine;
};

// ================================================================
// AX9 — audio loudness-normalisation apply path
// ================================================================

/// @brief Sink for the loudness-normalisation toggle + target. Both live
///        on `AudioEngine`; this is the abstract seam the tests mock.
class AudioLoudnessApplySink
{
public:
    virtual ~AudioLoudnessApplySink() = default;
    virtual void setLoudnessEnabled(bool enabled) = 0;
    virtual void setLoudnessTargetLufs(float lufs) = 0;
};

/// @brief Pushes the loudness toggle + target onto a sink.
void applyAudioLoudness(const AudioSettings& audio, AudioLoudnessApplySink& sink);

/// @brief Production sink wrapping a live `AudioEngine`. Forwards to
///        `AudioEngine::setLoudnessEnabled` / `setLoudnessTargetLufs` —
///        stored fields, no device reset (the makeup is recomputed per
///        clip lookup against the current target).
class AudioEngineLoudnessApplySink final : public AudioLoudnessApplySink
{
public:
    explicit AudioEngineLoudnessApplySink(AudioEngine& engine);
    void setLoudnessEnabled(bool enabled) override;
    void setLoudnessTargetLufs(float lufs) override;

private:
    AudioEngine& m_engine;
};

// ================================================================
// AX4 S9 — procedural-audio apply path (master toggle + untagged gate)
// ================================================================

/// @brief Sink for the procedural-audio toggles. Both flags live on
///        `AudioEngine`; this is the abstract seam the headless tests mock.
class ProceduralAudioApplySink
{
public:
    virtual ~ProceduralAudioApplySink() = default;
    virtual void setProceduralAudioEnabled(bool enabled) = 0;
    virtual void setEmitUntaggedCollisions(bool enabled) = 0;
};

/// @brief Pushes the procedural-audio toggles onto a sink.
void applyProceduralAudio(const AudioSettings& audio, ProceduralAudioApplySink& sink);

/// @brief Production sink wrapping a live `AudioEngine`. Forwards to
///        `AudioEngine::setProceduralAudioEnabled` /
///        `setEmitUntaggedCollisions` — stored flags, no device reset (the
///        next `playSynth` / `ImpactAudioSystem` decision reads them).
class AudioEngineProceduralAudioApplySink final : public ProceduralAudioApplySink
{
public:
    explicit AudioEngineProceduralAudioApplySink(AudioEngine& engine);
    void setProceduralAudioEnabled(bool enabled) override;
    void setEmitUntaggedCollisions(bool enabled) override;

private:
    AudioEngine& m_engine;
};

// ================================================================
// AX1 — geometric audio occlusion apply path (sibling to AX5/AX6)
// ================================================================

/// @brief Sink for the occlusion settings. All four live on `AudioEngine`;
///        this is the abstract seam the headless tests mock.
class AudioOcclusionApplySink
{
public:
    virtual ~AudioOcclusionApplySink() = default;
    virtual void setOcclusionEnabled(bool enabled) = 0;
    virtual void setOcclusionRayCount(int count) = 0;
    virtual void setOcclusionMaxDistance(float metres) = 0;
    virtual void setOcclusionSourceRadius(float metres) = 0;
};

/// @brief Pushes the occlusion settings onto a sink.
void applyAudioOcclusion(const AudioSettings& audio,
                         AudioOcclusionApplySink& sink);

/// @brief Production sink wrapping a live `AudioEngine`. Forwards to the
///        `AudioEngine::setOcclusion*` stored fields — no device reset (the
///        next `AudioOcclusionSystem::update` reads them).
class AudioEngineOcclusionApplySink final : public AudioOcclusionApplySink
{
public:
    explicit AudioEngineOcclusionApplySink(AudioEngine& engine);
    void setOcclusionEnabled(bool enabled) override;
    void setOcclusionRayCount(int count) override;
    void setOcclusionMaxDistance(float metres) override;
    void setOcclusionSourceRadius(float metres) override;

private:
    AudioEngine& m_engine;
};

// ================================================================
// AX2 R4 — reverb settings apply path (master toggle + wet cap +
// convolution-backend gate; all stored on AudioEngine)
// ================================================================

/// @brief Sink for the three reverb settings. All live on `AudioEngine`; this
///        is the abstract seam the headless tests mock.
class AudioReverbApplySink
{
public:
    virtual ~AudioReverbApplySink() = default;
    virtual void setReverbEnabled(bool enabled) = 0;
    virtual void setReverbWetCap(float cap) = 0;
    virtual void setReverbConvolutionEnabled(bool enabled) = 0;
};

/// @brief Pushes the reverb settings onto a sink.
void applyAudioReverb(const AudioSettings& audio, AudioReverbApplySink& sink);

/// @brief Production sink wrapping a live `AudioEngine`. The enable + wet-cap
///        setters take effect the next frame (`ReverbSystem` reads them); the
///        convolution gate is read once at `AudioEngine::initialize()`, so a
///        runtime flip lands at the next launch (boot's `forceLiveApply()`
///        pushes the persisted value before init).
class AudioEngineReverbApplySink final : public AudioReverbApplySink
{
public:
    explicit AudioEngineReverbApplySink(AudioEngine& engine);
    void setReverbEnabled(bool enabled) override;
    void setReverbWetCap(float cap) override;
    void setReverbConvolutionEnabled(bool enabled) override;

private:
    AudioEngine& m_engine;
};

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

/// @brief Production sink writing to an engine-owned `enabled` flag
///        and `PhotosensitiveLimits` struct (held on `Engine` so every
///        consumer that needs the caps can read a single source of
///        truth). The sink does NOT drive effect consumers directly —
///        consumers (camera shake, flash overlay, bloom post, strobe
///        emitters) call the `clampFlashAlpha` / `clampShakeAmplitude`
///        / `clampStrobeHz` / `limitBloomIntensity` helpers with the
///        stored values at their own call sites.
class PhotosensitiveStoreApplySink final : public PhotosensitiveApplySink
{
public:
    /// @param enabled  Pointer to the engine's `enabled` flag.
    /// @param limits   Pointer to the engine's caps struct.
    ///                 Both pointers must outlive the sink; the engine
    ///                 owns the storage.
    PhotosensitiveStoreApplySink(bool* enabled, PhotosensitiveLimits* limits);

    void setPhotosensitiveEnabled(bool enabled) override;
    void setPhotosensitiveLimits(const PhotosensitiveLimits& limits) override;

private:
    bool* m_enabled;
    PhotosensitiveLimits* m_limits;
};

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

// ================================================================
// Slice L5 — Localization apply path
// ================================================================

/// @brief Sink for the active UI language. The production sink wraps
///        the engine's `LocalizationService`; tests can substitute a
///        recording fake to verify the live-apply path without a
///        full registry.
class LocalizationApplySink
{
public:
    virtual ~LocalizationApplySink() = default;
    virtual void setLanguage(const std::string& code) = 0;
};

/// @brief Pushes the language code onto a sink.
void applyLocalization(const LocalizationSettings& localization,
                        LocalizationApplySink& sink);

/// @brief Production sink wrapping a live `LocalizationService`.
///
/// `SettingsEditor` re-pushes every sink on every mutation, but
/// `LocalizationService::setLanguage` reloads the JSON table and
/// publishes a `LanguageChangedEvent` on each call. Forwarding
/// unconditionally would re-fire that event (and trigger panel
/// rebuilds) on an unrelated slider drag. So this sink no-ops when
/// the requested code already matches the service's active language —
/// making it idempotent-cheap like the other setters.
class LocalizationServiceApplySink final : public LocalizationApplySink
{
public:
    explicit LocalizationServiceApplySink(LocalizationService& service);
    void setLanguage(const std::string& code) override;

private:
    LocalizationService& m_service;
};

} // namespace Vestige

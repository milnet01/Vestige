// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_apply.cpp
/// @brief Apply layer between Settings and engine subsystems — display.
#include "core/settings_apply.h"

#include "core/logger.h"
#include "core/settings.h"
#include "core/window.h"
#include "input/input_bindings.h"
#include "renderer/renderer.h"
#include "systems/ui_system.h"
#include "ui/subtitle.h"

#include <cstddef>

namespace Vestige
{

WindowDisplaySink::WindowDisplaySink(Window& window)
    : m_window(window)
{
}

void WindowDisplaySink::setVideoMode(int width, int height,
                                     bool fullscreen, bool vsync)
{
    m_window.setVideoMode(width, height, fullscreen, vsync);
}

void applyDisplay(const DisplaySettings& display, DisplayApplySink& sink)
{
    sink.setVideoMode(display.windowWidth,
                      display.windowHeight,
                      display.fullscreen,
                      display.vsync);
}

// ================================================================
// Slice 13.3 — Audio apply
// ================================================================

AudioMixerApplySink::AudioMixerApplySink(AudioMixer& mixer)
    : m_mixer(mixer)
{
}

void AudioMixerApplySink::setBusGain(AudioBus bus, float gain)
{
    m_mixer.setBusGain(bus, gain);
}

void applyAudio(const AudioSettings& audio, AudioApplySink& sink)
{
    // AudioBus enum ordering matches AudioSettings::busGains indices:
    // 0 Master, 1 Music, 2 Voice, 3 Sfx, 4 Ambient, 5 Ui. The Settings
    // JSON layer already uses these index-to-name mappings; the apply
    // path just pushes in enum order.
    static_assert(AudioBusCount == 6,
        "AudioBus enum assumed to have exactly 6 entries");

    for (std::size_t i = 0; i < AudioBusCount; ++i)
    {
        sink.setBusGain(static_cast<AudioBus>(i),
                        audio.busGains[i]);
    }
}

// ================================================================
// Slice 13.3 — UI accessibility apply
// ================================================================

namespace
{

/// @brief Map the wire-format scale-preset string to the typed enum.
///        Unknown strings fall back to 1.0× — matches the Settings
///        validation policy (`isValidScalePreset` in settings.cpp).
UIScalePreset uiScalePresetFromWireString(const std::string& s)
{
    if (s == "1.25x") return UIScalePreset::X1_25;
    if (s == "1.5x")  return UIScalePreset::X1_5;
    if (s == "2.0x")  return UIScalePreset::X2_0;
    // "1.0x" and anything unexpected both fall back to 1.0×.
    return UIScalePreset::X1_0;
}

} // namespace

UISystemAccessibilityApplySink::UISystemAccessibilityApplySink(UISystem& ui)
    : m_ui(ui)
{
}

void UISystemAccessibilityApplySink::applyScaleContrastMotion(
    UIScalePreset scale, bool highContrast, bool reducedMotion)
{
    m_ui.applyAccessibilityBatch(scale, highContrast, reducedMotion);
}

void applyUIAccessibility(const AccessibilitySettings& access,
                           UIAccessibilityApplySink& sink)
{
    const UIScalePreset scale = uiScalePresetFromWireString(access.uiScalePreset);
    sink.applyScaleContrastMotion(scale,
                                   access.highContrast,
                                   access.reducedMotion);
}

// ================================================================
// Slice 13.3b — Renderer accessibility apply
// ================================================================

namespace
{

ColorVisionMode colorVisionModeFromWireString(const std::string& s)
{
    if (s == "protanopia")   return ColorVisionMode::Protanopia;
    if (s == "deuteranopia") return ColorVisionMode::Deuteranopia;
    if (s == "tritanopia")   return ColorVisionMode::Tritanopia;
    return ColorVisionMode::Normal;   // "none" + unknown strings
}

PostProcessAccessibilitySettings postProcessFromWire(
    const PostProcessAccessibilityWire& w)
{
    PostProcessAccessibilitySettings pp;
    pp.depthOfFieldEnabled = w.depthOfFieldEnabled;
    pp.motionBlurEnabled   = w.motionBlurEnabled;
    pp.fogEnabled          = w.fogEnabled;
    pp.fogIntensityScale   = w.fogIntensityScale;
    pp.reduceMotionFog     = w.reduceMotionFog;
    return pp;
}

} // namespace

RendererAccessibilityApplySinkImpl::RendererAccessibilityApplySinkImpl(
    Renderer& renderer)
    : m_renderer(renderer)
{
}

void RendererAccessibilityApplySinkImpl::setColorVisionMode(
    ColorVisionMode mode)
{
    m_renderer.setColorVisionMode(mode);
}

void RendererAccessibilityApplySinkImpl::setPostProcessAccessibility(
    const PostProcessAccessibilitySettings& pp)
{
    m_renderer.setPostProcessAccessibility(pp);
}

void applyRendererAccessibility(const AccessibilitySettings& access,
                                 RendererAccessibilityApplySink& sink)
{
    sink.setColorVisionMode(
        colorVisionModeFromWireString(access.colorVisionFilter));
    sink.setPostProcessAccessibility(
        postProcessFromWire(access.postProcess));
}

// ================================================================
// Slice 13.3b — Subtitle apply
// ================================================================

namespace
{

SubtitleSizePreset subtitleSizeFromWireString(const std::string& s)
{
    if (s == "small") return SubtitleSizePreset::Small;
    if (s == "large") return SubtitleSizePreset::Large;
    if (s == "xl")    return SubtitleSizePreset::XL;
    return SubtitleSizePreset::Medium;   // "medium" + unknown strings
}

} // namespace

SubtitleQueueApplySink::SubtitleQueueApplySink(SubtitleQueue& queue)
    : m_queue(queue)
{
}

void SubtitleQueueApplySink::setSubtitleSize(SubtitleSizePreset preset)
{
    m_queue.setSizePreset(preset);
}

void applySubtitleSettings(const AccessibilitySettings& access,
                            SubtitleApplySink& sink)
{
    sink.setSubtitlesEnabled(access.subtitlesEnabled);
    sink.setSubtitleSize(subtitleSizeFromWireString(access.subtitleSize));
}

// ================================================================
// Slice 13.3b — HRTF apply
// ================================================================

void applyAudioHrtf(const AudioSettings& audio, AudioHrtfApplySink& sink)
{
    // `Auto` defers to the driver's own heuristic (typically enables
    // when headphones are detected); matches the AudioEngine default
    // when the user hasn't opted in. `Disabled` forces it off.
    sink.setHrtfMode(audio.hrtfEnabled ? HrtfMode::Auto : HrtfMode::Disabled);
}

// ================================================================
// Slice 13.3b — Photosensitive safety apply
// ================================================================

void applyPhotosensitiveSafety(const AccessibilitySettings& access,
                                PhotosensitiveApplySink& sink)
{
    const auto& w = access.photosensitiveSafety;
    sink.setPhotosensitiveEnabled(w.enabled);

    PhotosensitiveLimits limits;
    limits.maxFlashAlpha       = w.maxFlashAlpha;
    limits.shakeAmplitudeScale = w.shakeAmplitudeScale;
    limits.maxStrobeHz         = w.maxStrobeHz;
    limits.bloomIntensityScale = w.bloomIntensityScale;
    sink.setPhotosensitiveLimits(limits);
}

// ================================================================
// Slice 13.4 — Input bindings extract + apply
// ================================================================

namespace
{

const char* inputDeviceToWireString(InputDevice d)
{
    switch (d)
    {
        case InputDevice::Keyboard: return "keyboard";
        case InputDevice::Mouse:    return "mouse";
        case InputDevice::Gamepad:  return "gamepad";
        case InputDevice::None:     return "none";
    }
    return "none";
}

InputDevice inputDeviceFromWireString(const std::string& s)
{
    if (s == "keyboard") return InputDevice::Keyboard;
    if (s == "mouse")    return InputDevice::Mouse;
    if (s == "gamepad")  return InputDevice::Gamepad;
    return InputDevice::None;   // "none" + unknown strings
}

InputBindingWire bindingToWire(const InputBinding& b)
{
    InputBindingWire w;
    w.device   = inputDeviceToWireString(b.device);
    w.scancode = b.code;
    return w;
}

InputBinding bindingFromWire(const InputBindingWire& w)
{
    InputBinding b;
    b.device = inputDeviceFromWireString(w.device);
    b.code   = w.scancode;
    // Normalise an "unbound" wire entry: if either device or code
    // says "unbound", force both to their unbound sentinel so the
    // in-memory `isBound()` check returns false consistently.
    if (b.device == InputDevice::None || b.code < 0)
    {
        b = InputBinding::none();
    }
    return b;
}

} // namespace

std::vector<ActionBindingWire> extractInputBindings(
    const InputActionMap& map)
{
    std::vector<ActionBindingWire> out;
    out.reserve(map.actions().size());
    for (const InputAction& action : map.actions())
    {
        ActionBindingWire w;
        w.id        = action.id;
        w.primary   = bindingToWire(action.primary);
        w.secondary = bindingToWire(action.secondary);
        w.gamepad   = bindingToWire(action.gamepad);
        out.push_back(w);
    }
    return out;
}

void applyInputBindings(const std::vector<ActionBindingWire>& wires,
                         InputActionMap& map)
{
    for (const ActionBindingWire& w : wires)
    {
        InputAction* action = map.findAction(w.id);
        if (!action)
        {
            // Phantom id — logged at warning level so hand-edited
            // settings.json typos surface, but non-fatal. Protects
            // the map from accumulating ghost entries.
            Logger::warning(
                "Settings: input binding for unknown action id '"
                + w.id + "' dropped. Register the action before "
                "loading settings, or remove the entry from "
                "settings.json.");
            continue;
        }
        action->primary   = bindingFromWire(w.primary);
        action->secondary = bindingFromWire(w.secondary);
        action->gamepad   = bindingFromWire(w.gamepad);
    }
}

} // namespace Vestige

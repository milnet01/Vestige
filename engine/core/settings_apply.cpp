// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_apply.cpp
/// @brief Apply layer between Settings and engine subsystems — display.
#include "core/settings_apply.h"

#include "core/settings.h"
#include "core/window.h"
#include "systems/ui_system.h"

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

} // namespace Vestige

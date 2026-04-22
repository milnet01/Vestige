// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_editor_panel.cpp
/// @brief Phase 10 slice 13.5b — ImGui editor panel implementation.
#include "editor/panels/settings_editor_panel.h"

#include "core/logger.h"
#include "core/settings_editor.h"
#include "input/input_bindings.h"

#include <imgui.h>

#include <cstddef>
#include <string>

namespace Vestige
{

namespace
{

const char* kTabNames[] = {
    "Display",
    "Audio",
    "Controls",
    "Gameplay",
    "Accessibility",
};

constexpr std::size_t kNumTabs = sizeof(kTabNames) / sizeof(kTabNames[0]);

} // namespace

void SettingsEditorPanel::initialize(SettingsEditor* editor,
                                      InputActionMap* inputMap,
                                      std::filesystem::path settingsPath)
{
    m_editor       = editor;
    m_inputMap     = inputMap;
    m_settingsPath = std::move(settingsPath);
}

void SettingsEditorPanel::draw()
{
    if (!m_open || !m_editor)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0f, 560.0f), ImGuiCond_FirstUseEver);
    bool stayOpen = true;
    if (!ImGui::Begin("Settings", &stayOpen))
    {
        ImGui::End();
        m_open = stayOpen;
        return;
    }

    // Left sidebar — category list.
    ImGui::BeginChild("##settings_sidebar", ImVec2(160.0f, -80.0f), true);
    for (std::size_t i = 0; i < kNumTabs; ++i)
    {
        const bool selected = (m_activeTab == static_cast<int>(i));
        if (ImGui::Selectable(kTabNames[i], selected))
        {
            m_activeTab = static_cast<int>(i);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right content area.
    ImGui::BeginChild("##settings_content", ImVec2(0.0f, -80.0f), true);
    switch (m_activeTab)
    {
        case 0: drawDisplayTab();       break;
        case 1: drawAudioTab();         break;
        case 2: drawControlsTab();      break;
        case 3: drawGameplayTab();      break;
        case 4: drawAccessibilityTab(); break;
        default: break;
    }
    ImGui::EndChild();

    drawFooter();

    ImGui::End();
    m_open = stayOpen;
}

void SettingsEditorPanel::drawDisplayTab()
{
    const Settings& p = m_editor->pending();
    ImGui::TextDisabled("Display");
    ImGui::Separator();

    // Resolution — two ints + common presets. Keep simple: one
    // combobox of 8 standard resolutions + a manual override.
    int w = p.display.windowWidth;
    int h = p.display.windowHeight;
    if (ImGui::InputInt("Width", &w, 16, 64))
    {
        m_editor->mutate([w](Settings& s) { s.display.windowWidth = w; });
    }
    if (ImGui::InputInt("Height", &h, 16, 64))
    {
        m_editor->mutate([h](Settings& s) { s.display.windowHeight = h; });
    }

    bool fullscreen = p.display.fullscreen;
    if (ImGui::Checkbox("Fullscreen", &fullscreen))
    {
        m_editor->mutate(
            [fullscreen](Settings& s) { s.display.fullscreen = fullscreen; });
    }
    bool vsync = p.display.vsync;
    if (ImGui::Checkbox("VSync", &vsync))
    {
        m_editor->mutate([vsync](Settings& s) { s.display.vsync = vsync; });
    }

    // Quality preset combo.
    const char* presets[] = {"low", "medium", "high", "ultra", "custom"};
    const std::string current = qualityPresetToString(p.display.qualityPreset);
    int idx = 0;
    for (int i = 0; i < 5; ++i)
    {
        if (current == presets[i]) { idx = i; break; }
    }
    if (ImGui::Combo("Quality preset", &idx, presets, 5))
    {
        const std::string chosen = presets[idx];
        m_editor->mutate([chosen](Settings& s)
        {
            s.display.qualityPreset = qualityPresetFromString(chosen);
        });
    }

    float scale = p.display.renderScale;
    if (ImGui::SliderFloat("Render scale", &scale, 0.25f, 2.0f, "%.2f"))
    {
        m_editor->mutate([scale](Settings& s) { s.display.renderScale = scale; });
    }

    ImGui::Spacing();
    if (ImGui::Button("Restore display defaults"))
    {
        m_editor->restoreDisplayDefaults();
    }
}

void SettingsEditorPanel::drawAudioTab()
{
    const Settings& p = m_editor->pending();
    ImGui::TextDisabled("Audio");
    ImGui::Separator();

    const char* busLabels[] = {
        "Master", "Music", "Voice", "SFX", "Ambient", "UI",
    };
    for (std::size_t i = 0; i < AudioBusCount; ++i)
    {
        float v = p.audio.busGains[i];
        if (ImGui::SliderFloat(busLabels[i], &v, 0.0f, 1.0f, "%.2f"))
        {
            const std::size_t idx = i;
            const float       nv  = v;
            m_editor->mutate(
                [idx, nv](Settings& s) { s.audio.busGains[idx] = nv; });
        }
    }

    bool hrtf = p.audio.hrtfEnabled;
    if (ImGui::Checkbox("HRTF (headphone spatial audio)", &hrtf))
    {
        m_editor->mutate([hrtf](Settings& s) { s.audio.hrtfEnabled = hrtf; });
    }

    ImGui::Spacing();
    if (ImGui::Button("Restore audio defaults"))
    {
        m_editor->restoreAudioDefaults();
    }
}

void SettingsEditorPanel::drawControlsTab()
{
    const Settings& p = m_editor->pending();
    ImGui::TextDisabled("Controls");
    ImGui::Separator();

    float sens = p.controls.mouseSensitivity;
    if (ImGui::SliderFloat("Mouse sensitivity", &sens, 0.1f, 10.0f, "%.2f"))
    {
        m_editor->mutate([sens](Settings& s) { s.controls.mouseSensitivity = sens; });
    }

    bool invertY = p.controls.invertY;
    if (ImGui::Checkbox("Invert Y (mouse)", &invertY))
    {
        m_editor->mutate([invertY](Settings& s) { s.controls.invertY = invertY; });
    }

    float dzL = p.controls.gamepadDeadzoneLeft;
    if (ImGui::SliderFloat("Gamepad left stick deadzone", &dzL, 0.0f, 0.9f, "%.2f"))
    {
        m_editor->mutate([dzL](Settings& s) { s.controls.gamepadDeadzoneLeft = dzL; });
    }

    float dzR = p.controls.gamepadDeadzoneRight;
    if (ImGui::SliderFloat("Gamepad right stick deadzone", &dzR, 0.0f, 0.9f, "%.2f"))
    {
        m_editor->mutate([dzR](Settings& s) { s.controls.gamepadDeadzoneRight = dzR; });
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Keybindings");
    ImGui::Separator();

    if (!m_inputMap || m_inputMap->actions().empty())
    {
        ImGui::TextUnformatted("No input actions registered.");
    }
    else
    {
        // Three-column layout (Primary / Secondary / Gamepad) per
        // action. Rebind capture lands in slice 13.5c; this slice
        // shows a read-only labelled table so the layout is in place.
        if (ImGui::BeginTable("##keybinds", 4,
                               ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("Primary");
            ImGui::TableSetupColumn("Secondary");
            ImGui::TableSetupColumn("Gamepad");
            ImGui::TableHeadersRow();

            for (const InputAction& a : m_inputMap->actions())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(a.label.empty() ? a.id.c_str()
                                                       : a.label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(bindingDisplayLabel(a.primary).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(bindingDisplayLabel(a.secondary).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(bindingDisplayLabel(a.gamepad).c_str());
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled(
            "(Rebind capture lands in slice 13.5c — click-to-rebind not yet wired.)");
    }

    ImGui::Spacing();
    if (ImGui::Button("Restore controls defaults"))
    {
        m_editor->restoreControlsDefaults();
    }
}

void SettingsEditorPanel::drawGameplayTab()
{
    ImGui::TextDisabled("Gameplay");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Gameplay settings are stored as free-form JSON and consumed "
        "by individual game projects. A generic editor here would be "
        "noise — ship your game-specific gameplay settings UI that "
        "mutates `SettingsEditor::pending().gameplay` directly.");

    ImGui::Spacing();
    if (ImGui::Button("Restore gameplay defaults"))
    {
        m_editor->restoreGameplayDefaults();
    }
}

void SettingsEditorPanel::drawAccessibilityTab()
{
    const Settings& p = m_editor->pending();
    ImGui::TextDisabled("Accessibility");
    ImGui::Separator();

    // UI scale preset — combo over the four wire strings.
    const char* scalePresets[] = {"1.0x", "1.25x", "1.5x", "2.0x"};
    int scaleIdx = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (p.accessibility.uiScalePreset == scalePresets[i]) { scaleIdx = i; break; }
    }
    if (ImGui::Combo("UI scale", &scaleIdx, scalePresets, 4))
    {
        const std::string s = scalePresets[scaleIdx];
        m_editor->mutate([s](Settings& set) { set.accessibility.uiScalePreset = s; });
    }

    bool hc = p.accessibility.highContrast;
    if (ImGui::Checkbox("High contrast", &hc))
    {
        m_editor->mutate([hc](Settings& s) { s.accessibility.highContrast = hc; });
    }
    bool rm = p.accessibility.reducedMotion;
    if (ImGui::Checkbox("Reduced motion", &rm))
    {
        m_editor->mutate([rm](Settings& s) { s.accessibility.reducedMotion = rm; });
    }
    bool subs = p.accessibility.subtitlesEnabled;
    if (ImGui::Checkbox("Subtitles enabled", &subs))
    {
        m_editor->mutate([subs](Settings& s) { s.accessibility.subtitlesEnabled = subs; });
    }

    // Subtitle size combo.
    const char* subSizes[] = {"small", "medium", "large", "xl"};
    int subIdx = 1;
    for (int i = 0; i < 4; ++i)
    {
        if (p.accessibility.subtitleSize == subSizes[i]) { subIdx = i; break; }
    }
    if (ImGui::Combo("Subtitle size", &subIdx, subSizes, 4))
    {
        const std::string s = subSizes[subIdx];
        m_editor->mutate([s](Settings& set) { set.accessibility.subtitleSize = s; });
    }

    // Color-vision filter combo.
    const char* cvFilters[] = {"none", "protanopia", "deuteranopia", "tritanopia"};
    int cvIdx = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (p.accessibility.colorVisionFilter == cvFilters[i]) { cvIdx = i; break; }
    }
    if (ImGui::Combo("Color vision filter", &cvIdx, cvFilters, 4))
    {
        const std::string s = cvFilters[cvIdx];
        m_editor->mutate(
            [s](Settings& set) { set.accessibility.colorVisionFilter = s; });
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Post-process accessibility");
    ImGui::Separator();

    bool dof = p.accessibility.postProcess.depthOfFieldEnabled;
    if (ImGui::Checkbox("Depth of field", &dof))
    {
        m_editor->mutate([dof](Settings& s) { s.accessibility.postProcess.depthOfFieldEnabled = dof; });
    }
    bool mb = p.accessibility.postProcess.motionBlurEnabled;
    if (ImGui::Checkbox("Motion blur", &mb))
    {
        m_editor->mutate([mb](Settings& s) { s.accessibility.postProcess.motionBlurEnabled = mb; });
    }
    bool fog = p.accessibility.postProcess.fogEnabled;
    if (ImGui::Checkbox("Fog", &fog))
    {
        m_editor->mutate([fog](Settings& s) { s.accessibility.postProcess.fogEnabled = fog; });
    }
    float fogScale = p.accessibility.postProcess.fogIntensityScale;
    if (ImGui::SliderFloat("Fog intensity", &fogScale, 0.0f, 1.0f, "%.2f"))
    {
        m_editor->mutate([fogScale](Settings& s) { s.accessibility.postProcess.fogIntensityScale = fogScale; });
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Photosensitive safety");
    ImGui::Separator();

    bool pe = p.accessibility.photosensitiveSafety.enabled;
    if (ImGui::Checkbox("Enable photosensitive safe mode", &pe))
    {
        m_editor->mutate([pe](Settings& s) { s.accessibility.photosensitiveSafety.enabled = pe; });
    }
    if (pe)
    {
        float flash = p.accessibility.photosensitiveSafety.maxFlashAlpha;
        if (ImGui::SliderFloat("Max flash alpha", &flash, 0.0f, 1.0f, "%.2f"))
        {
            m_editor->mutate([flash](Settings& s) { s.accessibility.photosensitiveSafety.maxFlashAlpha = flash; });
        }
        float shake = p.accessibility.photosensitiveSafety.shakeAmplitudeScale;
        if (ImGui::SliderFloat("Shake amplitude scale", &shake, 0.0f, 1.0f, "%.2f"))
        {
            m_editor->mutate([shake](Settings& s) { s.accessibility.photosensitiveSafety.shakeAmplitudeScale = shake; });
        }
        float strobe = p.accessibility.photosensitiveSafety.maxStrobeHz;
        if (ImGui::SliderFloat("Max strobe Hz", &strobe, 0.0f, 10.0f, "%.2f"))
        {
            m_editor->mutate([strobe](Settings& s) { s.accessibility.photosensitiveSafety.maxStrobeHz = strobe; });
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Restore accessibility defaults"))
    {
        m_editor->restoreAccessibilityDefaults();
    }
}

void SettingsEditorPanel::drawFooter()
{
    ImGui::Separator();

    const bool dirty = m_editor->isDirty();
    if (dirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                           "Unsaved changes.");
    }
    else
    {
        ImGui::TextDisabled("All changes saved.");
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 360.0f);
    if (ImGui::Button("Restore All Defaults", ImVec2(150.0f, 0.0f)))
    {
        m_editor->restoreAllDefaults();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Revert", ImVec2(90.0f, 0.0f)))
    {
        m_editor->revert();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply", ImVec2(90.0f, 0.0f)))
    {
        SaveStatus s = m_editor->apply(m_settingsPath);
        if (s != SaveStatus::Ok)
        {
            Logger::warning("SettingsEditorPanel: Apply failed to save.");
        }
    }
    ImGui::EndDisabled();
}

} // namespace Vestige

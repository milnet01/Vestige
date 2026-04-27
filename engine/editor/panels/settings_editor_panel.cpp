// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file settings_editor_panel.cpp
/// @brief Phase 10 slice 13.5b — ImGui editor panel implementation.
#include "editor/panels/settings_editor_panel.h"

#include "core/logger.h"
#include "core/settings_editor.h"
#include "input/input_bindings.h"

#include <imgui.h>

// GLFW key codes — matching the wire format used in
// `InputBinding::code`. We include the GLFW header only for the
// constant values; we do not call any GLFW functions from this
// file (capture uses ImGui's backend-agnostic key state).
#include <GLFW/glfw3.h>

#include <cstddef>
#include <string>
#include <utility>

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

/// @brief Minimal ImGuiKey → GLFW_KEY_* mapping for rebind capture.
///
/// Covers letters, digits, F-row, arrows, modifiers, and a few
/// specials. Returns -1 for keys we don't have a GLFW equivalent
/// for. Extending this table is safe — any new entry just makes
/// more keys re-bindable.
int imguiKeyToGlfwKey(ImGuiKey k)
{
    switch (k)
    {
        case ImGuiKey_A: return GLFW_KEY_A; case ImGuiKey_B: return GLFW_KEY_B;
        case ImGuiKey_C: return GLFW_KEY_C; case ImGuiKey_D: return GLFW_KEY_D;
        case ImGuiKey_E: return GLFW_KEY_E; case ImGuiKey_F: return GLFW_KEY_F;
        case ImGuiKey_G: return GLFW_KEY_G; case ImGuiKey_H: return GLFW_KEY_H;
        case ImGuiKey_I: return GLFW_KEY_I; case ImGuiKey_J: return GLFW_KEY_J;
        case ImGuiKey_K: return GLFW_KEY_K; case ImGuiKey_L: return GLFW_KEY_L;
        case ImGuiKey_M: return GLFW_KEY_M; case ImGuiKey_N: return GLFW_KEY_N;
        case ImGuiKey_O: return GLFW_KEY_O; case ImGuiKey_P: return GLFW_KEY_P;
        case ImGuiKey_Q: return GLFW_KEY_Q; case ImGuiKey_R: return GLFW_KEY_R;
        case ImGuiKey_S: return GLFW_KEY_S; case ImGuiKey_T: return GLFW_KEY_T;
        case ImGuiKey_U: return GLFW_KEY_U; case ImGuiKey_V: return GLFW_KEY_V;
        case ImGuiKey_W: return GLFW_KEY_W; case ImGuiKey_X: return GLFW_KEY_X;
        case ImGuiKey_Y: return GLFW_KEY_Y; case ImGuiKey_Z: return GLFW_KEY_Z;

        case ImGuiKey_0: return GLFW_KEY_0; case ImGuiKey_1: return GLFW_KEY_1;
        case ImGuiKey_2: return GLFW_KEY_2; case ImGuiKey_3: return GLFW_KEY_3;
        case ImGuiKey_4: return GLFW_KEY_4; case ImGuiKey_5: return GLFW_KEY_5;
        case ImGuiKey_6: return GLFW_KEY_6; case ImGuiKey_7: return GLFW_KEY_7;
        case ImGuiKey_8: return GLFW_KEY_8; case ImGuiKey_9: return GLFW_KEY_9;

        case ImGuiKey_F1:  return GLFW_KEY_F1;  case ImGuiKey_F2:  return GLFW_KEY_F2;
        case ImGuiKey_F3:  return GLFW_KEY_F3;  case ImGuiKey_F4:  return GLFW_KEY_F4;
        case ImGuiKey_F5:  return GLFW_KEY_F5;  case ImGuiKey_F6:  return GLFW_KEY_F6;
        case ImGuiKey_F7:  return GLFW_KEY_F7;  case ImGuiKey_F8:  return GLFW_KEY_F8;
        case ImGuiKey_F9:  return GLFW_KEY_F9;  case ImGuiKey_F10: return GLFW_KEY_F10;
        case ImGuiKey_F11: return GLFW_KEY_F11; case ImGuiKey_F12: return GLFW_KEY_F12;

        case ImGuiKey_Space:      return GLFW_KEY_SPACE;
        case ImGuiKey_Enter:      return GLFW_KEY_ENTER;
        case ImGuiKey_Tab:        return GLFW_KEY_TAB;
        case ImGuiKey_Backspace:  return GLFW_KEY_BACKSPACE;
        case ImGuiKey_Escape:     return GLFW_KEY_ESCAPE;
        case ImGuiKey_LeftArrow:  return GLFW_KEY_LEFT;
        case ImGuiKey_RightArrow: return GLFW_KEY_RIGHT;
        case ImGuiKey_UpArrow:    return GLFW_KEY_UP;
        case ImGuiKey_DownArrow:  return GLFW_KEY_DOWN;
        case ImGuiKey_LeftShift:  return GLFW_KEY_LEFT_SHIFT;
        case ImGuiKey_RightShift: return GLFW_KEY_RIGHT_SHIFT;
        case ImGuiKey_LeftCtrl:   return GLFW_KEY_LEFT_CONTROL;
        case ImGuiKey_RightCtrl:  return GLFW_KEY_RIGHT_CONTROL;
        case ImGuiKey_LeftAlt:    return GLFW_KEY_LEFT_ALT;
        case ImGuiKey_RightAlt:   return GLFW_KEY_RIGHT_ALT;

        default: return -1;
    }
}

/// @brief Iterate the subset of ImGuiKeys we support; return the
///        first one just-pressed this frame, or `ImGuiKey_None`.
ImGuiKey firstJustPressedKey()
{
    static const ImGuiKey kSupported[] = {
        ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E,
        ImGuiKey_F, ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J,
        ImGuiKey_K, ImGuiKey_L, ImGuiKey_M, ImGuiKey_N, ImGuiKey_O,
        ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R, ImGuiKey_S, ImGuiKey_T,
        ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X, ImGuiKey_Y,
        ImGuiKey_Z,
        ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
        ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
        ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4,
        ImGuiKey_F5, ImGuiKey_F6, ImGuiKey_F7, ImGuiKey_F8,
        ImGuiKey_F9, ImGuiKey_F10, ImGuiKey_F11, ImGuiKey_F12,
        ImGuiKey_Space, ImGuiKey_Enter, ImGuiKey_Tab,
        ImGuiKey_Backspace, ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
        ImGuiKey_UpArrow, ImGuiKey_DownArrow,
        ImGuiKey_LeftShift, ImGuiKey_RightShift,
        ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl,
        ImGuiKey_LeftAlt, ImGuiKey_RightAlt,
    };
    for (ImGuiKey k : kSupported)
    {
        if (ImGui::IsKeyPressed(k, /*repeat=*/false))
        {
            return k;
        }
    }
    return ImGuiKey_None;
}

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
        // action. Click-to-rebind: each cell renders a button whose
        // label is the current binding; clicking enters capture mode.
        if (ImGui::BeginTable("##keybinds", 4,
                               ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("Primary");
            ImGui::TableSetupColumn("Secondary");
            ImGui::TableSetupColumn("Gamepad");
            ImGui::TableHeadersRow();

            const auto slotButton = [&](const InputAction& a,
                                         const InputBinding& b,
                                         SlotIndex slot,
                                         const char* uniqueTag)
            {
                std::string lbl = bindingDisplayLabel(b);
                if (lbl.empty()) lbl = "-";
                // ImGui needs a unique button id per cell — synthesise
                // one from action id + slot tag.
                const std::string id = "##rebind_" + a.id + "_" + uniqueTag;
                if (ImGui::Button((lbl + id).c_str(),
                                  ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                {
                    m_captureActionId = a.id;
                    m_captureSlot     = slot;
                    ImGui::OpenPopup("##rebind_modal");
                }
            };

            for (const InputAction& a : m_inputMap->actions())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(a.label.empty() ? a.id.c_str()
                                                       : a.label.c_str());
                ImGui::TableSetColumnIndex(1);
                slotButton(a, a.primary,   SlotIndex::Primary,   "p");
                ImGui::TableSetColumnIndex(2);
                slotButton(a, a.secondary, SlotIndex::Secondary, "s");
                ImGui::TableSetColumnIndex(3);
                slotButton(a, a.gamepad,   SlotIndex::Gamepad,   "g");
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled(
            "Click a binding cell to rebind it. Esc cancels; "
            "any other key overwrites the slot.");

        // Modal renders outside the table so ImGui's popup stack is
        // happy. The modal runs every frame while capturing; a
        // successful capture clears m_captureActionId and the modal
        // closes itself.
        drawRebindModal();
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
    // AUDIT W3 — surface the "awaiting consumer" status so users don't
    // expect the toggle to do something today.
    ImGui::TextDisabled("    (effect not yet shipped — preference is saved)");
    bool mb = p.accessibility.postProcess.motionBlurEnabled;
    if (ImGui::Checkbox("Motion blur", &mb))
    {
        m_editor->mutate([mb](Settings& s) { s.accessibility.postProcess.motionBlurEnabled = mb; });
    }
    ImGui::TextDisabled("    (effect not yet shipped — preference is saved)");
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
        if (ImGui::SliderFloat("Max strobe Hz", &strobe, 0.0f,
                               SAFE_MODE_STROBE_HZ_SLIDER_MAX, "%.2f"))
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

void SettingsEditorPanel::drawRebindModal()
{
    if (!ImGui::BeginPopupModal("##rebind_modal", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoTitleBar))
    {
        return;
    }

    ImGui::Text("Rebinding: %s", m_captureActionId.c_str());
    const char* slotName =
        (m_captureSlot == SlotIndex::Primary)   ? "Primary"
      : (m_captureSlot == SlotIndex::Secondary) ? "Secondary"
                                                : "Gamepad";
    ImGui::Text("Slot: %s", slotName);
    ImGui::Separator();
    ImGui::TextWrapped(
        "Press any key or mouse button to bind. Esc to cancel. "
        "Delete to clear the slot.");

    bool finished = false;

    // Esc → cancel (leave binding unchanged).
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        finished = true;
    }
    // Delete → explicit unbind.
    else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        if (m_editor && m_inputMap)
        {
            const std::string id   = m_captureActionId;
            const SlotIndex   slot = m_captureSlot;
            m_editor->mutate([](Settings&) { /* no Settings field flip — live action map */ });
            switch (slot)
            {
                case SlotIndex::Primary:
                    m_inputMap->setPrimary(id, InputBinding::none());   break;
                case SlotIndex::Secondary:
                    m_inputMap->setSecondary(id, InputBinding::none()); break;
                case SlotIndex::Gamepad:
                    m_inputMap->setGamepad(id, InputBinding::none());   break;
            }
        }
        finished = true;
    }
    else
    {
        // Mouse capture — left / right / middle.
        InputBinding captured = InputBinding::none();
        if      (ImGui::IsMouseClicked(0)) captured = InputBinding::mouse(GLFW_MOUSE_BUTTON_LEFT);
        else if (ImGui::IsMouseClicked(1)) captured = InputBinding::mouse(GLFW_MOUSE_BUTTON_RIGHT);
        else if (ImGui::IsMouseClicked(2)) captured = InputBinding::mouse(GLFW_MOUSE_BUTTON_MIDDLE);

        if (!captured.isBound())
        {
            // Keyboard capture — first supported key just pressed.
            const ImGuiKey k = firstJustPressedKey();
            if (k != ImGuiKey_None)
            {
                const int glfwCode = imguiKeyToGlfwKey(k);
                if (glfwCode >= 0)
                {
                    captured = InputBinding::key(glfwCode);
                }
            }
        }

        if (captured.isBound() && m_editor && m_inputMap)
        {
            const std::string id   = m_captureActionId;
            const SlotIndex   slot = m_captureSlot;
            // Conflict display — show names of actions already using
            // this binding. We don't block assignment; the user can
            // intentionally double-bind a control (rare but valid).
            const auto conflicts = m_inputMap->findConflicts(captured, id);
            if (!conflicts.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                    "Note: this binding is also assigned to other actions.");
            }
            switch (slot)
            {
                case SlotIndex::Primary:
                    m_inputMap->setPrimary(id, captured);   break;
                case SlotIndex::Secondary:
                    m_inputMap->setSecondary(id, captured); break;
                case SlotIndex::Gamepad:
                    m_inputMap->setGamepad(id, captured);   break;
            }
            // Trigger a mutate so live-apply fires (the editor doesn't
            // need to know a binding changed, but it will re-apply the
            // (unchanged) Settings struct and the input map change is
            // already reflected in the map itself).
            if (m_editor)
            {
                m_editor->mutate([](Settings&) {});
            }
            finished = true;
        }
    }

    if (finished)
    {
        m_captureActionId.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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

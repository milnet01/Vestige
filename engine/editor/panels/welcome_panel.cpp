// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file welcome_panel.cpp
/// @brief Welcome panel implementation.
#include "editor/panels/welcome_panel.h"

#include "core/logger.h"
#include "utils/atomic_write.h"

#include <imgui.h>

#include <filesystem>

namespace Vestige
{

void WelcomePanel::initialize(const std::string& configDir)
{
    m_configDir = configDir;
    m_flagPath = configDir + "/welcome_shown";

    // Phase 10.5 slice 14.4: auto-open removed. First-run onboarding
    // is now owned by FirstRunWizard; WelcomePanel remains as a
    // keyboard-shortcut reference reachable via Help → Welcome Screen
    // (Q3 resolution in docs/phases/phase_10_5_first_run_wizard_design.md).
    //
    // The legacy flag file is no longer read here — its role moved
    // to Settings::loadFromDisk, which promotes its presence into
    // `onboarding.hasCompletedFirstRun` and deletes it (slice 14.1).
    // Retained as state so the Help → Welcome Screen action stays
    // idempotent across re-opens.
    m_shownBefore = std::filesystem::exists(m_flagPath);
}

void WelcomePanel::draw()
{
    if (!m_open)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Welcome to Vestige", &m_open))
    {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Vestige 3D Engine");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextWrapped(
        "Vestige is a 3D exploration engine for creating first-person "
        "architectural walkthroughs. Use the editor to build, texture, "
        "light, and explore your scenes.");

    ImGui::Spacing();

    drawModesSection();
    drawShortcutsSection();
    drawToolsSection();

    ImGui::Spacing();
    ImGui::Separator();

    // Don't show again checkbox
    bool dontShow = m_shownBefore;
    if (ImGui::Checkbox("Don't show on startup", &dontShow))
    {
        if (dontShow && !m_shownBefore)
        {
            markAsShown();
        }
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80.0f);
    if (ImGui::Button("Close", ImVec2(80, 0)))
    {
        m_open = false;
        if (!m_shownBefore)
        {
            markAsShown();
        }
    }

    ImGui::End();
}

void WelcomePanel::drawModesSection()
{
    if (ImGui::CollapsingHeader("Editor Modes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BulletText("EDIT mode: Orbit camera, place objects, edit properties");
        ImGui::BulletText("PLAY mode: First-person walkthrough (press Escape to toggle)");
        ImGui::Spacing();
    }
}

void WelcomePanel::drawShortcutsSection()
{
    if (ImGui::CollapsingHeader("Keyboard Shortcuts", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Columns(2, "shortcuts", false);
        ImGui::SetColumnWidth(0, 200.0f);

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Navigation");
        ImGui::Text("Alt + Drag");      ImGui::NextColumn(); ImGui::Text("Orbit camera"); ImGui::NextColumn();
        ImGui::Text("Middle Drag");      ImGui::NextColumn(); ImGui::Text("Pan camera"); ImGui::NextColumn();
        ImGui::Text("Scroll");           ImGui::NextColumn(); ImGui::Text("Zoom"); ImGui::NextColumn();
        ImGui::Text("F");               ImGui::NextColumn(); ImGui::Text("Focus on selection"); ImGui::NextColumn();
        ImGui::Text("Numpad 1/3/7");    ImGui::NextColumn(); ImGui::Text("Front/Right/Top view"); ImGui::NextColumn();

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Transform");
        ImGui::Text("W");               ImGui::NextColumn(); ImGui::Text("Translate mode"); ImGui::NextColumn();
        ImGui::Text("E");               ImGui::NextColumn(); ImGui::Text("Rotate mode"); ImGui::NextColumn();
        ImGui::Text("R");               ImGui::NextColumn(); ImGui::Text("Scale mode"); ImGui::NextColumn();
        ImGui::Text("L");               ImGui::NextColumn(); ImGui::Text("Toggle local/world"); ImGui::NextColumn();
        ImGui::Text("Ctrl + Hold");     ImGui::NextColumn(); ImGui::Text("Snap to grid"); ImGui::NextColumn();

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Edit");
        ImGui::Text("Ctrl+D");          ImGui::NextColumn(); ImGui::Text("Duplicate"); ImGui::NextColumn();
        ImGui::Text("Delete");           ImGui::NextColumn(); ImGui::Text("Delete selected"); ImGui::NextColumn();
        ImGui::Text("Ctrl+Z");          ImGui::NextColumn(); ImGui::Text("Undo"); ImGui::NextColumn();
        ImGui::Text("Ctrl+Y");          ImGui::NextColumn(); ImGui::Text("Redo"); ImGui::NextColumn();
        ImGui::Text("Ctrl+G");          ImGui::NextColumn(); ImGui::Text("Group selected"); ImGui::NextColumn();
        ImGui::Text("H");               ImGui::NextColumn(); ImGui::Text("Toggle visibility"); ImGui::NextColumn();
        ImGui::Text("G");               ImGui::NextColumn(); ImGui::Text("Toggle grid"); ImGui::NextColumn();
        ImGui::Text("F11");             ImGui::NextColumn(); ImGui::Text("Screenshot"); ImGui::NextColumn();
        ImGui::Text("Escape");          ImGui::NextColumn(); ImGui::Text("Toggle Edit/Play"); ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::Spacing();
    }
}

void WelcomePanel::drawToolsSection()
{
    if (ImGui::CollapsingHeader("Tools Overview"))
    {
        ImGui::BulletText("Hierarchy Panel: View and organize the entity tree");
        ImGui::BulletText("Inspector Panel: Edit selected entity properties");
        ImGui::BulletText("Asset Browser: Browse textures, models, prefabs");
        ImGui::BulletText("Environment Panel: Paint foliage, scatter, trees");
        ImGui::BulletText("Terrain Panel: Sculpt and paint terrain");
        ImGui::BulletText("Performance Panel: Monitor FPS, draw calls, memory");
        ImGui::Spacing();
    }
}

void WelcomePanel::markAsShown()
{
    m_shownBefore = true;

    // Phase 10.9 Slice 12 Ed4 — atomic write so the flag is either
    // entirely written or not present (the legacy ofstream path could
    // leave an empty file if the editor was killed mid-flush, which
    // would still trigger the "exists()" check on next launch and
    // suppress the welcome panel for a user who never actually saw
    // it). AtomicWrite creates parent directories itself.
    const AtomicWrite::Status s = AtomicWrite::writeFile(m_flagPath, "1");
    if (s != AtomicWrite::Status::Ok)
    {
        Logger::warning(std::string("WelcomePanel: ")
                        + AtomicWrite::describe(s)
                        + " for " + m_flagPath);
    }
}

} // namespace Vestige

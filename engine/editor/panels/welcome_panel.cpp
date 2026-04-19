// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file welcome_panel.cpp
/// @brief Welcome panel implementation.
#include "editor/panels/welcome_panel.h"

#include <imgui.h>

#include <filesystem>
#include <fstream>

namespace Vestige
{

void WelcomePanel::initialize(const std::string& configDir)
{
    m_configDir = configDir;
    m_flagPath = configDir + "/welcome_shown";

    // Check if we've shown the welcome screen before
    m_shownBefore = std::filesystem::exists(m_flagPath);

    if (!m_shownBefore)
    {
        m_open = true;
    }
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

    // Create config directory if needed
    std::error_code ec;
    std::filesystem::create_directories(m_configDir, ec);

    // Write flag file
    std::ofstream file(m_flagPath);
    if (file.is_open())
    {
        file << "1";
    }
}

} // namespace Vestige

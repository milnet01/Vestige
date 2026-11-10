// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_hierarchy_delete_key.cpp
/// @brief 3D_E-0874 lane 14 — the hierarchy's Delete key deletes the
///        selection only when no text field has the keyboard. Pressing
///        Delete while typing in the search box must edit the search text,
///        not delete the selected entity.
///
/// Drives the real HierarchyPanel headlessly, as test_inspector_transform_undo
/// does: widgets are found by hovering and asking ImGui what is under the mouse.
#include <gtest/gtest.h>

#include "editor/command_history.h"
#include "editor/panels/hierarchy_panel.h"
#include "editor/selection.h"
#include "scene/entity.h"
#include "scene/scene.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>

namespace Vestige::Test
{

class HierarchyDeleteKeyTest : public ::testing::Test
{
protected:
    Scene m_scene{"Delete"};
    Selection m_selection;
    CommandHistory m_history;
    HierarchyPanel m_panel;
    uint32_t m_entityId = 0;

    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(640.0f, 480.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        m_entityId = m_scene.createEntity("Box")->getId();
        m_selection.select(m_entityId);
        m_panel.setCommandHistory(&m_history);
    }

    void TearDown() override { ImGui::DestroyContext(); }

    void frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 400.0f));
        ImGui::Begin("hierarchy");
        m_panel.draw(&m_scene, m_selection);
        ImGui::End();
        ImGui::Render();
    }

    void idle(int frames)
    {
        for (int i = 0; i < frames; ++i) frame();
    }

    /// A point over the widget, found by hovering only.
    std::optional<ImVec2> locate(const char* label)
    {
        const ImGuiID id = ImGui::FindWindowByName("hierarchy")->GetID(label);
        for (float y = 2.0f; y < 120.0f; y += 3.0f)
        {
            for (float x = 2.0f; x < 398.0f; x += 8.0f)
            {
                ImGui::GetIO().AddMousePosEvent(x, y);
                frame();
                if (ImGui::GetHoveredID() == id) return ImVec2(x, y);
            }
        }
        return std::nullopt;
    }

    void click(ImVec2 at)
    {
        ImGui::GetIO().AddMousePosEvent(at.x, at.y);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        idle(60);  // past the double-click window
    }

    void pressDelete()
    {
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, true);
        frame();
        ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, false);
        frame();
    }
};

// Control: with the panel focused and no text field active, Delete deletes.
// This proves the harness reaches the hotkey, so the test below is meaningful.
TEST_F(HierarchyDeleteKeyTest, DeleteRemovesTheSelectionWhenNoFieldIsActive)
{
    frame();
    click(ImVec2(200.0f, 380.0f));  // empty panel area: focuses the window
    m_selection.select(m_entityId);  // that click also deselects, by design
    ASSERT_FALSE(ImGui::GetIO().WantTextInput);

    pressDelete();
    EXPECT_EQ(m_scene.findEntityById(m_entityId), nullptr);
}

TEST_F(HierarchyDeleteKeyTest, DeleteWhileTypingInSearchKeepsTheSelection)
{
    frame();
    const auto search = locate("##HierarchySearch");
    ASSERT_TRUE(search.has_value()) << "search box not found";

    click(*search);
    ASSERT_TRUE(ImGui::GetIO().WantTextInput) << "search box did not take the keyboard";
    ImGui::GetIO().AddInputCharacter('b');
    frame();

    pressDelete();
    EXPECT_NE(m_scene.findEntityById(m_entityId), nullptr)
        << "Delete in the search box deleted the selected entity";
    EXPECT_FALSE(m_history.canUndo());
}

} // namespace Vestige::Test

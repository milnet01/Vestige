// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_inspector_transform_undo.cpp
/// @brief 3D_E-0872 — the inspector's Transform section records undo like
///        every other section: a click that edits nothing leaves no open
///        entry behind. 3D_E-0874 lane 14: the Material section likewise
///        records nothing for a click that edits nothing.
///
/// Drives the real InspectorPanel headlessly. Widgets are found by hovering
/// a grid and asking ImGui which item is under the mouse, so no stray click
/// lands anywhere.
#include <gtest/gtest.h>

#include "editor/command_history.h"
#include "editor/panels/inspector_panel.h"
#include "editor/selection.h"
#include "renderer/material.h"
#include "resource/resource_manager.h"
#include "scene/entity.h"
#include "scene/mesh_renderer.h"
#include "scene/scene.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>

namespace Vestige::Test
{

class InspectorTransformUndoTest : public ::testing::Test
{
protected:
    Scene m_scene{"Undo"};
    Selection m_selection;
    CommandHistory m_history;
    InspectorPanel m_panel;
    Entity* m_entity = nullptr;
    float m_windowHeight = 400.0f;  ///< Taller for a section far down the panel.

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

        m_entity = m_scene.createEntity("Box");
        m_entity->transform.position = glm::vec3(3.0f, 4.0f, 5.0f);
        m_selection.select(m_entity->getId());
        m_panel.setCommandHistory(&m_history);
    }

    void TearDown() override { ImGui::DestroyContext(); }

    void frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, m_windowHeight));
        ImGui::Begin("inspector");
        m_panel.draw(&m_scene, m_selection);
        ImGui::End();
        ImGui::Render();
    }

    void idle(int frames)
    {
        for (int i = 0; i < frames; ++i) frame();
    }

    /// The ID drawVec3Control gives a widget in its row.
    static ImGuiID widgetId(const char* row, const char* widget)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("inspector");
        const ImGuiID rowId = ImHashStr(row, 0, window->ID);
        return ImHashStr(widget, 0, rowId);
    }

    /// A point over the widget, found by hovering only.
    std::optional<ImVec2> locate(ImGuiID id)
    {
        for (float y = 2.0f; y < 300.0f; y += 3.0f)
        {
            for (float x = 2.0f; x < 398.0f; x += 4.0f)
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
};

TEST_F(InspectorTransformUndoTest, ResetAfterAClickThatEditedNothingIsUndoable)
{
    frame();
    const auto field = locate(widgetId("Position", "##X"));
    const auto reset = locate(widgetId("Position", "X"));
    ASSERT_TRUE(field.has_value()) << "Position X field not found";
    ASSERT_TRUE(reset.has_value()) << "Position X reset button not found";

    click(*field);  // focus the field, change nothing
    ASSERT_FALSE(m_history.canUndo());
    ASSERT_FLOAT_EQ(m_entity->transform.position.x, 3.0f);

    click(*reset);  // X back to 0
    ASSERT_FLOAT_EQ(m_entity->transform.position.x, 0.0f);
    ASSERT_TRUE(m_history.canUndo()) << "reset was not recorded";

    m_history.undo();
    EXPECT_FLOAT_EQ(m_entity->transform.position.x, 3.0f);
    EXPECT_FALSE(m_history.canUndo()) << "more than one entry recorded";
}

TEST_F(InspectorTransformUndoTest, DragIsOneStepBackToTheStart)
{
    frame();
    const auto field = locate(widgetId("Position", "##X"));
    ASSERT_TRUE(field.has_value()) << "Position X field not found";

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(field->x, field->y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    for (int i = 1; i <= 10; ++i)
    {
        io.AddMousePosEvent(field->x + 5.0f * static_cast<float>(i), field->y);
        frame();
    }
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    ASSERT_GT(m_entity->transform.position.x, 3.0f);

    m_history.undo();
    EXPECT_FLOAT_EQ(m_entity->transform.position.x, 3.0f);
    EXPECT_FALSE(m_history.canUndo()) << "the drag took more than one entry";
}

// The Material section records an undo step when a widget inside it is
// released. Clicking a slider without moving it released one too, so it left
// a step that changed nothing: the next Undo seemed to do nothing.
TEST_F(InspectorTransformUndoTest, MaterialClickThatEditedNothingRecordsNothing)
{
    auto material = std::make_shared<Material>();
    material->setType(MaterialType::PBR);
    material->setMetallic(0.25f);
    m_entity->addComponent<MeshRenderer>(nullptr, material);
    ResourceManager resources;  // what a material's saved form is read through
    m_panel.setResourceManager(&resources);
    m_windowHeight = 1600.0f;
    ImGui::GetIO().DisplaySize = ImVec2(640.0f, m_windowHeight);

    frame();
    ImGuiWindow* window = ImGui::FindWindowByName("inspector");
    const ImGuiID metallicId = ImHashStr("Metallic", 0, ImHashStr("Material", 0, window->ID));
    std::optional<ImVec2> slider;
    for (float y = 2.0f; y < m_windowHeight && !slider; y += 3.0f)
    {
        ImGui::GetIO().AddMousePosEvent(100.0f, y);
        frame();
        if (ImGui::GetHoveredID() == metallicId) slider = ImVec2(100.0f, y);
    }
    ASSERT_TRUE(slider.has_value()) << "Metallic slider not found";

    // Press and release on the spot. A slider jumps to the clicked value, so
    // first set the value to the one under the cursor, as a still click gives.
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(slider->x, slider->y);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    idle(60);
    m_history.clear();
    const float before = material->getMetallic();

    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    idle(60);
    ASSERT_FLOAT_EQ(material->getMetallic(), before) << "the click moved the value";
    EXPECT_FALSE(m_history.canUndo()) << "a click that changed nothing was recorded";

    // A real change is still recorded.
    io.AddMousePosEvent(slider->x + 60.0f, slider->y);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    idle(60);
    ASSERT_NE(material->getMetallic(), before);
    EXPECT_TRUE(m_history.canUndo()) << "a real change was not recorded";
}

} // namespace Vestige::Test

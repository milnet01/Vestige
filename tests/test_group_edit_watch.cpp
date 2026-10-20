// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_group_edit_watch.cpp
/// @brief 3D_E-0766 — the material inspector records a step only for edits
///        inside its own widgets.
///
/// Drives real ImGui headlessly, like test_edit_tracker_drag_undo.cpp: one
/// window holds a "Position" drag (a transform field) and, inside a
/// GroupEditWatch, a "Roughness" drag (a material field).

#include <gtest/gtest.h>
#include "editor/panels/edit_tracker.h"

#include <imgui.h>

namespace Vestige::Test
{

class GroupEditWatchTest : public ::testing::Test
{
protected:
    float m_position = 0.0f;
    float m_roughness = 0.5f;
    GroupEditWatch m_watch;
    int m_materialSteps = 0;
    ImVec2 m_positionCenter{0.0f, 0.0f};
    ImVec2 m_roughnessCenter{0.0f, 0.0f};

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
    }

    void TearDown() override { ImGui::DestroyContext(); }

    static ImVec2 itemCenter()
    {
        const ImVec2 lo = ImGui::GetItemRectMin();
        const ImVec2 hi = ImGui::GetItemRectMax();
        return {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
    }

    void frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
        ImGui::Begin("inspector");
        ImGui::DragFloat("Position", &m_position, 1.0f);
        m_positionCenter = itemCenter();
        m_watch.begin();
        ImGui::DragFloat("Roughness", &m_roughness, 0.01f);
        m_roughnessCenter = itemCenter();
        if (m_watch.end())
        {
            ++m_materialSteps;
        }
        ImGui::End();
        ImGui::Render();
    }

    /// Press on the widget at @a at, drag right, release.
    void drag(const ImVec2& at)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(at.x, at.y); frame();
        io.AddMouseButtonEvent(0, true); frame();
        for (int i = 1; i <= 10; ++i)
        {
            io.AddMousePosEvent(at.x + 5.0f * static_cast<float>(i), at.y);
            frame();
        }
        io.AddMouseButtonEvent(0, false); frame();
        frame();
    }
};

TEST_F(GroupEditWatchTest, EditOutsideTheGroupRecordsNoMaterialStep)
{
    frame();  // lay out
    drag(m_positionCenter);
    EXPECT_GT(m_position, 0.0f);  // the transform drag really happened
    EXPECT_EQ(m_materialSteps, 0);
}

TEST_F(GroupEditWatchTest, EditInsideTheGroupRecordsOneStep)
{
    frame();
    drag(m_roughnessCenter);
    EXPECT_GT(m_roughness, 0.5f);
    EXPECT_EQ(m_materialSteps, 1);
}

}  // namespace Vestige::Test

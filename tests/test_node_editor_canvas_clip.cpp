// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_node_editor_canvas_clip.cpp
/// @brief 3D_E-0761 — a node canvas that is the first thing drawn in its
///        child window is clipped to the canvas on screen.
///
/// imgui-node-editor v0.9.3 left that first draw command's clip rect in
/// canvas space, so the Script Editor's grid showed only where the canvas
/// overlapped the screen's top-left corner. Drives real ImGui headlessly,
/// like test_group_edit_watch.cpp, with the canvas wrapped in a child as
/// ScriptEditorPanel::drawCanvas does.

#include <gtest/gtest.h>
#include "editor/widgets/node_editor_widget.h"

#include <imgui.h>

#include <string>

namespace Vestige::Test
{

class NodeEditorCanvasClipTest : public ::testing::Test
{
protected:
    NodeEditorWidget m_widget;

    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1600.0f, 1000.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();
        m_widget.initialize();
    }

    void TearDown() override
    {
        m_widget.shutdown();
        ImGui::DestroyContext();
    }

    /// One frame of an empty canvas in a child of a window placed away from
    /// the screen origin. Returns the child's screen rect.
    ImVec4 frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(300.0f, 200.0f));
        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f));
        ImGui::Begin("Script Editor");
        ImGui::BeginChild("##script-canvas", ImVec2(0, 0), ImGuiChildFlags_Borders);
        const ImVec2 lo = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        if (m_widget.beginCanvas("ScriptEditorCanvas"))
        {
            m_widget.endCanvas();
        }
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
        return {lo.x, lo.y, lo.x + size.x, lo.y + size.y};
    }
};

TEST_F(NodeEditorCanvasClipTest, FirstCanvasCommandIsClippedToTheCanvasOnScreen)
{
    ImVec4 canvas{};
    for (int i = 0; i < 3; ++i)
    {
        canvas = frame();
    }

    const ImDrawList* childList = nullptr;
    for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists)
    {
        const std::string owner = list->_OwnerName != nullptr ? list->_OwnerName : "";
        if (owner.find("##script-canvas") != std::string::npos)
        {
            childList = list;
        }
    }
    ASSERT_NE(childList, nullptr);
    ASSERT_FALSE(childList->CmdBuffer.empty());

    // The first command holds the canvas background and grid. Its clip rect
    // must start where the canvas starts; v0.9.3 left it at (0,0).
    const ImVec4 clip = childList->CmdBuffer[0].ClipRect;
    EXPECT_GT(childList->CmdBuffer[0].ElemCount, 0u);
    EXPECT_NEAR(clip.x, canvas.x, 1.0f);
    EXPECT_NEAR(clip.y, canvas.y, 1.0f);
    EXPECT_NEAR(clip.z, canvas.z, 1.0f);
    EXPECT_NEAR(clip.w, canvas.w, 1.0f);
}

} // namespace Vestige::Test

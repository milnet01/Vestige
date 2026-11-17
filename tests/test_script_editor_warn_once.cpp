// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_script_editor_warn_once.cpp
/// @brief 3D_E-0874 lane 15 — a script connection the editor cannot draw
///        (a missing node, an unknown pin) is skipped with one warning, not
///        one warning per frame. Loading another script warns again.
///
/// Drives the real ScriptEditorPanel headlessly with an initialised node
/// editor, as test_node_editor_canvas_clip does.
#include <gtest/gtest.h>

#include "core/logger.h"
#include "editor/panels/script_editor_panel.h"
#include "test_helpers.h"

#include <imgui.h>

namespace Vestige::Test
{

using Vestige::Testing::countWarningsContaining;

class ScriptEditorWarnOnceTest : public ::testing::Test
{
protected:
    ScriptEditorPanel m_panel;

    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1200.0f, 800.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        m_panel.initialize();
        m_panel.setOpen(true);
        Logger::clearEntries();
    }

    void TearDown() override
    {
        m_panel.shutdown();
        ImGui::DestroyContext();
    }

    void frame()
    {
        ImGui::NewFrame();
        m_panel.draw();
        ImGui::Render();
    }

    void addDanglingConnection()
    {
        ScriptConnection conn;
        conn.id = 7;
        conn.sourceNode = 100;  // no such node
        conn.sourcePin = "out";
        conn.targetNode = 200;  // no such node
        conn.targetPin = "in";
        m_panel.graph().connections.push_back(conn);
    }
};

TEST_F(ScriptEditorWarnOnceTest, AnUndrawableConnectionWarnsOnce)
{
    addDanglingConnection();
    for (int i = 0; i < 5; ++i) frame();

    EXPECT_EQ(countWarningsContaining("Connection 7 references missing node"), 1u);
}

TEST_F(ScriptEditorWarnOnceTest, ANewScriptWarnsAgain)
{
    addDanglingConnection();
    for (int i = 0; i < 3; ++i) frame();

    m_panel.newGraph();
    addDanglingConnection();
    for (int i = 0; i < 3; ++i) frame();

    EXPECT_EQ(countWarningsContaining("Connection 7 references missing node"), 2u);
}

} // namespace Vestige::Test

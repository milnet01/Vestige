// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_history_panel_scroll.cpp
/// @brief 3D_E-0874 lane 15 — the History panel scrolls to the current
///        entry only when the current entry changes. Before the fix it
///        scrolled back every frame, so the user could not scroll away.
///
/// Drives the real HistoryPanel headlessly, as test_hierarchy_delete_key
/// does, and scrolls with the mouse wheel.

#include "editor/command_history.h"
#include "editor/commands/editor_command.h"
#include "editor/panels/history_panel.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <string>

namespace Vestige::Test
{
namespace
{

class NoopCommand : public EditorCommand
{
public:
    void execute() override {}
    void undo() override {}
    std::string getDescription() const override { return "Noop"; }
};

} // namespace

class HistoryPanelScrollTest : public ::testing::Test
{
protected:
    CommandHistory m_history;
    HistoryPanel m_panel;

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

        for (int i = 0; i < 60; ++i)
        {
            m_history.execute(std::make_unique<NoopCommand>());
        }
    }

    void TearDown() override { ImGui::DestroyContext(); }

    void frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
        ImGui::Begin("History");
        m_panel.draw(m_history);
        ImGui::End();
        ImGui::Render();
    }

    void idle(int frames)
    {
        for (int i = 0; i < frames; ++i) frame();
    }

    static float scrollY() { return ImGui::FindWindowByName("History")->Scroll.y; }
};

TEST_F(HistoryPanelScrollTest, ScrollingAwayFromTheCurrentEntryStays)
{
    idle(3);
    ASSERT_GT(scrollY(), 0.0f) << "the panel did not scroll to the last entry";

    ImGui::GetIO().AddMousePosEvent(150.0f, 100.0f);
    frame();
    for (int i = 0; i < 20; ++i)
    {
        ImGui::GetIO().AddMouseWheelEvent(0.0f, 10.0f);
        frame();
    }
    idle(5);
    EXPECT_EQ(scrollY(), 0.0f) << "the panel snapped back to the current entry";
}

TEST_F(HistoryPanelScrollTest, ChangingTheCurrentEntryScrollsToIt)
{
    idle(3);
    ImGui::GetIO().AddMousePosEvent(150.0f, 100.0f);
    frame();
    for (int i = 0; i < 20; ++i)
    {
        ImGui::GetIO().AddMouseWheelEvent(0.0f, 10.0f);
        frame();
    }
    idle(2);

    for (int i = 0; i < 20; ++i)
    {
        m_history.undo();  // current entry 60 -> 40, off screen below
    }
    idle(3);
    EXPECT_GT(scrollY(), 0.0f) << "an undo did not bring the current entry into view";
}

} // namespace Vestige::Test

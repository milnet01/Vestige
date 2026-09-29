// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_edit_tracker_drag_undo.cpp
/// @brief 3D_E-0722 — an inspector drag records its pre-drag value for undo.
///
/// Drives real ImGui headlessly: a simulated mouse drags one DragFloat, and
/// the block follows the inspector's pattern (snapshot at the top of the
/// frame, EditTracker per widget, commit through DragUndo).

#include <gtest/gtest.h>
#include "editor/panels/edit_tracker.h"

#include <imgui.h>

#include <utility>
#include <vector>

namespace Vestige::Test
{

class DragUndoTest : public ::testing::Test
{
protected:
    float m_value = 1.0f;
    DragUndo<float> m_undo;
    std::vector<std::pair<float, float>> m_entries;  // {before, after}
    ImVec2 m_center{0.0f, 0.0f};
    bool m_checkbox = false;  ///< Draw a checkbox instead of the slider.
    bool m_flag = false;
    DragUndo<bool> m_flagUndo;
    std::vector<std::pair<bool, bool>> m_flagEntries;

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

    /// One frame of an inspector block holding a single slider.
    void frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
        ImGui::Begin("inspector");
        const float before = m_value;
        const bool flagBefore = m_flag;
        EditTracker tr;
        if (m_checkbox)
            tr.track(ImGui::Checkbox("Looping", &m_flag));
        else
            tr.track(ImGui::DragFloat("Rate", &m_value, 1.0f));
        const ImVec2 lo = ImGui::GetItemRectMin();
        const ImVec2 hi = ImGui::GetItemRectMax();
        m_center = ImVec2((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f);
        if (m_checkbox)
        {
            if (auto b = m_flagUndo.commitBefore(tr, flagBefore, 7u))
                m_flagEntries.emplace_back(*b, m_flag);
        }
        else if (auto b = m_undo.commitBefore(tr, before, 7u))
        {
            m_entries.emplace_back(*b, m_value);
        }
        ImGui::End();
        ImGui::Render();
    }

    void mouseAt(float dx) { ImGui::GetIO().AddMousePosEvent(m_center.x + dx, m_center.y); }
    void button(bool down) { ImGui::GetIO().AddMouseButtonEvent(0, down); }

    /// Press on the slider, drag right, release.
    void drag()
    {
        const float x0 = m_center.x;
        mouseAt(0.0f); frame();
        button(true); frame();
        for (int i = 1; i <= 10; ++i)
        {
            ImGui::GetIO().AddMousePosEvent(x0 + 5.0f * static_cast<float>(i), m_center.y);
            frame();
        }
        button(false); frame();
        frame();
    }
};

TEST_F(DragUndoTest, DragRecordsTheValueFromBeforeTheDrag)
{
    frame();  // lay out, so the slider has a position
    drag();
    ASSERT_EQ(m_entries.size(), 1u);
    EXPECT_FLOAT_EQ(m_entries[0].first, 1.0f);   // undo restores the start
    EXPECT_GT(m_entries[0].second, 1.0f);        // redo restores the drag
}

TEST_F(DragUndoTest, ClickWithoutEditLeavesNoStaleStart)
{
    frame();
    mouseAt(0.0f); frame();
    button(true); frame();
    button(false); frame();
    frame();
    ASSERT_TRUE(m_entries.empty());
    for (int i = 0; i < 60; ++i) frame();  // past the double-click window

    m_value = 5.0f;  // changed elsewhere, e.g. by an undo
    drag();
    ASSERT_EQ(m_entries.size(), 1u);
    EXPECT_FLOAT_EQ(m_entries[0].first, 5.0f);
}

TEST_F(DragUndoTest, CheckboxClickRecordsOneStep)
{
    m_checkbox = true;
    frame();
    mouseAt(0.0f); frame();
    button(true); frame();
    button(false); frame();
    frame();
    frame();
    ASSERT_EQ(m_flagEntries.size(), 1u);
    EXPECT_FALSE(m_flagEntries[0].first);
    EXPECT_TRUE(m_flagEntries[0].second);
}

}  // namespace Vestige::Test

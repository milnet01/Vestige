// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_terrain_panel_tabs.cpp
/// @brief 3D_E-0874 lane 15 — the Terrain panel's Paint tab switches the
///        brush to PAINT. Going back to the Sculpt tab must give the brush
///        its sculpt mode again; before the fix it kept painting while the
///        Sculpt tab showed no mode selected.
///
/// Drives the real TerrainPanel headlessly, as test_hierarchy_delete_key
/// does. Terrain::initialize needs GL, hence the GL fixture.

#include "gl_test_fixture.h"

#include "editor/command_history.h"
#include "editor/panels/terrain_panel.h"
#include "editor/tools/terrain_brush.h"
#include "environment/terrain.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cstring>
#include <optional>

namespace Vestige::Test
{

class TerrainPanelTabsTest : public GLTestFixture
{
protected:
    Terrain m_terrain;
    TerrainBrush m_brush;
    CommandHistory m_history;
    TerrainPanel m_panel;
    bool m_imguiCreated = false;

    void SetUp() override
    {
        GLTestFixture::SetUp();
        if (IsSkipped())
        {
            return;
        }
        TerrainConfig config;
        config.width = 33;
        config.depth = 33;
        ASSERT_TRUE(m_terrain.initialize(config));

        ImGui::CreateContext();
        m_imguiCreated = true;
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(640.0f, 480.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        m_panel.setOpen(true);
        m_brush.setEnabled(true);
    }

    void TearDown() override
    {
        if (m_imguiCreated)
        {
            ImGui::DestroyContext();
        }
    }

    void frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(500.0f, 460.0f));
        m_panel.draw(m_brush, m_terrain, m_history);
        ImGui::Render();
    }

    /// The centre of the named tab, read from ImGui's own tab-bar layout.
    std::optional<ImVec2> tabCentre(const char* label)
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Terrain");
        if (window == nullptr) return std::nullopt;
        ImGuiTabBar* bar = ImGui::GetCurrentContext()->TabBars.GetByKey(
            window->GetID("TerrainTabs"));
        if (bar == nullptr) return std::nullopt;
        for (ImGuiTabItem& tab : bar->Tabs)
        {
            if (std::strcmp(ImGui::TabBarGetTabName(bar, &tab), label) == 0)
            {
                return ImVec2(bar->BarRect.Min.x + tab.Offset + (tab.Width * 0.5f),
                              bar->BarRect.GetCenter().y);
            }
        }
        return std::nullopt;
    }

    void clickTab(const char* label)
    {
        const auto at = tabCentre(label);
        ASSERT_TRUE(at.has_value()) << "tab not found: " << label;
        ImGui::GetIO().AddMousePosEvent(at->x, at->y);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame();
        frame();
    }
};

TEST_F(TerrainPanelTabsTest, SculptTabRestoresTheSculptModeAfterPainting)
{
    m_brush.mode = TerrainBrushMode::SMOOTH;
    frame();
    frame();

    clickTab("Paint");
    ASSERT_EQ(m_brush.mode, TerrainBrushMode::PAINT)
        << "the Paint tab did not switch the brush to PAINT";

    clickTab("Sculpt");
    EXPECT_EQ(m_brush.mode, TerrainBrushMode::SMOOTH)
        << "back on the Sculpt tab the brush still paints";
}

} // namespace Vestige::Test

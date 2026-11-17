// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_panel_zone_ids.cpp
/// @brief 3D_E-0874 lane 15 — an entity that is both a reverb zone and an
///        ambient zone appears in two lists on the Audio panel's Zones tab.
///        Each list must give its row its own ImGui ID; before the fix both
///        rows had the same ID, so clicking one could act on the other.
///
/// Drives the real AudioPanel headlessly and lets ImGui's own conflict
/// check decide: it flags an ID submitted twice when the mouse is over it.
#include <gtest/gtest.h>

#include "audio/ambient_zone_component.h"
#include "audio/reverb_zone_component.h"
#include "editor/panels/audio_panel.h"
#include "scene/entity.h"
#include "scene/scene.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstring>

namespace Vestige::Test
{

class AudioPanelZoneIdsTest : public ::testing::Test
{
protected:
    Scene m_scene{"Zones"};
    AudioPanel m_panel;
    bool m_conflictSeen = false;

    void SetUp() override
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800.0f, 900.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        io.ConfigDebugHighlightIdConflicts = true;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();

        Entity* both = m_scene.createEntity("Hall");
        both->addComponent<ReverbZoneComponent>();
        both->addComponent<AmbientZoneComponent>();
        m_panel.setOpen(true);
    }

    void TearDown() override { ImGui::DestroyContext(); }

    void frame()
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(800.0f, 900.0f));
        m_panel.draw(nullptr, &m_scene);
        ImGui::Render();
        if (GImGui->DebugDrawIdConflictsId != 0) m_conflictSeen = true;
    }

    void clickZonesTab()
    {
        ImGuiWindow* window = ImGui::FindWindowByName("Audio");
        ASSERT_NE(window, nullptr);
        ImGuiTabBar* bar = GImGui->TabBars.GetByKey(window->GetID("AudioTabs"));
        ASSERT_NE(bar, nullptr);
        for (ImGuiTabItem& tab : bar->Tabs)
        {
            if (std::strcmp(ImGui::TabBarGetTabName(bar, &tab), "Zones") == 0)
            {
                const float x = bar->BarRect.Min.x + tab.Offset + (tab.Width * 0.5f);
                ImGui::GetIO().AddMousePosEvent(x, bar->BarRect.GetCenter().y);
                frame();
                ImGui::GetIO().AddMouseButtonEvent(0, true);
                frame();
                ImGui::GetIO().AddMouseButtonEvent(0, false);
                frame();
                frame();
                return;
            }
        }
        FAIL() << "Zones tab not found";
    }
};

TEST_F(AudioPanelZoneIdsTest, AnEntityInTwoZoneListsHasTwoIds)
{
    frame();
    frame();
    clickZonesTab();
    m_conflictSeen = false;

    // Hover every row of the tab; ImGui flags any ID submitted twice.
    for (float y = 40.0f; y < 880.0f; y += 4.0f)
    {
        ImGui::GetIO().AddMousePosEvent(60.0f, y);
        frame();
    }
    EXPECT_FALSE(m_conflictSeen) << "two widgets on the Zones tab share an ImGui ID";
}

} // namespace Vestige::Test

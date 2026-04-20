// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tilemap_panel.cpp
/// @brief TilemapPanel implementation.
#include "editor/panels/tilemap_panel.h"

#include "editor/selection.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "scene/tilemap_component.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace Vestige
{

bool TilemapPanel::paintCell(TilemapComponent& tilemap, int col, int row)
{
    if (m_activeLayer >= tilemap.layers.size())
    {
        return false;
    }
    tilemap.layers[m_activeLayer].set(col, row, m_activeTileId);
    return true;
}

bool TilemapPanel::eraseCell(TilemapComponent& tilemap, int col, int row)
{
    if (m_activeLayer >= tilemap.layers.size())
    {
        return false;
    }
    tilemap.layers[m_activeLayer].set(col, row, 0);
    return true;
}

void TilemapPanel::draw(Scene* scene, Selection* selection)
{
    if (!m_visible)
    {
        return;
    }
    if (!ImGui::Begin("Tilemap", &m_visible))
    {
        ImGui::End();
        return;
    }

    TilemapComponent* tilemap = nullptr;
    if (scene && selection)
    {
        if (auto* e = selection->getPrimaryEntity(*scene))
        {
            tilemap = e->getComponent<TilemapComponent>();
        }
    }

    if (!tilemap)
    {
        ImGui::TextDisabled("Select an entity with a TilemapComponent.");
        ImGui::End();
        return;
    }

    // --- Layer list ---
    ImGui::Text("Layers (%zu)", tilemap->layers.size());
    for (std::size_t i = 0; i < tilemap->layers.size(); ++i)
    {
        char label[64];
        std::snprintf(label, sizeof(label), "%s##layer_%zu",
                      tilemap->layers[i].name.c_str(), i);
        const bool selected = (i == m_activeLayer);
        if (ImGui::Selectable(label, selected))
        {
            m_activeLayer = i;
        }
    }
    if (ImGui::Button("Add Layer"))
    {
        tilemap->addLayer("Layer", 16, 16);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Adds a new 16×16 layer. Resize via the "
                          "knobs below.");
    }

    // --- Active layer resize ---
    if (m_activeLayer < tilemap->layers.size())
    {
        auto& layer = tilemap->layers[m_activeLayer];
        int w = layer.width;
        int h = layer.height;
        ImGui::Separator();
        ImGui::Text("Active: %s  (%d × %d)", layer.name.c_str(), w, h);
        bool resized = false;
        if (ImGui::InputInt("Width", &w))  { resized = true; }
        if (ImGui::InputInt("Height", &h)) { resized = true; }
        if (resized)
        {
            layer.resize(std::max(0, w), std::max(0, h));
        }
    }

    // --- Tile palette ---
    ImGui::Separator();
    ImGui::Text("Tile palette");
    for (std::size_t i = 1; i < tilemap->tileDefs.size(); ++i)
    {
        char label[64];
        std::snprintf(label, sizeof(label), "%zu: %s##tile_%zu",
                      i, tilemap->tileDefs[i].atlasFrameName.c_str(), i);
        const bool selected = (m_activeTileId == static_cast<std::uint16_t>(i));
        if (ImGui::Selectable(label, selected))
        {
            m_activeTileId = static_cast<std::uint16_t>(i);
        }
    }

    // --- Brush hint ---
    ImGui::Separator();
    ImGui::TextDisabled("Click on a cell in the viewport to paint; "
                        "right-click to erase. Brush logic wired from "
                        "the viewport in Phase 9F-6.");

    ImGui::End();
}

} // namespace Vestige

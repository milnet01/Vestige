// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file tilemap_panel.h
/// @brief TilemapPanel — editor UI for painting tiles and managing
/// layers on the selected TilemapComponent (Phase 9F-6).
///
/// Phase-9F-6 scope:
///   - layer list (pick active layer, add/remove)
///   - tile palette (pick active tile id from tileDefs)
///   - grid paint: click a cell to stamp, right-click to erase
///   - simple resize knob for the active layer
/// Auto-tiling and multi-tile brushes are Phase 18 polish.
#pragma once

#include <cstdint>

namespace Vestige
{

class Scene;
class Selection;
class TilemapComponent;

class TilemapPanel
{
public:
    TilemapPanel() = default;

    void toggleVisible() { m_visible = !m_visible; }
    bool isVisible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; }

    /// @brief Draws the panel. Both arguments may be null.
    void draw(Scene* scene, Selection* selection);

    // Test accessors — let tests drive the panel state without ImGui.
    std::size_t getActiveLayer() const { return m_activeLayer; }
    void setActiveLayer(std::size_t i) { m_activeLayer = i; }
    std::uint16_t getActiveTileId() const { return m_activeTileId; }
    void setActiveTileId(std::uint16_t id) { m_activeTileId = id; }

    /// @brief Paints a single cell on the bound tilemap's active layer.
    /// Safe to call with no selection — returns false.
    bool paintCell(TilemapComponent& tilemap, int col, int row);

    /// @brief Erases a single cell on the active layer.
    bool eraseCell(TilemapComponent& tilemap, int col, int row);

private:
    bool m_visible = false;
    std::size_t m_activeLayer = 0;
    std::uint16_t m_activeTileId = 1;
};

} // namespace Vestige

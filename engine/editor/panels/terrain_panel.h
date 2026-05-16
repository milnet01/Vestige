// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_panel.h
/// @brief ImGui panel for terrain sculpting, painting, and settings.
#pragma once

#include "editor/panels/i_panel.h"
#include "editor/tools/terrain_brush.h"

namespace Vestige
{

class CommandHistory;
class Terrain;

/// @brief Draws the Terrain panel with sculpting/painting controls and settings.
class TerrainPanel : public IPanel
{
public:
    const char* displayName() const override { return "Terrain"; }

    /// @brief Draws the panel contents inside the current ImGui window.
    /// @param brush The terrain brush tool to configure.
    /// @param terrain The terrain to display info for.
    /// @param history Command history (for undo commands).
    void draw(TerrainBrush& brush, Terrain& terrain, CommandHistory& history);

    /// @brief Whether the panel is currently open/visible.
    bool isOpen() const override { return m_open; }

    /// @brief Toggle panel visibility.
    void setOpen(bool open) override { m_open = open; }

private:
    void drawSculptSection(TerrainBrush& brush);
    void drawPaintSection(TerrainBrush& brush, Terrain& terrain);
    void drawSettingsSection(Terrain& terrain);
    void drawImportExportSection(Terrain& terrain);

    bool m_open = false;

    // Auto-texture parameters (persisted across draws)
    float m_slopeGrassEnd = 0.3f;
    float m_slopeRockStart = 0.6f;
    float m_noiseScale = 0.05f;
    float m_noiseAmplitude = 0.12f;

    /// @brief Layer names for display.
    static constexpr const char* LAYER_NAMES[4] = {"Grass", "Rock", "Dirt", "Sand"};
};

} // namespace Vestige

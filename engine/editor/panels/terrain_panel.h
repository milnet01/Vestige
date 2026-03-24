/// @file terrain_panel.h
/// @brief ImGui panel for terrain sculpting, painting, and settings.
#pragma once

#include "editor/tools/terrain_brush.h"

namespace Vestige
{

class CommandHistory;
class Terrain;

/// @brief Draws the Terrain panel with sculpting/painting controls and settings.
class TerrainPanel
{
public:
    /// @brief Draws the panel contents inside the current ImGui window.
    /// @param brush The terrain brush tool to configure.
    /// @param terrain The terrain to display info for.
    /// @param history Command history (for undo commands).
    void draw(TerrainBrush& brush, Terrain& terrain, CommandHistory& history);

    /// @brief Whether the panel is currently open/visible.
    bool isOpen() const { return m_open; }

    /// @brief Toggle panel visibility.
    void setOpen(bool open) { m_open = open; }

private:
    void drawSculptSection(TerrainBrush& brush);
    void drawPaintSection(TerrainBrush& brush);
    void drawSettingsSection(Terrain& terrain);
    void drawImportExportSection(Terrain& terrain);

    bool m_open = false;

    /// @brief Layer names for display.
    static constexpr const char* LAYER_NAMES[4] = {"Grass", "Rock", "Dirt", "Sand"};
};

} // namespace Vestige

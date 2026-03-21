/// @file environment_panel.h
/// @brief ImGui panel for environment painting — brush settings, palette, biome presets.
#pragma once

#include "editor/tools/brush_tool.h"
#include "environment/biome_preset.h"

namespace Vestige
{

class CommandHistory;
class FoliageManager;
class FoliageRenderer;

/// @brief Draws the Environment Painting panel with brush controls and foliage palette.
class EnvironmentPanel
{
public:
    /// @brief Draws the panel contents inside the current ImGui window.
    /// @param brushTool The brush tool to configure.
    /// @param manager The foliage manager (for stats display).
    /// @param history Command history (for undo commands).
    void draw(BrushTool& brushTool, FoliageManager& manager, CommandHistory& history);

    /// @brief Gets the biome library for preset access.
    BiomeLibrary& getBiomeLibrary() { return m_biomeLibrary; }

    /// @brief Whether the panel is currently open/visible.
    bool isOpen() const { return m_open; }

    /// @brief Toggle panel visibility.
    void setOpen(bool open) { m_open = open; }

private:
    bool m_open = false;
    BiomeLibrary m_biomeLibrary;
    int m_selectedBiome = 0;
};

} // namespace Vestige

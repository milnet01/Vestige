/// @file environment_panel.h
/// @brief ImGui panel for environment painting — brush settings, palette, biome presets.
#pragma once

#include "editor/tools/brush_tool.h"
#include "environment/biome_preset.h"
#include "environment/density_map.h"
#include "environment/terrain.h"

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
    /// @param terrain Optional terrain for bank blend controls (nullptr = hide controls).
    void draw(BrushTool& brushTool, FoliageManager& manager, CommandHistory& history,
              Terrain* terrain = nullptr);

    /// @brief Gets the biome library for preset access.
    BiomeLibrary& getBiomeLibrary() { return m_biomeLibrary; }

    /// @brief Whether the panel is currently open/visible.
    bool isOpen() const { return m_open; }

    /// @brief Toggle panel visibility.
    void setOpen(bool open) { m_open = open; }

    /// @brief Gets the density map (creates on first access if terrain is set).
    DensityMap& getDensityMap() { return m_densityMap; }

private:
    bool m_open = false;
    BiomeLibrary m_biomeLibrary;
    int m_selectedBiome = 0;

    // Density map
    DensityMap m_densityMap;

    // Bank blend
    Terrain::BankBlendConfig m_bankBlendConfig;
    glm::vec2 m_bankWaterCenter{0.0f};
    glm::vec2 m_bankWaterHalfExtent{25.0f};
};

} // namespace Vestige

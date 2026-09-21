// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file environment_panel.cpp
/// @brief EnvironmentPanel implementation — brush tool ImGui controls.
#include "editor/panels/environment_panel.h"
#include "editor/command_history.h"
#include "environment/foliage_manager.h"

#include <imgui.h>

#include <string>

namespace Vestige
{

void EnvironmentPanel::draw(BrushTool& brushTool, FoliageManager& manager,
                             CommandHistory& history, Terrain* terrain)
{
    (void)history;

    if (!m_open) return;

    if (!ImGui::Begin("Environment", &m_open))
    {
        ImGui::End();
        return;
    }

    // Enable/disable brush
    bool brushEnabled = brushTool.isActive();
    if (ImGui::Checkbox("Enable Brush", &brushEnabled))
    {
        brushTool.setEnabled(brushEnabled);
    }

    if (!brushEnabled)
    {
        ImGui::TextDisabled("Enable the brush to start painting.");
        ImGui::Separator();

        // Stats even when disabled
        ImGui::Text("Total instances: %d", manager.getTotalFoliageCount());
        ImGui::Text("Active chunks: %d", manager.getChunkCount());

        // Clear all button
        if (manager.getTotalFoliageCount() > 0)
        {
            ImGui::Separator();
            if (ImGui::Button("Clear All Foliage"))
            {
                manager.clear();
            }
        }

        ImGui::End();
        return;
    }

    ImGui::Separator();

    // Mode selector
    ImGui::Text("Mode:");
    ImGui::SameLine();

    int modeInt = static_cast<int>(brushTool.mode);
    const char* modeNames[] = {"Foliage", "Scatter", "Tree", "Path", "Eraser", "Density"};
    if (ImGui::Combo("##Mode", &modeInt, modeNames, 6))
    {
        brushTool.mode = static_cast<BrushTool::Mode>(modeInt);
    }

    ImGui::Separator();

    // Brush settings
    ImGui::Text("Brush Settings");

    ImGui::SliderFloat("Radius", &brushTool.radius, 0.5f, 30.0f, "%.1f m");
    ImGui::InputFloat("##RadiusInput", &brushTool.radius, 0.5f, 1.0f, "%.1f");
    if (brushTool.radius < 0.5f) brushTool.radius = 0.5f;
    if (brushTool.radius > 50.0f) brushTool.radius = 50.0f;

    ImGui::SliderFloat("Density", &brushTool.density, 0.1f, 20.0f, "%.1f /m\xC2\xB2");
    ImGui::InputFloat("##DensityInput", &brushTool.density, 0.5f, 1.0f, "%.1f");
    if (brushTool.density < 0.1f) brushTool.density = 0.1f;

    ImGui::SliderFloat("Falloff", &brushTool.falloff, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Stamp Spacing", &brushTool.stampSpacing, 0.1f, 1.0f, "%.2f");

    ImGui::Separator();

    // Foliage type config (only in foliage mode)
    if (brushTool.mode == BrushTool::Mode::FOLIAGE)
    {
        ImGui::Text("Foliage Type");

        // Type selector
        int typeId = static_cast<int>(brushTool.selectedTypeId);
        const char* typeNames[] = {"Short Grass", "Tall Grass", "Flowers", "Ferns"};
        if (ImGui::Combo("Type", &typeId, typeNames, 4))
        {
            brushTool.selectedTypeId = static_cast<uint32_t>(typeId);
        }

        ImGui::SliderFloat("Min Scale", &brushTool.foliageConfig.minScale, 0.1f, 3.0f, "%.2f");
        ImGui::SliderFloat("Max Scale", &brushTool.foliageConfig.maxScale, 0.1f, 3.0f, "%.2f");
        if (brushTool.foliageConfig.minScale > brushTool.foliageConfig.maxScale)
        {
            brushTool.foliageConfig.maxScale = brushTool.foliageConfig.minScale;
        }

        ImGui::SliderFloat("Wind Amplitude", &brushTool.foliageConfig.windAmplitude,
                           0.0f, 0.5f, "%.3f");
        ImGui::SliderFloat("Wind Frequency", &brushTool.foliageConfig.windFrequency,
                           0.0f, 10.0f, "%.1f");

        ImGui::ColorEdit3("Tint Variation", &brushTool.foliageConfig.tintVariation.x);
    }

    // Scatter type config (only in scatter mode)
    if (brushTool.mode == BrushTool::Mode::SCATTER)
    {
        ImGui::Text("Scatter Type");

        int typeId = static_cast<int>(brushTool.selectedTypeId);
        const char* scatterNames[] = {"Small Rock", "Large Rock", "Debris", "Pebbles"};
        if (ImGui::Combo("Scatter Type", &typeId, scatterNames, 4))
        {
            brushTool.selectedTypeId = static_cast<uint32_t>(typeId);
        }

        ImGui::SliderFloat("Min Scale##scatter", &brushTool.scatterConfig.minScale,
                           0.1f, 5.0f, "%.2f");
        ImGui::SliderFloat("Max Scale##scatter", &brushTool.scatterConfig.maxScale,
                           0.1f, 5.0f, "%.2f");
        if (brushTool.scatterConfig.minScale > brushTool.scatterConfig.maxScale)
        {
            brushTool.scatterConfig.maxScale = brushTool.scatterConfig.minScale;
        }

        ImGui::SliderFloat("Surface Alignment", &brushTool.scatterConfig.surfaceAlignment,
                           0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("0 = upright, 1 = fully aligned to surface");
    }

    // Tree species config (only in tree mode)
    if (brushTool.mode == BrushTool::Mode::TREE)
    {
        ImGui::Text("Tree Species");

        int speciesId = static_cast<int>(brushTool.selectedSpeciesId);
        const char* speciesNames[] = {"Olive", "Cedar", "Palm", "Oak"};
        if (ImGui::Combo("Species", &speciesId, speciesNames, 4))
        {
            brushTool.selectedSpeciesId = static_cast<uint32_t>(speciesId);
        }

        ImGui::SliderFloat("Min Scale##tree", &brushTool.treeConfig.minScale,
                           0.3f, 3.0f, "%.2f");
        ImGui::SliderFloat("Max Scale##tree", &brushTool.treeConfig.maxScale,
                           0.3f, 3.0f, "%.2f");
        if (brushTool.treeConfig.minScale > brushTool.treeConfig.maxScale)
        {
            brushTool.treeConfig.maxScale = brushTool.treeConfig.minScale;
        }

        ImGui::SliderFloat("Min Spacing", &brushTool.treeConfig.minSpacing,
                           1.0f, 15.0f, "%.1f m");
        ImGui::TextDisabled("Trees closer than this will be rejected");
    }

    // Density map controls (only in DENSITY mode)
    if (brushTool.mode == BrushTool::Mode::DENSITY)
    {
        // 3D_E-0632: paint into the SCENE's mask, held by FoliageManager, not
        // into a panel-local one. The panel is not reachable from the scene
        // serialiser, so a mask owned here was discarded on every save.
        DensityMap& densityMap = manager.getDensityMap();

        ImGui::Text("Density Map");

        if (!densityMap.isInitialized())
        {
            if (ImGui::Button("Create Density Map"))
            {
                // Default: covers 256x256m centered at origin, 1 texel/m
                densityMap.initialize(-128.0f, -128.0f, 256.0f, 256.0f, 1.0f);
                brushTool.densityMap = &densityMap;
            }
            ImGui::TextDisabled("No density map. Create one to start painting.");
        }
        else
        {
            brushTool.densityMap = &densityMap;

            ImGui::Text("Size: %dx%d (%.0fm x %.0fm)",
                        densityMap.getWidth(), densityMap.getHeight(),
                        static_cast<double>(densityMap.getWorldExtent().x),
                        static_cast<double>(densityMap.getWorldExtent().y));

            ImGui::SliderFloat("Paint Value", &brushTool.densityPaintValue, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("0 = block foliage, 1 = allow foliage");

            ImGui::SliderFloat("Strength", &brushTool.densityPaintStrength, 0.01f, 1.0f, "%.2f");

            if (ImGui::Button("Fill All"))
            {
                densityMap.fill(1.0f);
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear All"))
            {
                densityMap.fill(0.0f);
            }
        }
    }
    else
    {
        // When not in density mode, still link density map if it exists.
        // Fetched here rather than reusing the reference above — that one is
        // scoped to the density-mode branch (3D_E-0632).
        DensityMap& sceneDensityMap = manager.getDensityMap();
        if (sceneDensityMap.isInitialized())
        {
            brushTool.densityMap = &sceneDensityMap;
        }
        else
        {
            brushTool.densityMap = nullptr;
        }
    }

    // Bank blend controls
    if (terrain && terrain->isInitialized())
    {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Bank Blending"))
        {
            ImGui::SliderFloat("Blend Width", &m_bankBlendConfig.blendWidth,
                               0.5f, 15.0f, "%.1f m");
            ImGui::TextDisabled("Distance from water edge to blend bank material");

            const char* channelNames[] = {"R (Grass)", "G (Rock)", "B (Dirt)", "A (Sand)"};
            ImGui::Combo("Bank Material", &m_bankBlendConfig.bankChannel, channelNames, 4);

            ImGui::SliderFloat("Bank Strength", &m_bankBlendConfig.bankStrength,
                               0.0f, 1.0f, "%.2f");

            ImGui::DragFloat2("Water Center", &m_bankWaterCenter.x, 0.5f);
            ImGui::DragFloat2("Water Half-Extent", &m_bankWaterHalfExtent.x, 0.5f, 1.0f, 500.0f);

            if (ImGui::Button("Apply Bank Blend"))
            {
                terrain->applyBankBlend(m_bankWaterCenter, m_bankWaterHalfExtent,
                                        m_bankBlendConfig);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Blends bank material into terrain splatmap\n"
                                  "near the water body edges.");
            }
        }
    }

    ImGui::Separator();

    // Stats
    ImGui::Text("Statistics");
    ImGui::Text("Total instances: %d", manager.getTotalFoliageCount());
    ImGui::Text("Active chunks: %d", manager.getChunkCount());

    // Clear all button
    if (manager.getTotalFoliageCount() > 0)
    {
        ImGui::Separator();
        if (ImGui::Button("Clear All Foliage"))
        {
            manager.clear();
        }
    }

    // Biome presets
    ImGui::Separator();
    ImGui::Text("Biome Presets");
    {
        auto names = m_biomeLibrary.getPresetNames();
        if (!names.empty())
        {
            // Build combo items
            std::string preview = (m_selectedBiome >= 0 && m_selectedBiome < static_cast<int>(names.size()))
                ? names[static_cast<size_t>(m_selectedBiome)] : "None";
            if (ImGui::BeginCombo("Biome", preview.c_str()))
            {
                for (int i = 0; i < static_cast<int>(names.size()); ++i)
                {
                    bool selected = (i == m_selectedBiome);
                    if (ImGui::Selectable(names[static_cast<size_t>(i)].c_str(), selected))
                    {
                        m_selectedBiome = i;
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Apply Biome to Brush"))
            {
                if (m_selectedBiome >= 0 && m_selectedBiome < m_biomeLibrary.getPresetCount())
                {
                    const auto& preset = m_biomeLibrary.getPreset(m_selectedBiome);
                    if (!preset.foliageLayers.empty())
                    {
                        brushTool.mode = BrushTool::Mode::FOLIAGE;
                        brushTool.selectedTypeId = preset.foliageLayers[0].typeId;
                        brushTool.density = preset.foliageLayers[0].density;
                    }
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Sets brush to the biome's primary foliage type and density");
            }
        }
    }

    // Shortcut hints
    ImGui::Separator();
    ImGui::TextDisabled("LMB: paint  |  [ / ]: radius");
    ImGui::TextDisabled("Shift+[ / ]: density");

    ImGui::End();
}

} // namespace Vestige

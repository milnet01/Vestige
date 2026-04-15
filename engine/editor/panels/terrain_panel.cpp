// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_panel.cpp
/// @brief ImGui panel for terrain sculpting, painting, and settings.

#include "editor/panels/terrain_panel.h"
#include "editor/command_history.h"
#include "environment/terrain.h"
#include "core/logger.h"

#include <imgui.h>

#include <string>

namespace Vestige
{

void TerrainPanel::draw(TerrainBrush& brush, Terrain& terrain, CommandHistory& /*history*/)
{
    if (!m_open) return;

    if (!ImGui::Begin("Terrain", &m_open))
    {
        ImGui::End();
        return;
    }

    if (!terrain.isInitialized())
    {
        ImGui::TextDisabled("No terrain loaded.");
        ImGui::End();
        return;
    }

    // Terrain info header
    const auto& cfg = terrain.getConfig();
    ImGui::Text("Heightmap: %dx%d (%.0fm x %.0fm)",
                cfg.width, cfg.depth,
                static_cast<double>(terrain.getWorldWidth()),
                static_cast<double>(terrain.getWorldDepth()));
    ImGui::Separator();

    // Tab bar for sculpt / paint / settings
    if (ImGui::BeginTabBar("TerrainTabs"))
    {
        if (ImGui::BeginTabItem("Sculpt"))
        {
            drawSculptSection(brush);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Paint"))
        {
            drawPaintSection(brush, terrain);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings"))
        {
            drawSettingsSection(terrain);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Import/Export"))
        {
            drawImportExportSection(terrain);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void TerrainPanel::drawSculptSection(TerrainBrush& brush)
{
    // Enable/disable brush
    bool active = brush.isActive();
    if (ImGui::Checkbox("Enable Terrain Brush", &active))
    {
        brush.setEnabled(active);
    }

    ImGui::Spacing();

    // Mode selection
    ImGui::Text("Mode:");
    int modeInt = static_cast<int>(brush.mode);

    // Only show sculpt modes (not PAINT)
    if (ImGui::RadioButton("Raise", modeInt == 0)) brush.mode = TerrainBrushMode::RAISE;
    ImGui::SameLine();
    if (ImGui::RadioButton("Lower", modeInt == 1)) brush.mode = TerrainBrushMode::LOWER;
    if (ImGui::RadioButton("Smooth", modeInt == 2)) brush.mode = TerrainBrushMode::SMOOTH;
    ImGui::SameLine();
    if (ImGui::RadioButton("Flatten", modeInt == 3)) brush.mode = TerrainBrushMode::FLATTEN;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Brush parameters
    ImGui::SliderFloat("Radius", &brush.radius, 1.0f, 50.0f, "%.1f m");
    ImGui::SliderFloat("Strength", &brush.strength, 0.01f, 2.0f, "%.2f");
    ImGui::SliderFloat("Falloff", &brush.falloff, 0.0f, 1.0f, "%.2f");

    ImGui::Spacing();

    // Hints
    ImGui::TextWrapped("Hold LMB on terrain to sculpt. "
                       "Alt+LMB to orbit camera instead.");
}

void TerrainPanel::drawPaintSection(TerrainBrush& brush, Terrain& terrain)
{
    // Enable/disable brush
    bool active = brush.isActive();
    if (ImGui::Checkbox("Enable Terrain Brush", &active))
    {
        brush.setEnabled(active);
    }

    // Switch to paint mode when this tab is visible
    if (brush.isActive() && brush.mode != TerrainBrushMode::PAINT)
    {
        brush.mode = TerrainBrushMode::PAINT;
    }

    ImGui::Spacing();

    // Layer selection
    ImGui::Text("Paint Layer:");
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0) ImGui::SameLine();

        // Color preview square
        ImVec4 colors[4] = {
            {0.3f, 0.6f, 0.2f, 1.0f},  // Grass (green)
            {0.5f, 0.5f, 0.5f, 1.0f},  // Rock (grey)
            {0.6f, 0.4f, 0.2f, 1.0f},  // Dirt (brown)
            {0.9f, 0.8f, 0.5f, 1.0f},  // Sand (tan)
        };

        ImGui::PushID(i);
        bool selected = (brush.paintChannel == i);
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, colors[i]);
        }

        if (ImGui::Button(LAYER_NAMES[i], ImVec2(60, 0)))
        {
            brush.paintChannel = i;
        }

        if (selected)
        {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Brush parameters
    ImGui::SliderFloat("Radius", &brush.radius, 1.0f, 50.0f, "%.1f m");
    ImGui::SliderFloat("Strength", &brush.strength, 0.01f, 2.0f, "%.2f");
    ImGui::SliderFloat("Falloff", &brush.falloff, 0.0f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::TextWrapped("Hold LMB on terrain to paint. "
                       "Weights are auto-normalized so all layers sum to 1.");

    // --- Auto-texture section ---
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Auto-Texture (Slope/Altitude)");
    ImGui::Spacing();

    ImGui::SliderFloat("Slope Grass End", &m_slopeGrassEnd, 0.05f, 0.8f, "%.2f");
    ImGui::SliderFloat("Slope Rock Start", &m_slopeRockStart, 0.1f, 0.95f, "%.2f");
    ImGui::SliderFloat("Noise Scale", &m_noiseScale, 0.005f, 0.2f, "%.3f");
    ImGui::SliderFloat("Noise Amount", &m_noiseAmplitude, 0.0f, 0.3f, "%.2f");

    ImGui::Spacing();
    if (ImGui::Button("Generate Auto-Texture"))
    {
        Terrain::AutoTextureConfig cfg;
        cfg.slopeGrassEnd = m_slopeGrassEnd;
        cfg.slopeRockStart = m_slopeRockStart;
        cfg.noiseScale = m_noiseScale;
        cfg.noiseAmplitude = m_noiseAmplitude;
        terrain.generateAutoTexture(cfg);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Generates splatmap from terrain slope and altitude.\n"
                          "Manual paint overrides this. Layers: Grass/Rock/Dirt/Sand.");
    }
}

void TerrainPanel::drawSettingsSection(Terrain& terrain)
{
    const auto& cfg = terrain.getConfig();

    ImGui::Text("Width: %d", cfg.width);
    ImGui::Text("Depth: %d", cfg.depth);
    ImGui::Text("Height Scale: %.1f m", static_cast<double>(cfg.heightScale));
    ImGui::Text("Grid Resolution: %d", cfg.gridResolution);
    ImGui::Text("LOD Levels: %d", cfg.maxLodLevels);
    ImGui::Text("Base LOD Distance: %.1f m", static_cast<double>(cfg.baseLodDistance));
    ImGui::Text("Origin: (%.1f, %.1f, %.1f)",
                static_cast<double>(cfg.origin.x),
                static_cast<double>(cfg.origin.y),
                static_cast<double>(cfg.origin.z));
}

void TerrainPanel::drawImportExportSection(Terrain& terrain)
{
    ImGui::TextWrapped("Import/export heightmap and splatmap data.");
    ImGui::Spacing();

    if (ImGui::Button("Flatten Terrain"))
    {
        int w = terrain.getWidth();
        int d = terrain.getDepth();
        for (int z = 0; z < d; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                terrain.setRawHeight(x, z, 0.0f);
            }
        }
        terrain.updateHeightmapRegion(0, 0, w, d);
        terrain.updateNormalMapRegion(0, 0, w, d);
        terrain.buildQuadtree();
        Logger::info("Terrain flattened");
    }

    if (ImGui::Button("Reset Splatmap"))
    {
        int w = terrain.getWidth();
        int d = terrain.getDepth();
        for (int z = 0; z < d; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                terrain.setSplatWeight(x, z, 0, 1.0f);
                terrain.setSplatWeight(x, z, 1, 0.0f);
                terrain.setSplatWeight(x, z, 2, 0.0f);
                terrain.setSplatWeight(x, z, 3, 0.0f);
            }
        }
        terrain.updateSplatmapRegion(0, 0, w, d);
        Logger::info("Splatmap reset to layer 0");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Quick Save/Load:");

    if (ImGui::Button("Save Heightmap (.r32)"))
    {
        terrain.saveHeightmap("terrain_heightmap.r32");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Heightmap (.r32)"))
    {
        terrain.loadHeightmap("terrain_heightmap.r32");
    }

    if (ImGui::Button("Save Splatmap (.splat)"))
    {
        terrain.saveSplatmap("terrain_splatmap.splat");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Splatmap (.splat)"))
    {
        terrain.loadSplatmap("terrain_splatmap.splat");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Terrain is also saved/loaded with scene files (Ctrl+S).");
}

} // namespace Vestige

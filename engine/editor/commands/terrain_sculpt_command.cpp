// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_sculpt_command.cpp
/// @brief Undoable commands for terrain heightmap sculpting and splatmap painting.

#include "editor/commands/terrain_sculpt_command.h"

namespace Vestige
{

// ---------------------------------------------------------------------------
// TerrainSculptCommand
// ---------------------------------------------------------------------------

TerrainSculptCommand::TerrainSculptCommand(Terrain& terrain,
                                           int x, int z, int w, int h,
                                           std::vector<float> beforeHeights,
                                           std::vector<float> afterHeights)
    : m_terrain(terrain)
    , m_x(x), m_z(z), m_w(w), m_h(h)
    , m_beforeHeights(std::move(beforeHeights))
    , m_afterHeights(std::move(afterHeights))
{
}

void TerrainSculptCommand::execute()
{
    applyHeights(m_afterHeights);
}

void TerrainSculptCommand::undo()
{
    applyHeights(m_beforeHeights);
}

std::string TerrainSculptCommand::getDescription() const
{
    return "Sculpt Terrain";
}

void TerrainSculptCommand::applyHeights(const std::vector<float>& heights)
{
    for (int rz = 0; rz < m_h; ++rz)
    {
        for (int rx = 0; rx < m_w; ++rx)
        {
            float h = heights[static_cast<size_t>(rz * m_w + rx)];
            m_terrain.setRawHeight(m_x + rx, m_z + rz, h);
        }
    }

    // Update GPU textures for the affected region
    m_terrain.updateHeightmapRegion(m_x, m_z, m_w, m_h);
    m_terrain.updateNormalMapRegion(m_x, m_z, m_w, m_h);
    m_terrain.buildQuadtree();
}

// ---------------------------------------------------------------------------
// TerrainPaintCommand
// ---------------------------------------------------------------------------

TerrainPaintCommand::TerrainPaintCommand(Terrain& terrain,
                                         int x, int z, int w, int h,
                                         std::vector<glm::vec4> beforeSplat,
                                         std::vector<glm::vec4> afterSplat)
    : m_terrain(terrain)
    , m_x(x), m_z(z), m_w(w), m_h(h)
    , m_beforeSplat(std::move(beforeSplat))
    , m_afterSplat(std::move(afterSplat))
{
}

void TerrainPaintCommand::execute()
{
    applySplat(m_afterSplat);
}

void TerrainPaintCommand::undo()
{
    applySplat(m_beforeSplat);
}

std::string TerrainPaintCommand::getDescription() const
{
    return "Paint Terrain";
}

void TerrainPaintCommand::applySplat(const std::vector<glm::vec4>& splat)
{
    for (int rz = 0; rz < m_h; ++rz)
    {
        for (int rx = 0; rx < m_w; ++rx)
        {
            const auto& s = splat[static_cast<size_t>(rz * m_w + rx)];
            for (int c = 0; c < 4; ++c)
            {
                m_terrain.setSplatWeight(m_x + rx, m_z + rz, c, s[c]);
            }
        }
    }

    m_terrain.updateSplatmapRegion(m_x, m_z, m_w, m_h);
}

} // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file terrain_sculpt_command.h
/// @brief Undoable command for terrain heightmap sculpting.
#pragma once

#include "editor/commands/editor_command.h"
#include "environment/terrain.h"

#include <glm/glm.hpp>

#include <vector>

namespace Vestige
{

/// @brief Captures a rectangular region of the heightmap before and after sculpting.
///
/// On undo, restores the "before" snapshot. On redo, restores the "after" snapshot.
/// Both also update normals and the CDLOD quadtree for the affected region.
class TerrainSculptCommand : public EditorCommand
{
public:
    /// @brief Constructs a sculpt command with before/after height data.
    /// @param terrain Terrain to modify (must outlive this command).
    /// @param x Region origin X (texel).
    /// @param z Region origin Z (texel).
    /// @param w Region width (texels).
    /// @param h Region height (texels).
    /// @param beforeHeights Height data before the stroke.
    /// @param afterHeights Height data after the stroke.
    TerrainSculptCommand(Terrain& terrain,
                         int x, int z, int w, int h,
                         std::vector<float> beforeHeights,
                         std::vector<float> afterHeights);

    void execute() override;
    void undo() override;
    std::string getDescription() const override;

private:
    void applyHeights(const std::vector<float>& heights);

    Terrain& m_terrain;
    int m_x, m_z, m_w, m_h;
    std::vector<float> m_beforeHeights;
    std::vector<float> m_afterHeights;
};

/// @brief Captures a rectangular region of the splatmap before and after painting.
class TerrainPaintCommand : public EditorCommand
{
public:
    TerrainPaintCommand(Terrain& terrain,
                        int x, int z, int w, int h,
                        std::vector<glm::vec4> beforeSplat,
                        std::vector<glm::vec4> afterSplat);

    void execute() override;
    void undo() override;
    std::string getDescription() const override;

private:
    void applySplat(const std::vector<glm::vec4>& splat);

    Terrain& m_terrain;
    int m_x, m_z, m_w, m_h;
    std::vector<glm::vec4> m_beforeSplat;
    std::vector<glm::vec4> m_afterSplat;
};

} // namespace Vestige

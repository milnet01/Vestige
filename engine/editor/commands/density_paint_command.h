// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file density_paint_command.h
/// @brief Undoable density-map brush stroke (whole-map before/after snapshot).
#pragma once

#include "editor/commands/editor_command.h"
#include "environment/density_map.h"

#include <string>
#include <utility>
#include <vector>

namespace Vestige
{

/// @brief One density-brush stroke, undone by restoring the map's texels.
///
/// Holds the whole map before and after the stroke. The editor's map is
/// 256 x 256 texels (environment_panel.cpp), so each snapshot is 256 KB.
class DensityPaintCommand : public EditorCommand
{
public:
    /// @param map The map the stroke painted (must outlive this command).
    /// @param before Texels before the stroke, row-major (DensityMap::snapshot).
    /// @param after Texels after the stroke, same layout.
    DensityPaintCommand(DensityMap& map, std::vector<float> before, std::vector<float> after)
        : m_map(map)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }

    /// Re-applies the stroke. The first call (CommandHistory::execute) writes
    /// the same texels the stroke already wrote, so it changes nothing.
    void execute() override { apply(m_after); }

    void undo() override { apply(m_before); }

    std::string getDescription() const override { return "Paint density"; }

private:
    void apply(const std::vector<float>& texels)
    {
        const int w = m_map.getWidth();
        const int h = m_map.getHeight();
        if (texels.size() != static_cast<size_t>(w) * static_cast<size_t>(h))
        {
            return;  // the map was re-initialised since the stroke
        }
        for (int z = 0; z < h; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                m_map.setTexel(x, z, texels[static_cast<size_t>(z) * static_cast<size_t>(w)
                                            + static_cast<size_t>(x)]);
            }
        }
    }

    DensityMap& m_map;
    std::vector<float> m_before;
    std::vector<float> m_after;
};

} // namespace Vestige

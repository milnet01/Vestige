// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

#pragma once

/// @file plot_cache_guard.h
/// @brief Decides when the Workbench's plot cache must be rebuilt. Split
///        from workbench.cpp so tests need no ImGui (3D_E-0874).

#include "formula/curve_fitter.h"

#include <string>
#include <vector>

namespace Vestige
{

/// @brief Remembers what the plot cache was last built from.
///
/// `Workbench::renderVisualizer` asks `needsRebuild` every frame;
/// `Workbench::rebuildVisualizationCache` calls `markBuilt` when it ends.
class PlotCacheGuard
{
public:
    /// @brief True when the cache was never built, or was built from other
    ///        data, another plotted variable or another formula.
    bool needsRebuild(const std::vector<DataPoint>& points,
                      const std::string& plotVariable,
                      const std::string& formulaName) const;

    /// @brief Records the inputs the cache was just built from.
    void markBuilt(const std::vector<DataPoint>& points,
                   const std::string& plotVariable,
                   const std::string& formulaName);

private:
    bool m_built = false;
    std::vector<DataPoint> m_points;  ///< A copy: an in-place edit must show.
    std::string m_plotVariable;
    std::string m_formulaName;
};

}  // namespace Vestige

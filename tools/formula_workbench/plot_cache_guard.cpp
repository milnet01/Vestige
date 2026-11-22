// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file plot_cache_guard.cpp
/// @brief PlotCacheGuard implementation.
#include "plot_cache_guard.h"

#include <algorithm>

namespace Vestige
{

namespace
{

bool samePoints(const std::vector<DataPoint>& a, const std::vector<DataPoint>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const DataPoint& l, const DataPoint& r)
                      {
                          return l.observed == r.observed && l.variables == r.variables;
                      });
}

}  // namespace

bool PlotCacheGuard::needsRebuild(const std::vector<DataPoint>& points,
                                  const std::string& plotVariable,
                                  const std::string& formulaName) const
{
    return !m_built
        || plotVariable != m_plotVariable
        || formulaName != m_formulaName
        || !samePoints(points, m_points);
}

void PlotCacheGuard::markBuilt(const std::vector<DataPoint>& points,
                               const std::string& plotVariable,
                               const std::string& formulaName)
{
    m_built = true;
    m_points = points;
    m_plotVariable = plotVariable;
    m_formulaName = formulaName;
}

}  // namespace Vestige

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_plot_cache_guard.cpp
/// @brief 3D_E-0874 lane-12 workbench.cpp 675 — the Workbench's plot cache
///        is rebuilt when, and only when, what it was built from changes.
///
/// The old rule compared the plotted point count with the total, so a point
/// without the plotted variable rebuilt the cache every frame, and editing a
/// value in place (the data editor's InputFloat) never rebuilt it.
#include "plot_cache_guard.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace Vestige;

namespace
{

DataPoint point(float x, float y, float observed)
{
    DataPoint dp;
    dp.variables["x"] = x;
    dp.variables["y"] = y;
    dp.observed = observed;
    return dp;
}

std::vector<DataPoint> twoPoints()
{
    return {point(1.0f, 2.0f, 3.0f), point(4.0f, 5.0f, 6.0f)};
}

}  // namespace

TEST(PlotCacheGuard, NeedsABuildBeforeTheFirst)
{
    const PlotCacheGuard guard;
    EXPECT_TRUE(guard.needsRebuild(twoPoints(), "x", "f"));
}

TEST(PlotCacheGuard, UnchangedInputsNeedNoRebuild)
{
    PlotCacheGuard guard;
    const auto points = twoPoints();
    guard.markBuilt(points, "x", "f");
    EXPECT_FALSE(guard.needsRebuild(points, "x", "f"));
}

TEST(PlotCacheGuard, APointWithoutThePlottedVariableDoesNotRebuildEveryFrame)
{
    PlotCacheGuard guard;
    auto points = twoPoints();
    DataPoint noX;
    noX.variables["y"] = 7.0f;
    noX.observed = 8.0f;
    points.push_back(noX);

    guard.markBuilt(points, "x", "f");
    EXPECT_FALSE(guard.needsRebuild(points, "x", "f"))
        << "a point lacking the plotted variable made the cache rebuild on every frame";
}

TEST(PlotCacheGuard, EditingAValueInPlaceRebuilds)
{
    PlotCacheGuard guard;
    auto points = twoPoints();
    guard.markBuilt(points, "x", "f");

    points[0].observed = 30.0f;
    EXPECT_TRUE(guard.needsRebuild(points, "x", "f"))
        << "an edited observed value left the plot stale";

    guard.markBuilt(points, "x", "f");
    points[1].variables["x"] = 40.0f;
    EXPECT_TRUE(guard.needsRebuild(points, "x", "f"))
        << "an edited plotted value left the plot stale";

    // Residuals evaluate every input, so an unplotted one counts too.
    guard.markBuilt(points, "x", "f");
    points[1].variables["y"] = 50.0f;
    EXPECT_TRUE(guard.needsRebuild(points, "x", "f"))
        << "an edited unplotted input left the residuals stale";
}

TEST(PlotCacheGuard, AnotherVariableOrFormulaRebuilds)
{
    PlotCacheGuard guard;
    const auto points = twoPoints();
    guard.markBuilt(points, "x", "f");
    EXPECT_TRUE(guard.needsRebuild(points, "y", "f"));
    EXPECT_TRUE(guard.needsRebuild(points, "x", "g"));
}

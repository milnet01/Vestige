// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_navigation_panel.cpp
/// @brief Unit tests for the editor's NavigationPanel.
///
/// These cover the no-GL behaviour of the panel: initial state, toggle
/// behaviour, and the polygon-edge extractor on NavMeshBuilder. The
/// ImGui draw path itself isn't exercised because ImGui requires a GL
/// context; that's covered by manual editor-launch verification.

#include <gtest/gtest.h>

#include "editor/panels/navigation_panel.h"
#include "navigation/nav_mesh_builder.h"

using namespace Vestige;

TEST(NavigationPanel, DefaultsAreClosedAndNotVisualising)
{
    NavigationPanel panel;
    EXPECT_FALSE(panel.isOpen());
    EXPECT_FALSE(panel.isVisualizationEnabled());
}

TEST(NavigationPanel, SetOpenTogglesVisibility)
{
    NavigationPanel panel;
    panel.setOpen(true);
    EXPECT_TRUE(panel.isOpen());
    panel.setOpen(false);
    EXPECT_FALSE(panel.isOpen());
}

TEST(NavigationPanel, OverlayLiftDefaultIsAboveZero)
{
    // Lift must be > 0 so the overlay sits above coincident ground geometry
    // without z-fighting. Exact value is a tunable; we just guard against a
    // future refactor accidentally setting it back to 0.
    NavigationPanel panel;
    EXPECT_GT(panel.getOverlayLift(), 0.0f);
}

TEST(NavigationPanel, OverlayColorIsInZeroToOneRange)
{
    NavigationPanel panel;
    const auto& c = panel.getOverlayColor();
    EXPECT_GE(c.r, 0.0f); EXPECT_LE(c.r, 1.0f);
    EXPECT_GE(c.g, 0.0f); EXPECT_LE(c.g, 1.0f);
    EXPECT_GE(c.b, 0.0f); EXPECT_LE(c.b, 1.0f);
}

TEST(NavMeshBuilder, ExtractPolygonEdgesEmptyWhenNoMesh)
{
    NavMeshBuilder builder;
    ASSERT_FALSE(builder.hasMesh());

    std::vector<glm::vec3> segs;
    builder.extractPolygonEdges(segs);
    EXPECT_TRUE(segs.empty());
}

TEST(NavMeshBuilder, ExtractPolygonEdgesAppendsRatherThanClears)
{
    // Caller-supplied buffer must be appended to, not overwritten.
    NavMeshBuilder builder;
    std::vector<glm::vec3> segs = {glm::vec3(1.0f), glm::vec3(2.0f)};
    builder.extractPolygonEdges(segs);
    ASSERT_EQ(segs.size(), 2u);  // Empty mesh: no new entries appended.
    EXPECT_EQ(segs[0], glm::vec3(1.0f));
    EXPECT_EQ(segs[1], glm::vec3(2.0f));
}

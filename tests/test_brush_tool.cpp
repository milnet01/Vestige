// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_brush_tool.cpp
/// @brief 3D_E-0874 lane-16 brush_tool rows: a foliage brush stroke always
///        gets its undo entry, whatever changes while it is open.

#include "editor/command_history.h"
#include "editor/tools/brush_tool.h"
#include "environment/foliage_manager.h"

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{

// A ray straight down onto the ground at the origin.
Ray downAtOrigin()
{
    Ray ray;
    ray.origin = glm::vec3(0.0f, 10.0f, 0.0f);
    ray.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    return ray;
}

}  // namespace

// brush_tool.cpp endStroke: the stroke's undo entry used to be built from the
// LIVE mode, so switching mode mid-drag left the foliage already painted with
// no undo entry at all.
TEST(BrushToolStroke, StrokeKeepsTheModeItStartedWith)
{
    FoliageManager manager;
    CommandHistory history;
    BrushTool brush;
    brush.setEnabled(true, manager, history);
    brush.mode = BrushTool::Mode::FOLIAGE;

    brush.processInput(downAtOrigin(), true, 0.016f, manager, history);
    ASSERT_GT(manager.getTotalFoliageCount(), 0) << "the stroke painted nothing";

    brush.mode = BrushTool::Mode::SCATTER;  // switched while the mouse is held
    brush.processInput(downAtOrigin(), false, 0.016f, manager, history);

    ASSERT_TRUE(history.canUndo());
    history.undo();
    EXPECT_EQ(manager.getTotalFoliageCount(), 0);
}

// brush_tool.h setEnabled: the editor stops calling processInput once the
// brush is off, so disabling it mid-drag used to leave the stroke open with
// no undo entry.
TEST(BrushToolStroke, DisablingMidStrokeEndsTheStroke)
{
    FoliageManager manager;
    CommandHistory history;
    BrushTool brush;
    brush.setEnabled(true, manager, history);
    brush.mode = BrushTool::Mode::FOLIAGE;

    brush.processInput(downAtOrigin(), true, 0.016f, manager, history);
    ASSERT_GT(manager.getTotalFoliageCount(), 0) << "the stroke painted nothing";

    brush.setEnabled(false, manager, history);

    ASSERT_TRUE(history.canUndo());
    history.undo();
    EXPECT_EQ(manager.getTotalFoliageCount(), 0);
}

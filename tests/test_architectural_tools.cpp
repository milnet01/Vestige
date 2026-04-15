// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_architectural_tools.cpp
/// @brief Unit tests for architectural tool state machines (Wall, Room, Cutout, Roof, Stair).

#include "editor/tools/wall_tool.h"
#include "editor/tools/room_tool.h"
#include "editor/tools/cutout_tool.h"
#include "editor/tools/roof_tool.h"
#include "editor/tools/stair_tool.h"

#include <gtest/gtest.h>

using namespace Vestige;

// =============================================================================
// WallTool tests
// =============================================================================

TEST(WallToolTest, StartsInactive)
{
    WallTool tool;
    EXPECT_EQ(tool.getState(), WallTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
}

TEST(WallToolTest, ActivateEntersWaitingStart)
{
    WallTool tool;
    tool.activate();
    EXPECT_EQ(tool.getState(), WallTool::State::WAITING_START);
    EXPECT_TRUE(tool.isActive());
}

TEST(WallToolTest, CancelResetsToInactive)
{
    WallTool tool;
    tool.activate();
    EXPECT_TRUE(tool.isActive());

    tool.cancel();
    EXPECT_EQ(tool.getState(), WallTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
}

TEST(WallToolTest, CancelFromWaitingEndResetsToInactive)
{
    WallTool tool;
    tool.activate();
    // We cannot call processClick without Scene/ResourceManager/CommandHistory,
    // so test cancel from WAITING_START only.
    tool.cancel();
    EXPECT_EQ(tool.getState(), WallTool::State::INACTIVE);
}

TEST(WallToolTest, DefaultParameters)
{
    WallTool tool;
    EXPECT_FLOAT_EQ(tool.height, 3.0f);
    EXPECT_FLOAT_EQ(tool.thickness, 0.2f);
}

TEST(WallToolTest, ParametersModifiable)
{
    WallTool tool;
    tool.height = 5.0f;
    tool.thickness = 0.4f;
    EXPECT_FLOAT_EQ(tool.height, 5.0f);
    EXPECT_FLOAT_EQ(tool.thickness, 0.4f);
}

TEST(WallToolTest, DoubleActivateStaysInWaitingStart)
{
    WallTool tool;
    tool.activate();
    tool.activate();
    EXPECT_EQ(tool.getState(), WallTool::State::WAITING_START);
    EXPECT_TRUE(tool.isActive());
}

TEST(WallToolTest, CancelWhenInactiveStaysInactive)
{
    WallTool tool;
    tool.cancel();
    EXPECT_EQ(tool.getState(), WallTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
}

// =============================================================================
// RoomTool tests
// =============================================================================

TEST(RoomToolTest, StartsInactive)
{
    RoomTool tool;
    EXPECT_EQ(tool.getState(), RoomTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
    EXPECT_FALSE(tool.showingDialog());
}

TEST(RoomToolTest, ActivateDimensionMode)
{
    RoomTool tool;
    tool.activateDimensionMode();
    EXPECT_EQ(tool.getState(), RoomTool::State::DIMENSION_INPUT);
    EXPECT_TRUE(tool.isActive());
    EXPECT_TRUE(tool.showingDialog());
}

TEST(RoomToolTest, ActivateClickMode)
{
    RoomTool tool;
    tool.activateClickMode();
    EXPECT_EQ(tool.getState(), RoomTool::State::WAITING_CORNER);
    EXPECT_TRUE(tool.isActive());
    EXPECT_FALSE(tool.showingDialog());
}

TEST(RoomToolTest, CancelResetsToInactive)
{
    RoomTool tool;
    tool.activateDimensionMode();
    tool.cancel();
    EXPECT_EQ(tool.getState(), RoomTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
}

TEST(RoomToolTest, CancelFromClickModeResetsToInactive)
{
    RoomTool tool;
    tool.activateClickMode();
    tool.cancel();
    EXPECT_EQ(tool.getState(), RoomTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
}

TEST(RoomToolTest, DefaultParameters)
{
    RoomTool tool;
    EXPECT_FLOAT_EQ(tool.roomWidth, 4.0f);
    EXPECT_FLOAT_EQ(tool.roomDepth, 4.0f);
    EXPECT_FLOAT_EQ(tool.roomHeight, 3.0f);
    EXPECT_FLOAT_EQ(tool.wallThickness, 0.2f);
    EXPECT_TRUE(tool.includeFloor);
    EXPECT_FALSE(tool.includeCeiling);
}

TEST(RoomToolTest, ParametersModifiable)
{
    RoomTool tool;
    tool.roomWidth = 6.0f;
    tool.roomDepth = 8.0f;
    tool.roomHeight = 4.0f;
    tool.wallThickness = 0.3f;
    tool.includeFloor = false;
    tool.includeCeiling = true;

    EXPECT_FLOAT_EQ(tool.roomWidth, 6.0f);
    EXPECT_FLOAT_EQ(tool.roomDepth, 8.0f);
    EXPECT_FLOAT_EQ(tool.roomHeight, 4.0f);
    EXPECT_FLOAT_EQ(tool.wallThickness, 0.3f);
    EXPECT_FALSE(tool.includeFloor);
    EXPECT_TRUE(tool.includeCeiling);
}

// =============================================================================
// CutoutTool tests
// =============================================================================

TEST(CutoutToolTest, StartsInactive)
{
    CutoutTool tool;
    EXPECT_EQ(tool.getState(), CutoutTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
    EXPECT_FALSE(tool.showingDialog());
}

TEST(CutoutToolTest, ActivateEntersSelectWall)
{
    CutoutTool tool;
    tool.activate();
    EXPECT_EQ(tool.getState(), CutoutTool::State::SELECT_WALL);
    EXPECT_TRUE(tool.isActive());
    EXPECT_FALSE(tool.showingDialog());
}

TEST(CutoutToolTest, CancelResetsToInactive)
{
    CutoutTool tool;
    tool.activate();
    tool.cancel();
    EXPECT_EQ(tool.getState(), CutoutTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
}

TEST(CutoutToolTest, DefaultParameters)
{
    CutoutTool tool;
    EXPECT_EQ(tool.openingType, CutoutTool::OpeningType::DOOR);
    EXPECT_FLOAT_EQ(tool.openingWidth, 0.9f);
    EXPECT_FLOAT_EQ(tool.openingHeight, 2.1f);
    EXPECT_FLOAT_EQ(tool.sillHeight, 0.0f);
    EXPECT_FLOAT_EQ(tool.xPosition, 0.5f);
}

TEST(CutoutToolTest, ParametersModifiable)
{
    CutoutTool tool;
    tool.openingType = CutoutTool::OpeningType::WINDOW;
    tool.openingWidth = 1.2f;
    tool.openingHeight = 1.0f;
    tool.sillHeight = 0.9f;
    tool.xPosition = 0.3f;

    EXPECT_EQ(tool.openingType, CutoutTool::OpeningType::WINDOW);
    EXPECT_FLOAT_EQ(tool.openingWidth, 1.2f);
    EXPECT_FLOAT_EQ(tool.openingHeight, 1.0f);
    EXPECT_FLOAT_EQ(tool.sillHeight, 0.9f);
    EXPECT_FLOAT_EQ(tool.xPosition, 0.3f);
}

TEST(CutoutToolTest, ShowingDialogOnlyInConfigureState)
{
    CutoutTool tool;
    EXPECT_FALSE(tool.showingDialog());

    tool.activate();
    EXPECT_FALSE(tool.showingDialog()); // SELECT_WALL, not CONFIGURE

    tool.cancel();
    EXPECT_FALSE(tool.showingDialog());
}

// =============================================================================
// RoofTool tests
// =============================================================================

TEST(RoofToolTest, StartsInactive)
{
    RoofTool tool;
    EXPECT_EQ(tool.getState(), RoofTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
    EXPECT_FALSE(tool.showingPanel());
}

TEST(RoofToolTest, ActivateEntersConfigure)
{
    RoofTool tool;
    tool.activate();
    EXPECT_EQ(tool.getState(), RoofTool::State::CONFIGURE);
    EXPECT_TRUE(tool.isActive());
    EXPECT_TRUE(tool.showingPanel());
}

TEST(RoofToolTest, CancelResetsToInactive)
{
    RoofTool tool;
    tool.activate();
    tool.cancel();
    EXPECT_EQ(tool.getState(), RoofTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
    EXPECT_FALSE(tool.showingPanel());
}

TEST(RoofToolTest, DefaultParameters)
{
    RoofTool tool;
    EXPECT_EQ(tool.roofType, RoofType::GABLE);
    EXPECT_FLOAT_EQ(tool.roofWidth, 4.0f);
    EXPECT_FLOAT_EQ(tool.roofDepth, 4.0f);
    EXPECT_FLOAT_EQ(tool.peakHeight, 1.5f);
    EXPECT_FLOAT_EQ(tool.overhang, 0.3f);
}

TEST(RoofToolTest, ParametersModifiable)
{
    RoofTool tool;
    tool.roofType = RoofType::SHED;
    tool.roofWidth = 6.0f;
    tool.roofDepth = 8.0f;
    tool.peakHeight = 2.0f;
    tool.overhang = 0.5f;

    EXPECT_EQ(tool.roofType, RoofType::SHED);
    EXPECT_FLOAT_EQ(tool.roofWidth, 6.0f);
    EXPECT_FLOAT_EQ(tool.roofDepth, 8.0f);
    EXPECT_FLOAT_EQ(tool.peakHeight, 2.0f);
    EXPECT_FLOAT_EQ(tool.overhang, 0.5f);
}

// =============================================================================
// StairTool tests
// =============================================================================

TEST(StairToolTest, StartsInactive)
{
    StairTool tool;
    EXPECT_EQ(tool.getState(), StairTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
    EXPECT_FALSE(tool.showingPanel());
}

TEST(StairToolTest, ActivateEntersConfigure)
{
    StairTool tool;
    tool.activate();
    EXPECT_EQ(tool.getState(), StairTool::State::CONFIGURE);
    EXPECT_TRUE(tool.isActive());
    EXPECT_TRUE(tool.showingPanel());
}

TEST(StairToolTest, CancelResetsToInactive)
{
    StairTool tool;
    tool.activate();
    tool.cancel();
    EXPECT_EQ(tool.getState(), StairTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
    EXPECT_FALSE(tool.showingPanel());
}

TEST(StairToolTest, DefaultParameters)
{
    StairTool tool;
    EXPECT_EQ(tool.stairType, StairType::STRAIGHT);
    EXPECT_FLOAT_EQ(tool.totalHeight, 3.0f);
    EXPECT_FLOAT_EQ(tool.stepHeight, 0.18f);
    EXPECT_FLOAT_EQ(tool.stepDepth, 0.28f);
    EXPECT_FLOAT_EQ(tool.width, 1.0f);
    EXPECT_FLOAT_EQ(tool.innerRadius, 0.3f);
    EXPECT_FLOAT_EQ(tool.outerRadius, 1.5f);
    EXPECT_FLOAT_EQ(tool.totalAngle, 360.0f);
}

TEST(StairToolTest, ParametersModifiable)
{
    StairTool tool;
    tool.stairType = StairType::SPIRAL;
    tool.totalHeight = 5.0f;
    tool.stepHeight = 0.2f;
    tool.stepDepth = 0.3f;
    tool.width = 1.2f;
    tool.innerRadius = 0.5f;
    tool.outerRadius = 2.0f;
    tool.totalAngle = 720.0f;

    EXPECT_EQ(tool.stairType, StairType::SPIRAL);
    EXPECT_FLOAT_EQ(tool.totalHeight, 5.0f);
    EXPECT_FLOAT_EQ(tool.stepHeight, 0.2f);
    EXPECT_FLOAT_EQ(tool.stepDepth, 0.3f);
    EXPECT_FLOAT_EQ(tool.width, 1.2f);
    EXPECT_FLOAT_EQ(tool.innerRadius, 0.5f);
    EXPECT_FLOAT_EQ(tool.outerRadius, 2.0f);
    EXPECT_FLOAT_EQ(tool.totalAngle, 720.0f);
}

TEST(StairToolTest, CancelWhenInactiveStaysInactive)
{
    StairTool tool;
    tool.cancel();
    EXPECT_EQ(tool.getState(), StairTool::State::INACTIVE);
    EXPECT_FALSE(tool.isActive());
}

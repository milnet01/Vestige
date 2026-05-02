// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_architectural_tools.cpp
/// @brief Unit tests for architectural tool state machines (Wall, Room, Cutout, Roof, Stair).
///
/// The four lifecycle contracts shared by every architectural tool —
/// starts-inactive, activate-makes-active, cancel-after-activate-resets,
/// cancel-when-inactive-is-noop — are exercised once via a TYPED_TEST
/// suite parametrised over a `ToolTraits<T>` table. Adding a new tool
/// only needs a new traits specialisation; all four contracts apply
/// automatically. Tool-specific tests (default parameters, modifiable
/// fields, mode variants) remain as ordinary `TEST` blocks below.

#include "editor/tools/wall_tool.h"
#include "editor/tools/room_tool.h"
#include "editor/tools/cutout_tool.h"
#include "editor/tools/roof_tool.h"
#include "editor/tools/stair_tool.h"

#include <gtest/gtest.h>

using namespace Vestige;

// =============================================================================
// Shared lifecycle contract — TYPED_TEST over all architectural tools
// =============================================================================

namespace {

/// @brief Per-tool traits: how to canonically activate the tool and what
///        state is expected before / after.
///
/// `RoomTool` has no plain `activate()` — it has `activateClickMode()` and
/// `activateDimensionMode()`. The traits pick `activateClickMode()` as the
/// canonical activation for the shared contract; `activateDimensionMode()`
/// is exercised by a tool-specific TEST below.
template <typename T>
struct ToolTraits;

template <>
struct ToolTraits<WallTool>
{
    static void activate(WallTool& t) { t.activate(); }
    static auto activeState() { return WallTool::State::WAITING_START; }
    static auto inactiveState() { return WallTool::State::INACTIVE; }
};

template <>
struct ToolTraits<RoomTool>
{
    static void activate(RoomTool& t) { t.activateClickMode(); }
    static auto activeState() { return RoomTool::State::WAITING_CORNER; }
    static auto inactiveState() { return RoomTool::State::INACTIVE; }
};

template <>
struct ToolTraits<CutoutTool>
{
    static void activate(CutoutTool& t) { t.activate(); }
    static auto activeState() { return CutoutTool::State::SELECT_WALL; }
    static auto inactiveState() { return CutoutTool::State::INACTIVE; }
};

template <>
struct ToolTraits<RoofTool>
{
    static void activate(RoofTool& t) { t.activate(); }
    static auto activeState() { return RoofTool::State::CONFIGURE; }
    static auto inactiveState() { return RoofTool::State::INACTIVE; }
};

template <>
struct ToolTraits<StairTool>
{
    static void activate(StairTool& t) { t.activate(); }
    static auto activeState() { return StairTool::State::CONFIGURE; }
    static auto inactiveState() { return StairTool::State::INACTIVE; }
};

template <typename T>
class ArchitecturalToolLifecycle : public ::testing::Test {};

using AllArchitecturalTools = ::testing::Types<
    WallTool, RoomTool, CutoutTool, RoofTool, StairTool>;

}  // namespace

TYPED_TEST_SUITE(ArchitecturalToolLifecycle, AllArchitecturalTools);

TYPED_TEST(ArchitecturalToolLifecycle, StartsInactive)
{
    TypeParam tool;
    EXPECT_EQ(tool.getState(), ToolTraits<TypeParam>::inactiveState());
    EXPECT_FALSE(tool.isActive());
}

TYPED_TEST(ArchitecturalToolLifecycle, ActivateMakesActive)
{
    TypeParam tool;
    ToolTraits<TypeParam>::activate(tool);
    EXPECT_EQ(tool.getState(), ToolTraits<TypeParam>::activeState());
    EXPECT_TRUE(tool.isActive());
}

TYPED_TEST(ArchitecturalToolLifecycle, CancelAfterActivateResetsToInactive)
{
    TypeParam tool;
    ToolTraits<TypeParam>::activate(tool);
    EXPECT_TRUE(tool.isActive());

    tool.cancel();
    EXPECT_EQ(tool.getState(), ToolTraits<TypeParam>::inactiveState());
    EXPECT_FALSE(tool.isActive());
}

TYPED_TEST(ArchitecturalToolLifecycle, CancelWhenInactiveIsNoOp)
{
    TypeParam tool;
    tool.cancel();
    EXPECT_EQ(tool.getState(), ToolTraits<TypeParam>::inactiveState());
    EXPECT_FALSE(tool.isActive());
}

// =============================================================================
// WallTool — tool-specific tests
// =============================================================================

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

// =============================================================================
// RoomTool — tool-specific tests (mode variants + parameters)
// =============================================================================

TEST(RoomToolTest, ActivateDimensionModeShowsDialog)
{
    RoomTool tool;
    tool.activateDimensionMode();
    EXPECT_EQ(tool.getState(), RoomTool::State::DIMENSION_INPUT);
    EXPECT_TRUE(tool.isActive());
    EXPECT_TRUE(tool.showingDialog());
}

TEST(RoomToolTest, ActivateClickModeDoesNotShowDialog)
{
    RoomTool tool;
    tool.activateClickMode();
    EXPECT_FALSE(tool.showingDialog());
}

TEST(RoomToolTest, StartsWithNoDialog)
{
    RoomTool tool;
    EXPECT_FALSE(tool.showingDialog());
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
// CutoutTool — tool-specific tests
// =============================================================================

TEST(CutoutToolTest, StartsWithNoDialog)
{
    CutoutTool tool;
    EXPECT_FALSE(tool.showingDialog());
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
// RoofTool — tool-specific tests
// =============================================================================

TEST(RoofToolTest, StartsWithNoPanel)
{
    RoofTool tool;
    EXPECT_FALSE(tool.showingPanel());
}

TEST(RoofToolTest, ActivateShowsPanel)
{
    RoofTool tool;
    tool.activate();
    EXPECT_TRUE(tool.showingPanel());
}

TEST(RoofToolTest, CancelHidesPanel)
{
    RoofTool tool;
    tool.activate();
    tool.cancel();
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
// StairTool — tool-specific tests
// =============================================================================

TEST(StairToolTest, StartsWithNoPanel)
{
    StairTool tool;
    EXPECT_FALSE(tool.showingPanel());
}

TEST(StairToolTest, ActivateShowsPanel)
{
    StairTool tool;
    tool.activate();
    EXPECT_TRUE(tool.showingPanel());
}

TEST(StairToolTest, CancelHidesPanel)
{
    StairTool tool;
    tool.activate();
    tool.cancel();
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

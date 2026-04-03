/// @file test_pressure_plate.cpp
/// @brief Unit tests for PressurePlateComponent configuration and state.
/// @note Physics overlap queries require a full Jolt setup and are not tested here.

#include "scene/pressure_plate_component.h"

#include <gtest/gtest.h>

using namespace Vestige;

// =============================================================================
// Default construction
// =============================================================================

TEST(PressurePlateTest, DefaultNotActivated)
{
    PressurePlateComponent plate;
    EXPECT_FALSE(plate.isActivated());
}

TEST(PressurePlateTest, DefaultDetectionRadius)
{
    PressurePlateComponent plate;
    EXPECT_FLOAT_EQ(plate.detectionRadius, 1.0f);
}

TEST(PressurePlateTest, DefaultDetectionHeight)
{
    PressurePlateComponent plate;
    EXPECT_FLOAT_EQ(plate.detectionHeight, 0.5f);
}

TEST(PressurePlateTest, DefaultQueryInterval)
{
    PressurePlateComponent plate;
    EXPECT_FLOAT_EQ(plate.queryInterval, 0.1f);
}

TEST(PressurePlateTest, DefaultNotInverted)
{
    PressurePlateComponent plate;
    EXPECT_FALSE(plate.inverted);
}

TEST(PressurePlateTest, DefaultOverlapCountZero)
{
    PressurePlateComponent plate;
    EXPECT_EQ(plate.getOverlapCount(), 0u);
}

TEST(PressurePlateTest, DefaultEnabled)
{
    PressurePlateComponent plate;
    EXPECT_TRUE(plate.isEnabled());
}

// =============================================================================
// Configuration setters
// =============================================================================

TEST(PressurePlateTest, SetDetectionRadius)
{
    PressurePlateComponent plate;
    plate.detectionRadius = 2.5f;
    EXPECT_FLOAT_EQ(plate.detectionRadius, 2.5f);
}

TEST(PressurePlateTest, SetDetectionHeight)
{
    PressurePlateComponent plate;
    plate.detectionHeight = 1.0f;
    EXPECT_FLOAT_EQ(plate.detectionHeight, 1.0f);
}

TEST(PressurePlateTest, SetQueryInterval)
{
    PressurePlateComponent plate;
    plate.queryInterval = 0.5f;
    EXPECT_FLOAT_EQ(plate.queryInterval, 0.5f);
}

TEST(PressurePlateTest, SetInverted)
{
    PressurePlateComponent plate;
    plate.inverted = true;
    EXPECT_TRUE(plate.inverted);
}

TEST(PressurePlateTest, SetEnabled)
{
    PressurePlateComponent plate;
    plate.setEnabled(false);
    EXPECT_FALSE(plate.isEnabled());

    plate.setEnabled(true);
    EXPECT_TRUE(plate.isEnabled());
}

// =============================================================================
// Clone behavior
// =============================================================================

TEST(PressurePlateTest, CloneReturnsNonNull)
{
    PressurePlateComponent plate;
    auto cloned = plate.clone();
    EXPECT_NE(cloned, nullptr);
}

TEST(PressurePlateTest, CloneCopiesConfiguration)
{
    PressurePlateComponent plate;
    plate.detectionRadius = 3.0f;
    plate.detectionHeight = 1.5f;
    plate.queryInterval = 0.25f;
    plate.inverted = true;

    auto cloned = plate.clone();
    ASSERT_NE(cloned, nullptr);

    auto* clonedPlate = dynamic_cast<PressurePlateComponent*>(cloned.get());
    ASSERT_NE(clonedPlate, nullptr);

    EXPECT_FLOAT_EQ(clonedPlate->detectionRadius, 3.0f);
    EXPECT_FLOAT_EQ(clonedPlate->detectionHeight, 1.5f);
    EXPECT_FLOAT_EQ(clonedPlate->queryInterval, 0.25f);
    EXPECT_TRUE(clonedPlate->inverted);
}

TEST(PressurePlateTest, CloneDoesNotCopyRuntimeState)
{
    PressurePlateComponent plate;
    // Runtime state (activated, overlapping bodies) should not be copied.
    // Since we cannot trigger activation without a physics world, verify
    // that the clone starts in the default unactivated state.
    auto cloned = plate.clone();
    auto* clonedPlate = dynamic_cast<PressurePlateComponent*>(cloned.get());
    ASSERT_NE(clonedPlate, nullptr);

    EXPECT_FALSE(clonedPlate->isActivated());
    EXPECT_EQ(clonedPlate->getOverlapCount(), 0u);
}

TEST(PressurePlateTest, CloneDoesNotCopyCallbacks)
{
    PressurePlateComponent plate;
    bool callbackFired = false;
    plate.onActivate = [&callbackFired]() { callbackFired = true; };

    auto cloned = plate.clone();
    auto* clonedPlate = dynamic_cast<PressurePlateComponent*>(cloned.get());
    ASSERT_NE(clonedPlate, nullptr);

    // Cloned plate should not have the callback
    EXPECT_FALSE(static_cast<bool>(clonedPlate->onActivate));
    EXPECT_FALSE(callbackFired);
}

// =============================================================================
// Callbacks can be set
// =============================================================================

TEST(PressurePlateTest, CallbacksAssignable)
{
    PressurePlateComponent plate;
    bool activated = false;
    bool deactivated = false;

    plate.onActivate = [&activated]() { activated = true; };
    plate.onDeactivate = [&deactivated]() { deactivated = true; };

    EXPECT_TRUE(static_cast<bool>(plate.onActivate));
    EXPECT_TRUE(static_cast<bool>(plate.onDeactivate));
}

// =============================================================================
// Update without physics world does not crash
// =============================================================================

TEST(PressurePlateTest, UpdateWithoutPhysicsWorldDoesNotCrash)
{
    PressurePlateComponent plate;
    // No physics world set, no owner set -- should early-return safely
    EXPECT_NO_THROW(plate.update(0.016f));
    EXPECT_FALSE(plate.isActivated());
}

TEST(PressurePlateTest, UpdateDisabledDoesNotCrash)
{
    PressurePlateComponent plate;
    plate.setEnabled(false);
    EXPECT_NO_THROW(plate.update(0.016f));
}

TEST(PressurePlateTest, SetPhysicsWorldToNull)
{
    PressurePlateComponent plate;
    plate.setPhysicsWorld(nullptr);
    EXPECT_NO_THROW(plate.update(0.016f));
}

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_pressure_plate.cpp
/// @brief Unit tests for PressurePlateComponent configuration and state.
/// @note Physics overlap queries require a full Jolt setup and are not tested here.

#include "scene/entity.h"
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

// =============================================================================
// Phase 10.9 Slice 3 S6 — computePressurePlateCenter uses the entity's
// world position, not its local transform. A plate parented under
// another entity must fire the trigger at its rendered location,
// not at the parent's origin.
// =============================================================================

TEST(PressurePlateCenter, UnparentedEntityPlacesCenterAboveLocalPosition_S6)
{
    Entity plateEntity("Plate");
    plateEntity.transform.position = glm::vec3(2.0f, 3.0f, 4.0f);

    const glm::vec3 c = computePressurePlateCenter(plateEntity, 0.5f);

    EXPECT_FLOAT_EQ(c.x, 2.0f);
    EXPECT_FLOAT_EQ(c.y, 3.5f);  // 3.0 + 0.5 detectionHeight
    EXPECT_FLOAT_EQ(c.z, 4.0f);
}

TEST(PressurePlateCenter, ChildOfTranslatedParentUsesWorldPosition_S6)
{
    // Parent at (10, 0, 0). Child at local (2, 3, 4). World = (12, 3, 4).
    // With detectionHeight = 0.5, centre must be (12, 3.5, 4).
    Entity parent("Parent");
    parent.transform.position = glm::vec3(10.0f, 0.0f, 0.0f);
    Entity* child =
        parent.addChild(std::make_unique<Entity>("PlateChild"));
    child->transform.position = glm::vec3(2.0f, 3.0f, 4.0f);

    const glm::vec3 c = computePressurePlateCenter(*child, 0.5f);

    EXPECT_FLOAT_EQ(c.x, 12.0f)
        << "parent translation must cascade into the plate's overlap-query "
           "centre — a plate parented under a moving rig was firing its "
           "trigger at the rig's origin";
    EXPECT_FLOAT_EQ(c.y, 3.5f);
    EXPECT_FLOAT_EQ(c.z, 4.0f);
}

TEST(PressurePlateCenter, GrandchildCascadesThroughFullHierarchy_S6)
{
    // Grandparent (100, 0, 0) → parent (local 10, 0, 0) → plate (local 2, 3, 4).
    // Plate world = (112, 3, 4). Centre with height 0.5 = (112, 3.5, 4).
    Entity grandparent("GP");
    grandparent.transform.position = glm::vec3(100.0f, 0.0f, 0.0f);
    Entity* parent =
        grandparent.addChild(std::make_unique<Entity>("Parent"));
    parent->transform.position = glm::vec3(10.0f, 0.0f, 0.0f);
    Entity* plate =
        parent->addChild(std::make_unique<Entity>("Plate"));
    plate->transform.position = glm::vec3(2.0f, 3.0f, 4.0f);

    const glm::vec3 c = computePressurePlateCenter(*plate, 0.5f);

    EXPECT_FLOAT_EQ(c.x, 112.0f);
    EXPECT_FLOAT_EQ(c.y, 3.5f);
    EXPECT_FLOAT_EQ(c.z, 4.0f);
}

TEST(PressurePlateCenter, ZeroDetectionHeightPutsCenterAtWorldPosition_S6)
{
    Entity plateEntity("Flat");
    plateEntity.transform.position = glm::vec3(5.0f, 0.0f, -2.0f);

    const glm::vec3 c = computePressurePlateCenter(plateEntity, 0.0f);

    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 0.0f);
    EXPECT_FLOAT_EQ(c.z, -2.0f);
}

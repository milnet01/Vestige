// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_stasis_system.cpp
/// @brief Unit tests for StasisSystem state management.
/// @note Actual physics freeze/restore requires a full Jolt setup and is not tested here.

#include "physics/stasis_system.h"

#include <gtest/gtest.h>

using namespace Vestige;

// =============================================================================
// Initial state
// =============================================================================

TEST(StasisSystemTest, InitialActiveCountIsZero)
{
    StasisSystem system;
    EXPECT_EQ(system.getActiveCount(), 0u);
}

TEST(StasisSystemTest, IsInStasisReturnsFalseForUnknownBody)
{
    StasisSystem system;
    EXPECT_FALSE(system.isInStasis(0));
    EXPECT_FALSE(system.isInStasis(42));
    EXPECT_FALSE(system.isInStasis(9999));
}

TEST(StasisSystemTest, GetRemainingDurationReturnsZeroForUnknownBody)
{
    StasisSystem system;
    EXPECT_FLOAT_EQ(system.getRemainingDuration(0), 0.0f);
    EXPECT_FLOAT_EQ(system.getRemainingDuration(42), 0.0f);
    EXPECT_FLOAT_EQ(system.getRemainingDuration(9999), 0.0f);
}

// =============================================================================
// releaseAll on empty system
// =============================================================================

TEST(StasisSystemTest, ReleaseAllOnEmptyDoesNotCrash)
{
    StasisSystem system;
    EXPECT_NO_THROW(system.releaseAll());
    EXPECT_EQ(system.getActiveCount(), 0u);
}

// =============================================================================
// releaseStasis on non-existent body
// =============================================================================

TEST(StasisSystemTest, ReleaseNonExistentBodyDoesNotCrash)
{
    StasisSystem system;
    EXPECT_NO_THROW(system.releaseStasis(42));
    EXPECT_EQ(system.getActiveCount(), 0u);
}

// =============================================================================
// applyStasis without physics world (no-op)
// =============================================================================

TEST(StasisSystemTest, ApplyStasisWithoutPhysicsWorldIsNoOp)
{
    StasisSystem system;
    // No physics world set -- applyStasis should early-return
    system.applyStasis(1, 5.0f, 0.0f);
    EXPECT_FALSE(system.isInStasis(1));
    EXPECT_EQ(system.getActiveCount(), 0u);
}

// =============================================================================
// update on empty system
// =============================================================================

TEST(StasisSystemTest, UpdateEmptySystemDoesNotCrash)
{
    StasisSystem system;
    EXPECT_NO_THROW(system.update(0.016f));
    EXPECT_EQ(system.getActiveCount(), 0u);
}

TEST(StasisSystemTest, UpdateWithoutPhysicsWorldDoesNotCrash)
{
    StasisSystem system;
    // No physics world set, m_stasisMap is empty
    EXPECT_NO_THROW(system.update(0.016f));
}

// =============================================================================
// setPhysicsWorld
// =============================================================================

TEST(StasisSystemTest, SetPhysicsWorldToNull)
{
    StasisSystem system;
    system.setPhysicsWorld(nullptr);
    // Should still not crash on operations
    EXPECT_NO_THROW(system.releaseAll());
    EXPECT_NO_THROW(system.update(0.016f));
    EXPECT_EQ(system.getActiveCount(), 0u);
}

// =============================================================================
// StasisState struct defaults
// =============================================================================

TEST(StasisStateTest, DefaultValues)
{
    StasisState state;
    EXPECT_FLOAT_EQ(state.linearVelocity.x, 0.0f);
    EXPECT_FLOAT_EQ(state.linearVelocity.y, 0.0f);
    EXPECT_FLOAT_EQ(state.linearVelocity.z, 0.0f);
    EXPECT_FLOAT_EQ(state.angularVelocity.x, 0.0f);
    EXPECT_FLOAT_EQ(state.angularVelocity.y, 0.0f);
    EXPECT_FLOAT_EQ(state.angularVelocity.z, 0.0f);
    EXPECT_FLOAT_EQ(state.timeScale, 0.0f);
    EXPECT_FLOAT_EQ(state.duration, 0.0f);
    EXPECT_FLOAT_EQ(state.elapsed, 0.0f);
}

TEST(StasisStateTest, ValuesModifiable)
{
    StasisState state;
    state.linearVelocity = glm::vec3(1.0f, 2.0f, 3.0f);
    state.angularVelocity = glm::vec3(0.5f, 0.5f, 0.5f);
    state.timeScale = 0.1f;
    state.duration = 10.0f;
    state.elapsed = 2.5f;

    EXPECT_FLOAT_EQ(state.linearVelocity.x, 1.0f);
    EXPECT_FLOAT_EQ(state.angularVelocity.y, 0.5f);
    EXPECT_FLOAT_EQ(state.timeScale, 0.1f);
    EXPECT_FLOAT_EQ(state.duration, 10.0f);
    EXPECT_FLOAT_EQ(state.elapsed, 2.5f);
}

// =============================================================================
// Multiple operations sequence (no physics world -- all no-ops but shouldn't crash)
// =============================================================================

TEST(StasisSystemTest, MultipleOperationsSequenceDoesNotCrash)
{
    StasisSystem system;

    // Apply, release, update, releaseAll in sequence without a physics world
    system.applyStasis(1, 5.0f, 0.0f);
    system.applyStasis(2, 0.0f, 0.5f);
    system.releaseStasis(1);
    system.update(0.016f);
    system.releaseAll();

    EXPECT_EQ(system.getActiveCount(), 0u);
}

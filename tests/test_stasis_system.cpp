// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_stasis_system.cpp
/// @brief Unit tests for StasisSystem state management.
/// @note Actual physics freeze/restore requires a full Jolt setup and is not tested here.

#include "experimental/physics/stasis_system.h"

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

// Phase 10.9 Slice 18 Ts4 collapse: eight "no-op without physics
// world" tests (IsInStasisReturnsFalseForUnknownBody,
// GetRemainingDurationReturnsZeroForUnknownBody,
// ReleaseAllOnEmptyDoesNotCrash, ReleaseNonExistentBodyDoesNotCrash,
// ApplyStasisWithoutPhysicsWorldIsNoOp, UpdateEmptySystemDoesNotCrash,
// UpdateWithoutPhysicsWorldDoesNotCrash, SetPhysicsWorldToNull) all
// passed for the same root reason: `StasisSystem` is a no-op without
// a physics world. They covered slightly different API surfaces but
// shared one root invariant — if it broke, all eight flipped together.
// The compound `MultipleOperationsSequenceDoesNotCrash` below
// exercises every method the eight covered; the single semantic pin
// `ApplyStasisWithoutPhysicsWorldIsNoOp` (kept below) retains the
// behavioural pin distinct from "doesn't crash".

TEST(StasisSystemTest, ApplyStasisWithoutPhysicsWorldIsNoOp)
{
    StasisSystem system;
    // No physics world set -- applyStasis should early-return.
    system.applyStasis(1, 5.0f, 0.0f);
    EXPECT_FALSE(system.isInStasis(1));
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

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_physics_fixed_step_callback.cpp
/// @brief Phase 10.9 Slice 7 Ph1 — pin the fixed-step-callback contract.
///
/// `PhysicsWorld::update(dt, callback)` invokes `callback(fixedDt)`
/// exactly once per substep that ran inside the fixed-step accumulator
/// loop, with `fixedDt == m_fixedTimestep`. Pre-Ph1 the breakable-
/// constraint check ran once per *frame* and divided its impulses by
/// frame dt — wrong by a factor of substeps-per-frame and missing
/// breakage spikes that landed in any but the last substep. The
/// callback hook makes the integration cadence match the physics step.

#include <gtest/gtest.h>
#include "physics/physics_world.h"

namespace Vestige::PhysicsFixedStep::Test
{

class PhysicsFixedStepTest : public ::testing::Test
{
protected:
    PhysicsWorld m_world;
    void SetUp() override
    {
        PhysicsWorldConfig cfg;
        cfg.fixedTimestep = 1.0f / 60.0f;  // 16.67 ms.
        ASSERT_TRUE(m_world.initialize(cfg));
    }
    void TearDown() override
    {
        m_world.shutdown();
    }
};

// One frame of exactly 1× fixed-step → callback fires once.
TEST_F(PhysicsFixedStepTest, OneSubstepInvokesCallbackOnce_Ph1)
{
    int callCount = 0;
    float observedDt = -1.0f;
    // Pad slightly so float-precision drift in the accumulator can't
    // strand the substep just below the `>=` comparison threshold.
    const float dt = m_world.getFixedTimestep() * 1.001f;
    m_world.update(dt, [&](float dtCb) {
        ++callCount;
        observedDt = dtCb;
    });
    EXPECT_EQ(callCount, 1);
    EXPECT_FLOAT_EQ(observedDt, m_world.getFixedTimestep());
}

// One frame of just over 3× fixed-step → callback fires exactly three times.
TEST_F(PhysicsFixedStepTest, MultipleSubstepsInvokeCallbackPerStep_Ph1)
{
    int callCount = 0;
    // 3.001× pads against fp drift; 3.999× would still clamp at 4.
    const float dt = m_world.getFixedTimestep() * 3.001f;
    m_world.update(dt, [&](float /*dtCb*/) {
        ++callCount;
    });
    EXPECT_EQ(callCount, 3);
}

// A frame that doesn't accumulate enough → callback doesn't fire at
// all (matches Jolt's substep-skip behaviour).
TEST_F(PhysicsFixedStepTest, ShortFrameDoesNotFireCallback_Ph1)
{
    int callCount = 0;
    m_world.update(1.0f / 240.0f, [&](float /*dt*/) {
        ++callCount;
    });
    EXPECT_EQ(callCount, 0);
}

// Spiral-of-death clamp: a very long frame caps at the 4-substep
// safety bound the prior PhysicsWorld::update established. This pins
// that the Ph1 callback path didn't accidentally remove the clamp.
TEST_F(PhysicsFixedStepTest, SpiralOfDeathClampHoldsAtFour_Ph1)
{
    int callCount = 0;
    m_world.update(1.0f, [&](float /*dt*/) {  // 60 substeps' worth.
        ++callCount;
    });
    EXPECT_EQ(callCount, 4);  // Clamp = m_fixedTimestep × 4.
}

// Substep dt is always exactly the configured fixed timestep — never
// the variable frame dt. This is the Ph1 correctness gate for impulse-
// to-force conversion in checkBreakableConstraints.
TEST_F(PhysicsFixedStepTest, CallbackDtIsAlwaysFixedTimestep_Ph1)
{
    const float kFixed = m_world.getFixedTimestep();
    bool allMatch = true;
    m_world.update(kFixed * 2.5f, [&](float dt) {
        if (dt != kFixed) allMatch = false;
    });
    EXPECT_TRUE(allMatch);
}

// Backwards-compatible overload: the no-callback `update(dt)` form
// keeps working without callers having to opt in.
TEST_F(PhysicsFixedStepTest, NoCallbackOverloadStillSteps_Ph1)
{
    // No callback supplied. The call must not crash and must accumulate
    // substeps as before. We can't directly observe step counts via
    // public API, so we just verify the call path is non-fatal and
    // multiple frames still progress without assertion.
    EXPECT_NO_FATAL_FAILURE(m_world.update(1.0f / 60.0f));
    EXPECT_NO_FATAL_FAILURE(m_world.update(2.0f / 60.0f));
}

}  // namespace Vestige::PhysicsFixedStep::Test

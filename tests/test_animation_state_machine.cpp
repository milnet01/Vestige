// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_animation_state_machine.cpp
/// @brief Unit tests for the animation state machine.
#include "animation/animation_state_machine.h"
#include "animation/skeleton_animator.h"
#include "animation/skeleton.h"
#include "animation/animation_clip.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Skeleton> makeTestSkeleton()
{
    auto skel = std::make_shared<Skeleton>();
    Joint j;
    j.name = "root";
    j.parentIndex = -1;
    j.inverseBindMatrix = glm::mat4(1.0f);
    j.localBindTransform = glm::mat4(1.0f);
    skel->m_joints.push_back(j);
    skel->m_rootJoints.push_back(0);
    return skel;
}

static std::shared_ptr<AnimationClip> makeClip(const std::string& name, float duration)
{
    auto clip = std::make_shared<AnimationClip>();
    clip->m_name = name;

    AnimationChannel ch;
    ch.jointIndex = 0;
    ch.targetPath = AnimTargetPath::TRANSLATION;
    ch.interpolation = AnimInterpolation::LINEAR;
    ch.timestamps = {0.0f, duration};
    ch.values = {0, 0, 0, 1, 0, 0};  // move along X
    clip->m_channels.push_back(ch);
    clip->computeDuration();
    return clip;
}

class StateMachineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_skel = makeTestSkeleton();
        m_idle = makeClip("idle", 2.0f);
        m_walk = makeClip("walk", 1.0f);
        m_run  = makeClip("run",  0.8f);

        m_animator.setSkeleton(m_skel);
        m_animator.addClip(m_idle);  // index 0
        m_animator.addClip(m_walk);  // index 1
        m_animator.addClip(m_run);   // index 2
    }

    std::shared_ptr<Skeleton> m_skel;
    std::shared_ptr<AnimationClip> m_idle, m_walk, m_run;
    SkeletonAnimator m_animator;
};

TEST_F(StateMachineTest, InitialStateIsNotRunning)
{
    AnimationStateMachine sm;
    EXPECT_FALSE(sm.isRunning());
    EXPECT_EQ(sm.getCurrentStateIndex(), -1);
}

TEST_F(StateMachineTest, StartEntersFirstState)
{
    AnimationStateMachine sm;
    sm.addState({"idle", 0, 1.0f, true});
    sm.start(m_animator);

    EXPECT_TRUE(sm.isRunning());
    EXPECT_EQ(sm.getCurrentStateIndex(), 0);
    EXPECT_EQ(sm.getCurrentStateName(), "idle");
    EXPECT_TRUE(m_animator.isPlaying());
}

TEST_F(StateMachineTest, TransitionFiresWhenConditionMet)
{
    AnimationStateMachine sm;
    sm.addState({"idle", 0, 1.0f, true});
    sm.addState({"walk", 1, 1.0f, true});

    AnimTransition trans;
    trans.fromState = 0;
    trans.toState = 1;
    trans.crossfadeDuration = 0.2f;
    trans.conditions.push_back({"speed", AnimCompareOp::GREATER, 0.1f});
    sm.addTransition(trans);

    sm.start(m_animator);
    m_animator.update(0.01f);

    // Speed is 0 — transition should NOT fire
    sm.setFloat("speed", 0.0f);
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 0);

    // Speed > 0.1 — transition should fire
    sm.setFloat("speed", 1.5f);
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 1);
    EXPECT_EQ(sm.getCurrentStateName(), "walk");
}

TEST_F(StateMachineTest, TransitionBlockedWhenConditionNotMet)
{
    AnimationStateMachine sm;
    sm.addState({"idle", 0, 1.0f, true});
    sm.addState({"walk", 1, 1.0f, true});

    AnimTransition trans;
    trans.fromState = 0;
    trans.toState = 1;
    trans.crossfadeDuration = 0.2f;
    trans.conditions.push_back({"speed", AnimCompareOp::GREATER, 5.0f});
    sm.addTransition(trans);

    sm.start(m_animator);
    m_animator.update(0.01f);

    sm.setFloat("speed", 3.0f);  // Not > 5.0
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 0);  // Still idle
}

TEST_F(StateMachineTest, MultipleConditionsRequireAll)
{
    AnimationStateMachine sm;
    sm.addState({"idle", 0, 1.0f, true});
    sm.addState({"run", 2, 1.0f, true});

    AnimTransition trans;
    trans.fromState = 0;
    trans.toState = 1;
    trans.crossfadeDuration = 0.2f;
    trans.conditions.push_back({"speed", AnimCompareOp::GREATER, 3.0f});
    trans.conditions.push_back({"isGrounded", AnimCompareOp::EQUAL, 1.0f});
    sm.addTransition(trans);

    sm.start(m_animator);
    m_animator.update(0.01f);

    // Only speed met, not grounded
    sm.setFloat("speed", 5.0f);
    sm.setBool("isGrounded", false);
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 0);  // Still idle

    // Both met
    sm.setBool("isGrounded", true);
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 1);  // Transitioned
}

TEST_F(StateMachineTest, AnyStateTransition)
{
    AnimationStateMachine sm;
    sm.addState({"idle", 0, 1.0f, true});
    sm.addState({"walk", 1, 1.0f, true});
    sm.addState({"run", 2, 1.0f, true});

    // Any state → run when speed > 5
    AnimTransition anyToRun;
    anyToRun.fromState = -1;  // any state
    anyToRun.toState = 2;
    anyToRun.crossfadeDuration = 0.1f;
    anyToRun.conditions.push_back({"speed", AnimCompareOp::GREATER, 5.0f});
    sm.addTransition(anyToRun);

    sm.start(m_animator);
    m_animator.update(0.01f);

    sm.setFloat("speed", 6.0f);
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 2);
    EXPECT_EQ(sm.getCurrentStateName(), "run");
}

TEST_F(StateMachineTest, TriggerConsumedAfterTransition)
{
    AnimationStateMachine sm;
    sm.addState({"idle", 0, 1.0f, true});
    sm.addState({"attack", 1, 1.0f, false});

    AnimTransition trans;
    trans.fromState = 0;
    trans.toState = 1;
    trans.crossfadeDuration = 0.1f;
    trans.conditions.push_back({"doAttack", AnimCompareOp::GREATER, 0.5f});
    sm.addTransition(trans);

    // Also add return transition
    AnimTransition returnTrans;
    returnTrans.fromState = 1;
    returnTrans.toState = 0;
    returnTrans.crossfadeDuration = 0.1f;
    returnTrans.conditions.push_back({"doAttack", AnimCompareOp::GREATER, 0.5f});
    sm.addTransition(returnTrans);

    sm.start(m_animator);
    m_animator.update(0.01f);

    sm.setTrigger("doAttack");
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 1);  // Transitioned to attack

    // Trigger should be consumed — next update should NOT transition back
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 1);  // Still in attack
}

TEST_F(StateMachineTest, ExitTimePreventsEarlyTransition)
{
    AnimationStateMachine sm;
    sm.addState({"idle", 0, 1.0f, true});   // clip duration 2.0s
    sm.addState({"walk", 1, 1.0f, true});

    AnimTransition trans;
    trans.fromState = 0;
    trans.toState = 1;
    trans.crossfadeDuration = 0.2f;
    trans.exitTime = 0.5f;  // Must be 50% through idle before transition
    trans.conditions.push_back({"speed", AnimCompareOp::GREATER, 0.1f});
    sm.addTransition(trans);

    sm.start(m_animator);
    sm.setFloat("speed", 1.0f);

    // At 0.1s into a 2.0s clip, normalized time = 0.05 < 0.5
    m_animator.update(0.1f);
    sm.update(m_animator, 0.1f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 0);  // Too early

    // Advance to ~1.1s into a 2.0s clip, normalized time = 0.55 >= 0.5
    m_animator.update(1.0f);
    sm.update(m_animator, 1.0f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 1);  // Now transitions
}

TEST_F(StateMachineTest, SelfTransitionBlocked)
{
    AnimationStateMachine sm;
    sm.addState({"idle", 0, 1.0f, true});

    // Transition from idle to idle
    AnimTransition trans;
    trans.fromState = 0;
    trans.toState = 0;
    trans.crossfadeDuration = 0.2f;
    sm.addTransition(trans);

    sm.start(m_animator);
    m_animator.update(0.01f);
    sm.update(m_animator, 0.016f);

    // Should not trigger a crossfade to itself
    EXPECT_FALSE(m_animator.isCrossfading());
}

TEST_F(StateMachineTest, ParameterDefaults)
{
    AnimationStateMachine sm;
    EXPECT_NEAR(sm.getFloat("nonexistent"), 0.0f, 0.001f);
    EXPECT_FALSE(sm.getBool("nonexistent"));
}

TEST_F(StateMachineTest, CompareOpsWork)
{
    AnimationStateMachine sm;
    sm.addState({"s0", 0, 1.0f, true});
    sm.addState({"s1", 1, 1.0f, true});

    // Test LESS
    AnimTransition trans;
    trans.fromState = 0;
    trans.toState = 1;
    trans.crossfadeDuration = 0.0f;
    trans.conditions.push_back({"val", AnimCompareOp::LESS, 5.0f});
    sm.addTransition(trans);

    sm.start(m_animator);
    m_animator.update(0.01f);

    sm.setFloat("val", 10.0f);  // 10 < 5 = false
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 0);

    sm.setFloat("val", 3.0f);  // 3 < 5 = true
    sm.update(m_animator, 0.016f);
    EXPECT_EQ(sm.getCurrentStateIndex(), 1);
}

TEST_F(StateMachineTest, GetStateCount)
{
    AnimationStateMachine sm;
    EXPECT_EQ(sm.getStateCount(), 0);
    sm.addState({"idle", 0, 1.0f, true});
    sm.addState({"walk", 1, 1.0f, true});
    EXPECT_EQ(sm.getStateCount(), 2);
}

TEST_F(StateMachineTest, EmptyStateMachineStartDoesNothing)
{
    AnimationStateMachine sm;
    sm.start(m_animator);
    EXPECT_FALSE(sm.isRunning());
}

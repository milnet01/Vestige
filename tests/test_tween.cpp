// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_tween.cpp
/// @brief Unit tests for the tween system (property animation).
#include "animation/tween.h"

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

using namespace Vestige;

// ---------------------------------------------------------------------------
// Float tween basics
// ---------------------------------------------------------------------------

TEST(TweenTest, FloatReachesTarget)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.update(1.0f);

    EXPECT_NEAR(value, 10.0f, 0.001f);
    EXPECT_TRUE(tween.isFinished());
}

TEST(TweenTest, FloatMidpoint)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.update(0.5f);

    EXPECT_NEAR(value, 5.0f, 0.001f);
    EXPECT_FALSE(tween.isFinished());
}

TEST(TweenTest, FloatWithEasing)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f, EaseType::EASE_IN_QUAD);
    tween.update(0.5f);

    // EaseInQuad(0.5) = 0.25 → value = 2.5
    EXPECT_NEAR(value, 2.5f, 0.01f);
}

// ---------------------------------------------------------------------------
// Vec3 tween
// ---------------------------------------------------------------------------

TEST(TweenTest, Vec3Interpolation)
{
    glm::vec3 value(0.0f);
    auto tween = Tween::vec3Tween(&value, glm::vec3(0.0f), glm::vec3(10.0f, 20.0f, 30.0f), 1.0f);
    tween.update(0.5f);

    EXPECT_NEAR(value.x, 5.0f, 0.01f);
    EXPECT_NEAR(value.y, 10.0f, 0.01f);
    EXPECT_NEAR(value.z, 15.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Vec4 tween
// ---------------------------------------------------------------------------

TEST(TweenTest, Vec4Interpolation)
{
    glm::vec4 value(0.0f);
    auto tween = Tween::vec4Tween(&value, glm::vec4(0.0f), glm::vec4(1.0f, 2.0f, 3.0f, 4.0f), 1.0f);
    tween.update(1.0f);

    EXPECT_NEAR(value.x, 1.0f, 0.001f);
    EXPECT_NEAR(value.y, 2.0f, 0.001f);
    EXPECT_NEAR(value.z, 3.0f, 0.001f);
    EXPECT_NEAR(value.w, 4.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Quaternion tween (slerp)
// ---------------------------------------------------------------------------

TEST(TweenTest, QuatUsesSlerp)
{
    glm::quat value(1, 0, 0, 0); // identity
    glm::quat from(1, 0, 0, 0);
    glm::quat to = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));

    auto tween = Tween::quatTween(&value, from, to, 1.0f);
    tween.update(0.5f);

    // At 50%, should be ~45 degrees around Y
    glm::quat expected = glm::slerp(from, to, 0.5f);
    EXPECT_NEAR(glm::dot(value, expected), 1.0f, 0.01f);
    // Result should be normalized
    EXPECT_NEAR(glm::length(value), 1.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// Delay
// ---------------------------------------------------------------------------

TEST(TweenTest, DelayPostponesStart)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.setDelay(0.5f);

    tween.update(0.3f);  // Still in delay
    EXPECT_NEAR(value, 0.0f, 0.001f);

    tween.update(0.7f);  // 0.3+0.7=1.0, active time = 0.5
    EXPECT_NEAR(value, 5.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Pause / Resume
// ---------------------------------------------------------------------------

TEST(TweenTest, PauseResumePreservesProgress)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);

    tween.update(0.3f);
    EXPECT_NEAR(value, 3.0f, 0.01f);

    tween.pause();
    tween.update(0.5f);  // Should not advance
    EXPECT_NEAR(value, 3.0f, 0.01f);
    EXPECT_TRUE(tween.isPaused());

    tween.resume();
    tween.update(0.2f);
    EXPECT_NEAR(value, 5.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Playback modes
// ---------------------------------------------------------------------------

TEST(TweenTest, OnceModeFinishes)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.setPlayback(TweenPlayback::ONCE);

    tween.update(1.5f);  // Overshoots duration
    EXPECT_NEAR(value, 10.0f, 0.001f);
    EXPECT_TRUE(tween.isFinished());
}

TEST(TweenTest, LoopWrapsAround)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.setPlayback(TweenPlayback::LOOP);

    // At 1.5s, should be 50% into second cycle → value = 5
    tween.update(1.5f);
    EXPECT_NEAR(value, 5.0f, 0.5f);
    EXPECT_FALSE(tween.isFinished());
}

TEST(TweenTest, PingPongReverses)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.setPlayback(TweenPlayback::PING_PONG);

    // At exactly 1.0s, should be at the end (10.0)
    tween.update(1.0f);
    float atEnd = value;

    // At 1.5s, should be 50% back → value = 5
    tween.update(0.5f);
    EXPECT_NEAR(value, 5.0f, 0.5f);
    EXPECT_FALSE(tween.isFinished());
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

TEST(TweenTest, EventFiresAtCorrectTime)
{
    float value = 0.0f;
    bool eventFired = false;

    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.addEvent(0.5f, [&eventFired]() { eventFired = true; });

    tween.update(0.3f);
    EXPECT_FALSE(eventFired);

    tween.update(0.3f);  // Progress crosses 0.5
    EXPECT_TRUE(eventFired);
}

TEST(TweenTest, EventFiresOnFrameSkip)
{
    float value = 0.0f;
    bool eventFired = false;

    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.addEvent(0.5f, [&eventFired]() { eventFired = true; });

    // Jump from 0 to 1.0 in one frame — should still fire the 0.5 event
    tween.update(1.0f);
    EXPECT_TRUE(eventFired);
}

TEST(TweenTest, MultipleEventsFireInOrder)
{
    float value = 0.0f;
    int order = 0;
    int firstOrder = -1, secondOrder = -1;

    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.addEvent(0.3f, [&]() { firstOrder = order++; });
    tween.addEvent(0.7f, [&]() { secondOrder = order++; });

    tween.update(1.0f);
    EXPECT_EQ(firstOrder, 0);
    EXPECT_EQ(secondOrder, 1);
}

// ---------------------------------------------------------------------------
// onComplete / onLoop callbacks
// ---------------------------------------------------------------------------

TEST(TweenTest, OnCompleteFiresOnce)
{
    float value = 0.0f;
    int completions = 0;

    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.onComplete([&completions]() { completions++; });

    tween.update(1.0f);
    EXPECT_EQ(completions, 1);

    // Already finished, update should not fire again
    tween.update(0.1f);
    EXPECT_EQ(completions, 1);
}

TEST(TweenTest, OnLoopFiresOnWrap)
{
    float value = 0.0f;
    int loops = 0;

    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.setPlayback(TweenPlayback::LOOP);
    tween.onLoop([&loops]() { loops++; });

    tween.update(0.5f);  // No wrap yet
    EXPECT_EQ(loops, 0);

    tween.update(0.6f);  // Wraps at 1.0s
    EXPECT_GE(loops, 1);
}

// ---------------------------------------------------------------------------
// Custom cubic bezier easing
// ---------------------------------------------------------------------------

TEST(TweenTest, CustomBezierEasing)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.setCustomEase(0.42f, 0.0f, 1.0f, 1.0f);  // ease-in

    tween.update(0.5f);
    // Ease-in at 50% should produce less than 5.0
    EXPECT_LT(value, 5.0f);
}

// ---------------------------------------------------------------------------
// Restart
// ---------------------------------------------------------------------------

TEST(TweenTest, RestartResetsToBeginning)
{
    float value = 0.0f;
    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);

    tween.update(1.0f);
    EXPECT_TRUE(tween.isFinished());

    tween.restart();
    EXPECT_FALSE(tween.isFinished());
    EXPECT_NEAR(tween.getProgress(), 0.0f, 0.001f);

    tween.update(0.5f);
    EXPECT_NEAR(value, 5.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// TweenManager
// ---------------------------------------------------------------------------

TEST(TweenManagerTest, TicksAllTweens)
{
    TweenManager mgr;
    float a = 0.0f, b = 0.0f;

    mgr.add(Tween::floatTween(&a, 0.0f, 10.0f, 1.0f));
    mgr.add(Tween::floatTween(&b, 0.0f, 20.0f, 1.0f));
    EXPECT_EQ(mgr.activeTweenCount(), 2u);

    mgr.update(0.5f);
    EXPECT_NEAR(a, 5.0f, 0.01f);
    EXPECT_NEAR(b, 10.0f, 0.01f);
}

TEST(TweenManagerTest, RemovesFinishedTweens)
{
    TweenManager mgr;
    float value = 0.0f;

    mgr.add(Tween::floatTween(&value, 0.0f, 10.0f, 1.0f));
    EXPECT_EQ(mgr.activeTweenCount(), 1u);

    mgr.update(1.1f);
    EXPECT_EQ(mgr.activeTweenCount(), 0u);
}

TEST(TweenManagerTest, CancelTargetRemovesSpecificTweens)
{
    TweenManager mgr;
    float a = 0.0f, b = 0.0f;

    mgr.add(Tween::floatTween(&a, 0.0f, 10.0f, 1.0f));
    mgr.add(Tween::floatTween(&b, 0.0f, 20.0f, 1.0f));

    mgr.cancelTarget(&a);
    EXPECT_EQ(mgr.activeTweenCount(), 1u);

    mgr.update(0.5f);
    EXPECT_NEAR(a, 0.0f, 0.001f);  // Cancelled, not updated
    EXPECT_NEAR(b, 10.0f, 0.01f);  // Still active
}

TEST(TweenManagerTest, CancelAllClearsEverything)
{
    TweenManager mgr;
    float a = 0.0f, b = 0.0f;

    mgr.add(Tween::floatTween(&a, 0.0f, 10.0f, 1.0f));
    mgr.add(Tween::floatTween(&b, 0.0f, 20.0f, 1.0f));

    mgr.cancelAll();
    EXPECT_EQ(mgr.activeTweenCount(), 0u);
}

TEST(TweenManagerTest, LoopingTweensNotRemoved)
{
    TweenManager mgr;
    float value = 0.0f;

    auto tween = Tween::floatTween(&value, 0.0f, 10.0f, 1.0f);
    tween.setPlayback(TweenPlayback::LOOP);
    mgr.add(std::move(tween));

    mgr.update(2.5f);  // Past multiple cycles
    EXPECT_EQ(mgr.activeTweenCount(), 1u);  // Still alive
}

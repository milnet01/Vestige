// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_sprite_animation.cpp
/// @brief Unit tests for SpriteAnimation (Phase 9F-1).
#include "animation/sprite_animation.h"

#include <gtest/gtest.h>

using namespace Vestige;

namespace
{

/// @brief Builds a simple three-frame forward clip (50 ms per frame).
SpriteAnimationClip makeThreeFrameClip(const std::string& name = "idle",
                                       SpriteAnimationDirection dir =
                                           SpriteAnimationDirection::Forward,
                                       bool loop = true)
{
    SpriteAnimationClip clip;
    clip.name = name;
    clip.direction = dir;
    clip.loop = loop;
    clip.frames = {
        {"f0", 50.0f},
        {"f1", 50.0f},
        {"f2", 50.0f},
    };
    return clip;
}

} // namespace

TEST(SpriteAnimation, PlaysForwardAndAdvancesFrames)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip());
    anim.play("idle");
    EXPECT_EQ(anim.currentFrameName(), "f0");

    // 30 ms — still on f0.
    anim.tick(0.030f);
    EXPECT_EQ(anim.currentFrameName(), "f0");
    EXPECT_FALSE(anim.advancedLastTick());

    // Another 30 ms pushes elapsed to 60 ms > 50 ms frame duration, advance.
    anim.tick(0.030f);
    EXPECT_EQ(anim.currentFrameName(), "f1");
    EXPECT_TRUE(anim.advancedLastTick());
}

TEST(SpriteAnimation, LargeDeltaCrossesMultipleFrames)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip());
    anim.play("idle");
    // 120 ms — crosses f0 (50) + f1 (50) = 100 ms, into f2 with 20ms left.
    anim.tick(0.120f);
    EXPECT_EQ(anim.currentFrameName(), "f2");
}

TEST(SpriteAnimation, LoopsBackToStart)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip("idle", SpriteAnimationDirection::Forward, /*loop*/ true));
    anim.play("idle");
    // 3 frames * 50ms = 150ms for one cycle. Tick slightly past that.
    anim.tick(0.160f);
    // After one loop + 10ms we should be back on f0.
    EXPECT_EQ(anim.currentFrameName(), "f0");
    EXPECT_TRUE(anim.isPlaying());
}

TEST(SpriteAnimation, NoLoopStopsOnLastFrame)
{
    SpriteAnimation anim;
    auto clip = makeThreeFrameClip("once", SpriteAnimationDirection::Forward, /*loop*/ false);
    anim.addClip(clip);
    anim.play("once");
    anim.tick(0.200f);
    EXPECT_FALSE(anim.isPlaying());
    EXPECT_EQ(anim.currentFrameName(), "f2");
}

TEST(SpriteAnimation, ReversePlaysLastToFirst)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip("rev", SpriteAnimationDirection::Reverse));
    anim.play("rev");
    EXPECT_EQ(anim.currentFrameName(), "f2");
    anim.tick(0.055f);
    EXPECT_EQ(anim.currentFrameName(), "f1");
    anim.tick(0.055f);
    EXPECT_EQ(anim.currentFrameName(), "f0");
}

TEST(SpriteAnimation, PingPongReversesAtEndpoints)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip("pp", SpriteAnimationDirection::PingPong));
    anim.play("pp");
    // Walk forward f0 → f1 → f2.
    anim.tick(0.055f);
    EXPECT_EQ(anim.currentFrameName(), "f1");
    anim.tick(0.055f);
    EXPECT_EQ(anim.currentFrameName(), "f2");
    // Next tick flips direction — back to f1.
    anim.tick(0.055f);
    EXPECT_EQ(anim.currentFrameName(), "f1");
    anim.tick(0.055f);
    EXPECT_EQ(anim.currentFrameName(), "f0");
    // Bounce forward again.
    anim.tick(0.055f);
    EXPECT_EQ(anim.currentFrameName(), "f1");
}

TEST(SpriteAnimation, StopKeepsCurrentFrame)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip());
    anim.play("idle");
    anim.tick(0.055f);  // advance to f1
    anim.stop();
    EXPECT_FALSE(anim.isPlaying());
    EXPECT_EQ(anim.currentFrameName(), "f1");
    // Subsequent ticks are no-ops.
    anim.tick(1.0f);
    EXPECT_EQ(anim.currentFrameName(), "f1");
}

TEST(SpriteAnimation, PlayUnknownClipIsNoOp)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip());
    anim.play("does_not_exist");
    EXPECT_FALSE(anim.isPlaying());
    EXPECT_EQ(anim.currentFrameName(), "");
}

TEST(SpriteAnimation, MultipleClipsSwitchCleanly)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip("idle"));

    SpriteAnimationClip run;
    run.name = "run";
    run.frames = {{"r0", 100.0f}, {"r1", 100.0f}};
    anim.addClip(run);

    anim.play("idle");
    anim.tick(0.055f);
    EXPECT_EQ(anim.currentFrameName(), "f1");

    anim.play("run");
    EXPECT_EQ(anim.currentFrameName(), "r0");
    anim.tick(0.110f);
    EXPECT_EQ(anim.currentFrameName(), "r1");
}

TEST(SpriteAnimation, AddClipReplacesExisting)
{
    SpriteAnimation anim;
    anim.addClip(makeThreeFrameClip());
    EXPECT_EQ(anim.clipCount(), 1u);

    // Replace with a clip of the same name.
    SpriteAnimationClip replacement;
    replacement.name = "idle";
    replacement.frames = {{"only", 100.0f}};
    anim.addClip(replacement);
    EXPECT_EQ(anim.clipCount(), 1u);

    anim.play("idle");
    EXPECT_EQ(anim.currentFrameName(), "only");
}

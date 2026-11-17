// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_source_mute_solo.cpp
/// @brief 3D_E-0874 — the per-entity Mute / Solo gate AudioSystem applies to
///        each auto-played source's output gain.
#include <gtest/gtest.h>

#include "audio/audio_source_mute_solo.h"

#include <unordered_set>

using namespace Vestige;

TEST(AudioSourceMuteSolo, NothingSetPlaysEverything)
{
    const AudioSourceMuteSolo gate;
    EXPECT_EQ(gate.gainMultiplier(1), 1.0f);
    EXPECT_FALSE(gate.anySoloed());
}

TEST(AudioSourceMuteSolo, MuteSilencesOnlyThatSource)
{
    AudioSourceMuteSolo gate;
    gate.setMuted(1, true);
    EXPECT_EQ(gate.gainMultiplier(1), 0.0f);
    EXPECT_EQ(gate.gainMultiplier(2), 1.0f);
    gate.setMuted(1, false);
    EXPECT_EQ(gate.gainMultiplier(1), 1.0f);
}

TEST(AudioSourceMuteSolo, SoloSilencesTheOthersAndMuteBeatsSolo)
{
    AudioSourceMuteSolo gate;
    gate.setSoloed(1, true);
    gate.setSoloed(2, true);
    EXPECT_EQ(gate.gainMultiplier(1), 1.0f);
    EXPECT_EQ(gate.gainMultiplier(2), 1.0f);
    EXPECT_EQ(gate.gainMultiplier(3), 0.0f);

    gate.setMuted(2, true);
    EXPECT_EQ(gate.gainMultiplier(2), 0.0f);
}

// A soloed entity that is deleted must not leave every other source silent
// with no checkbox left to clear it.
TEST(AudioSourceMuteSolo, ForgetGoneDropsDeletedEntities)
{
    AudioSourceMuteSolo gate;
    gate.setSoloed(1, true);
    gate.setMuted(2, true);
    gate.setMuted(3, true);

    const std::unordered_set<std::uint32_t> gone{1, 2};
    gate.forgetGone([&gone](std::uint32_t id) { return gone.count(id) != 0; });

    EXPECT_FALSE(gate.anySoloed());
    EXPECT_FALSE(gate.isMuted(2));
    EXPECT_TRUE(gate.isMuted(3));
    EXPECT_EQ(gate.gainMultiplier(4), 1.0f);
}

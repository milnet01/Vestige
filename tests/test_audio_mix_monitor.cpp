// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_mix_monitor.cpp
/// @brief Phase 10 AX12 — coverage for the per-bus producer-push monitor and
///        its pure combiner. Pins INV-2 (per-bus sum, zero-pad, buses never
///        cross), INV-3 (no clamp), INV-6 (inactive no-op / pending never leaks
///        without flush / deactivate frees), and idle-freeze.

#include <gtest/gtest.h>

#include "audio/audio_mix_monitor.h"

#include <vector>

using namespace Vestige;

// -- accumulateBusFrame (pure) --------------------------------------

TEST(AudioMixMonitor, SumsSimultaneousSubmissions)
{
    const std::vector<float> s0{1.0f, 2.0f, 3.0f, 4.0f};
    const std::vector<float> s1{10.0f, 20.0f, 30.0f, 40.0f};
    std::vector<MixSubmission> subs{
        {s0.data(), s0.size(), 0.5f},
        {s1.data(), s1.size(), 0.25f},
    };

    std::vector<float> out;
    accumulateBusFrame(subs, out);

    ASSERT_EQ(out.size(), std::size_t{4});
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_FLOAT_EQ(out[i], 0.5f * s0[i] + 0.25f * s1[i]);
    }
}

TEST(AudioMixMonitor, ShortBlockZeroPads)
{
    const std::vector<float> longB{1.0f, 1.0f, 1.0f, 1.0f};
    const std::vector<float> shortB{5.0f, 5.0f};  // only covers indices 0,1
    std::vector<MixSubmission> subs{
        {longB.data(), longB.size(), 1.0f},
        {shortB.data(), shortB.size(), 1.0f},
    };

    std::vector<float> out;
    accumulateBusFrame(subs, out);

    ASSERT_EQ(out.size(), std::size_t{4});  // == max submitted length
    EXPECT_FLOAT_EQ(out[0], 6.0f);
    EXPECT_FLOAT_EQ(out[1], 6.0f);
    EXPECT_FLOAT_EQ(out[2], 1.0f);  // short block zero-padded here
    EXPECT_FLOAT_EQ(out[3], 1.0f);
}

TEST(AudioMixMonitor, EmptyFrameYieldsEmpty)
{
    std::vector<MixSubmission> subs;
    std::vector<float> out{9.0f, 9.0f};  // pre-filled → cleared
    accumulateBusFrame(subs, out);
    EXPECT_TRUE(out.empty());
}

TEST(AudioMixMonitor, DoesNotClampOverUnity)
{
    // Two contributions each ≤ 1.0 summing > 1.0 must survive (INV-3).
    const std::vector<float> a{0.8f};
    const std::vector<float> b{0.7f};
    std::vector<MixSubmission> subs{
        {a.data(), a.size(), 1.0f},
        {b.data(), b.size(), 1.0f},
    };
    std::vector<float> out;
    accumulateBusFrame(subs, out);
    ASSERT_EQ(out.size(), std::size_t{1});
    EXPECT_FLOAT_EQ(out[0], 1.5f);  // not clamped to 1.0
}

// -- Monitor lifecycle ----------------------------------------------

TEST(AudioMixMonitor, InactiveSubmitIsNoOp)
{
    AudioMixMonitor mon;  // starts inactive
    const std::vector<float> s{1.0f, 2.0f};
    mon.submit(AudioBus::Music, s.data(), s.size(), 1.0f, 48000);
    mon.flushFrame();  // also a no-op while inactive
    EXPECT_TRUE(mon.ring(AudioBus::Music).empty());
    EXPECT_FALSE(mon.hadRecentSignal(AudioBus::Music));
}

TEST(AudioMixMonitor, PendingDoesNotLeakWithoutFlush)
{
    AudioMixMonitor mon;
    mon.setActive(true);
    const std::vector<float> s{1.0f, 2.0f, 3.0f};
    mon.submit(AudioBus::Music, s.data(), s.size(), 1.0f, 48000);
    // No flushFrame yet → the content ring must be unchanged (empty).
    EXPECT_TRUE(mon.ring(AudioBus::Music).empty());

    mon.flushFrame();
    ASSERT_EQ(mon.ring(AudioBus::Music).size(), std::size_t{3});
    EXPECT_FLOAT_EQ(mon.ring(AudioBus::Music)[1], 2.0f);
    EXPECT_EQ(mon.rateHz(AudioBus::Music), 48000);
    EXPECT_TRUE(mon.hadRecentSignal(AudioBus::Music));
}

TEST(AudioMixMonitor, GainIsAppliedOnSubmit)
{
    AudioMixMonitor mon;
    mon.setActive(true);
    const std::vector<float> s{2.0f, 4.0f};
    mon.submit(AudioBus::Sfx, s.data(), s.size(), 0.5f, 44100);
    mon.flushFrame();
    ASSERT_EQ(mon.ring(AudioBus::Sfx).size(), std::size_t{2});
    EXPECT_FLOAT_EQ(mon.ring(AudioBus::Sfx)[0], 1.0f);
    EXPECT_FLOAT_EQ(mon.ring(AudioBus::Sfx)[1], 2.0f);
}

TEST(AudioMixMonitor, IdleBusFreezesTrace)
{
    AudioMixMonitor mon;
    mon.setActive(true);
    const std::vector<float> s{1.0f, 2.0f};
    mon.submit(AudioBus::Music, s.data(), s.size(), 1.0f, 48000);
    mon.flushFrame();
    const std::size_t sizeAfterFirst = mon.ring(AudioBus::Music).size();

    // Next frame: no submit → flush appends nothing, ring holds (freeze).
    mon.flushFrame();
    EXPECT_EQ(mon.ring(AudioBus::Music).size(), sizeAfterFirst);
    EXPECT_FALSE(mon.hadRecentSignal(AudioBus::Music));
}

TEST(AudioMixMonitor, DifferentBusesDoNotCross)
{
    AudioMixMonitor mon;
    mon.setActive(true);
    const std::vector<float> m{1.0f, 1.0f};
    const std::vector<float> x{2.0f, 2.0f};
    mon.submit(AudioBus::Music, m.data(), m.size(), 1.0f, 48000);
    mon.submit(AudioBus::Sfx, x.data(), x.size(), 1.0f, 48000);
    mon.flushFrame();
    ASSERT_EQ(mon.ring(AudioBus::Music).size(), std::size_t{2});
    EXPECT_FLOAT_EQ(mon.ring(AudioBus::Music)[0], 1.0f);
    EXPECT_FLOAT_EQ(mon.ring(AudioBus::Sfx)[0], 2.0f);
}

TEST(AudioMixMonitor, DeactivateFreesRings)
{
    AudioMixMonitor mon;
    mon.setActive(true);
    const std::vector<float> s{1.0f, 2.0f};
    mon.submit(AudioBus::Music, s.data(), s.size(), 1.0f, 48000);
    mon.flushFrame();
    ASSERT_FALSE(mon.ring(AudioBus::Music).empty());

    mon.setActive(false);
    EXPECT_TRUE(mon.ring(AudioBus::Music).empty());
    EXPECT_FALSE(mon.hadRecentSignal(AudioBus::Music));
}

TEST(AudioMixMonitor, MostRecentRateWins)
{
    AudioMixMonitor mon;
    mon.setActive(true);
    const std::vector<float> s{1.0f};
    mon.submit(AudioBus::Music, s.data(), s.size(), 1.0f, 44100);
    mon.submit(AudioBus::Music, s.data(), s.size(), 1.0f, 48000);
    mon.flushFrame();
    EXPECT_EQ(mon.rateHz(AudioBus::Music), 48000);
}

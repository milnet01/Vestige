// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_output_mode.cpp
/// @brief Phase 10 audio quick-wins (AX8) — coverage for the headless
///        speaker-layout module: label/string round-trips, unknown-
///        token fallback, and the `resolveOutputMode` precedence table
///        (HRTF always wins). OpenAL device integration is not tested
///        here — only the pure mapping, which compares against the
///        `ALC_*_SOFT` tokens from the vendored `<AL/alext.h>`.

#include <gtest/gtest.h>

#include "audio/audio_output_mode.h"

#include <AL/alc.h>
#include <AL/alext.h>

using namespace Vestige;

// -- Labels & string round-trip -------------------------------------

TEST(AudioOutputMode, LabelsAreStable)
{
    EXPECT_STREQ(audioOutputLayoutLabel(AudioOutputLayout::Auto),       "Auto");
    EXPECT_STREQ(audioOutputLayoutLabel(AudioOutputLayout::Mono),       "Mono");
    EXPECT_STREQ(audioOutputLayoutLabel(AudioOutputLayout::Stereo),     "Stereo");
    EXPECT_STREQ(audioOutputLayoutLabel(AudioOutputLayout::Surround51), "5.1 Surround");
    EXPECT_STREQ(audioOutputLayoutLabel(AudioOutputLayout::Surround71), "7.1 Surround");
}

TEST(AudioOutputMode, StringRoundTrip)
{
    const AudioOutputLayout all[] = {
        AudioOutputLayout::Auto, AudioOutputLayout::Mono, AudioOutputLayout::Stereo,
        AudioOutputLayout::Surround51, AudioOutputLayout::Surround71,
    };
    for (AudioOutputLayout layout : all)
    {
        EXPECT_EQ(audioOutputLayoutFromString(audioOutputLayoutToString(layout)), layout);
    }
}

TEST(AudioOutputMode, UnknownTokenFallsBack)
{
    // Unknown / empty / hand-edited tokens degrade to the supplied
    // fallback (mirrors qualityPresetFromString's policy).
    EXPECT_EQ(audioOutputLayoutFromString("quadraphonic"), AudioOutputLayout::Auto);
    EXPECT_EQ(audioOutputLayoutFromString(""),             AudioOutputLayout::Auto);
    EXPECT_EQ(audioOutputLayoutFromString("garbage", AudioOutputLayout::Stereo),
              AudioOutputLayout::Stereo);
}

// -- resolveOutputMode: HRTF disabled => layout drives the mode ------

TEST(AudioOutputMode, LayoutDrivesModeWhenHrtfOff)
{
    EXPECT_EQ(resolveOutputMode(AudioOutputLayout::Auto,       false), ALC_ANY_SOFT);
    EXPECT_EQ(resolveOutputMode(AudioOutputLayout::Mono,       false), ALC_MONO_SOFT);
    EXPECT_EQ(resolveOutputMode(AudioOutputLayout::Stereo,     false), ALC_STEREO_SOFT);
    EXPECT_EQ(resolveOutputMode(AudioOutputLayout::Surround51, false), ALC_5POINT1_SOFT);
    EXPECT_EQ(resolveOutputMode(AudioOutputLayout::Surround71, false), ALC_7POINT1_SOFT);
}

// -- resolveOutputMode: HRTF on => ANY for every layout (HRTF wins) --

TEST(AudioOutputMode, HrtfWinsForEveryLayout)
{
    const AudioOutputLayout all[] = {
        AudioOutputLayout::Auto, AudioOutputLayout::Mono, AudioOutputLayout::Stereo,
        AudioOutputLayout::Surround51, AudioOutputLayout::Surround71,
    };
    for (AudioOutputLayout layout : all)
    {
        // HRTF is requested through the separate ALC_HRTF_SOFT attribute,
        // so the output mode must defer to the driver (ALC_ANY_SOFT) and
        // never co-request surround.
        EXPECT_EQ(resolveOutputMode(layout, true), ALC_ANY_SOFT);
    }
}

TEST(AudioOutputMode, NeverReturnsStereoHrtf)
{
    // ALC_STEREO_HRTF_SOFT must never be produced — HRTF is its own
    // attribute, so it is never double-specified through the output mode.
    const AudioOutputLayout all[] = {
        AudioOutputLayout::Auto, AudioOutputLayout::Mono, AudioOutputLayout::Stereo,
        AudioOutputLayout::Surround51, AudioOutputLayout::Surround71,
    };
    for (AudioOutputLayout layout : all)
    {
        EXPECT_NE(resolveOutputMode(layout, true),  ALC_STEREO_HRTF_SOFT);
        EXPECT_NE(resolveOutputMode(layout, false), ALC_STEREO_HRTF_SOFT);
    }
}

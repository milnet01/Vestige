// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_hrtf.cpp
/// @brief Phase 10 spatial-audio coverage for the HRTF headless
///        module — label stability, HrtfSettings equality, and
///        dataset-name resolution edge cases. OpenAL integration
///        itself is not tested here (requires an audio device).

#include <gtest/gtest.h>

#include "audio/audio_hrtf.h"

using namespace Vestige;

// -- Labels ---------------------------------------------------------

TEST(AudioHrtf, ModeLabelsAreStable)
{
    EXPECT_STREQ(hrtfModeLabel(HrtfMode::Disabled), "Disabled");
    EXPECT_STREQ(hrtfModeLabel(HrtfMode::Auto),     "Auto");
    EXPECT_STREQ(hrtfModeLabel(HrtfMode::Forced),   "Forced");
}

TEST(AudioHrtf, StatusLabelsAreStable)
{
    EXPECT_STREQ(hrtfStatusLabel(HrtfStatus::Disabled),           "Disabled");
    EXPECT_STREQ(hrtfStatusLabel(HrtfStatus::Enabled),            "Enabled");
    EXPECT_STREQ(hrtfStatusLabel(HrtfStatus::Denied),             "Denied");
    EXPECT_STREQ(hrtfStatusLabel(HrtfStatus::Required),           "Required");
    EXPECT_STREQ(hrtfStatusLabel(HrtfStatus::HeadphonesDetected), "HeadphonesDetected");
    EXPECT_STREQ(hrtfStatusLabel(HrtfStatus::UnsupportedFormat),  "UnsupportedFormat");
    EXPECT_STREQ(hrtfStatusLabel(HrtfStatus::Unknown),            "Unknown");
}

// -- HrtfSettings equality ------------------------------------------

TEST(AudioHrtf, DefaultSettingsAreAutoEmptyDataset)
{
    HrtfSettings s;
    EXPECT_EQ(s.mode, HrtfMode::Auto);
    EXPECT_TRUE(s.preferredDataset.empty());
}

TEST(AudioHrtf, SettingsEqualityConsidersModeAndDataset)
{
    HrtfSettings a;
    HrtfSettings b;
    EXPECT_EQ(a, b);

    b.mode = HrtfMode::Forced;
    EXPECT_NE(a, b);

    b.mode = HrtfMode::Auto;
    b.preferredDataset = "KEMAR";
    EXPECT_NE(a, b);

    a.preferredDataset = "KEMAR";
    EXPECT_EQ(a, b);
}

// -- resolveHrtfDatasetIndex ---------------------------------------

TEST(AudioHrtf, ResolveEmptyAvailableListReturnsMinusOne)
{
    const std::vector<std::string> none;
    EXPECT_EQ(resolveHrtfDatasetIndex(none, ""),       -1);
    EXPECT_EQ(resolveHrtfDatasetIndex(none, "KEMAR"),  -1);
}

TEST(AudioHrtf, ResolveEmptyPreferredPicksFirst)
{
    const std::vector<std::string> available{"Default HRTF", "KEMAR", "CIPIC 003"};
    EXPECT_EQ(resolveHrtfDatasetIndex(available, ""), 0);
}

TEST(AudioHrtf, ResolveExactNameReturnsThatIndex)
{
    const std::vector<std::string> available{"Default HRTF", "KEMAR", "CIPIC 003"};
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "Default HRTF"), 0);
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "KEMAR"),        1);
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "CIPIC 003"),    2);
}

TEST(AudioHrtf, ResolveUnknownNameReturnsMinusOne)
{
    const std::vector<std::string> available{"Default HRTF", "KEMAR"};
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "MadeUp"),   -1);
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "kemar"),    -1);  // case-sensitive
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "KEMAR "),   -1);  // trailing space fails
}

TEST(AudioHrtf, ResolveIsCaseSensitive)
{
    // Double-checked as a standalone case because the settings panel
    // wiring needs to match the driver's strings byte-for-byte — a
    // case-insensitive match would silently accept typos and pick
    // the wrong dataset.
    const std::vector<std::string> available{"KEMAR"};
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "KEMAR"), 0);
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "Kemar"), -1);
    EXPECT_EQ(resolveHrtfDatasetIndex(available, "kemar"), -1);
}

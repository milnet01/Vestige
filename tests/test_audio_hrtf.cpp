// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_audio_hrtf.cpp
/// @brief Phase 10 spatial-audio coverage for the HRTF headless
///        module — label stability, HrtfSettings equality, and
///        dataset-name resolution edge cases. OpenAL integration
///        itself is not tested here (requires an audio device).

#include <gtest/gtest.h>

#include "audio/audio_engine.h"
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

// -- Phase 10.9 Slice 2 P8: HrtfStatusEvent + composeHrtfStatusEvent
//
// The Settings UI needs to surface "Requested: Forced / Actual:
// Denied (UnsupportedFormat)" whenever the driver downgrades an
// HRTF request. The event payload pairs the engine-stored
// `HrtfSettings` (what we asked for) with the driver's resolved
// `HrtfStatus` (what we got) so a single listener call is enough
// for the panel to render both sides without reading back through
// AudioEngine.

TEST(AudioHrtfStatusEvent, ComposeCarriesRequestedModeAndDataset)
{
    HrtfSettings settings;
    settings.mode = HrtfMode::Forced;
    settings.preferredDataset = "KEMAR";

    const auto event = composeHrtfStatusEvent(settings, HrtfStatus::Enabled);

    EXPECT_EQ(event.requestedMode,    HrtfMode::Forced);
    EXPECT_EQ(event.requestedDataset, "KEMAR");
    EXPECT_EQ(event.actualStatus,     HrtfStatus::Enabled);
}

TEST(AudioHrtfStatusEvent, ComposeCapturesDriverDowngrade)
{
    // Forced + UnsupportedFormat is the exact case the roadmap calls
    // out — the user asked for HRTF, the driver refused because the
    // output format can't do it. The listener must see both halves.
    HrtfSettings settings;
    settings.mode = HrtfMode::Forced;

    const auto event =
        composeHrtfStatusEvent(settings, HrtfStatus::UnsupportedFormat);

    EXPECT_EQ(event.requestedMode, HrtfMode::Forced);
    EXPECT_EQ(event.actualStatus,  HrtfStatus::UnsupportedFormat);
    EXPECT_TRUE(event.requestedDataset.empty());
}

TEST(AudioHrtfStatusEvent, ComposeCarriesUnknownForUninitializedEngine)
{
    // Pre-initialize setHrtfMode calls must still notify the UI so a
    // user can see their requested mode before any device is open.
    // The resolved status is Unknown on an uninitialized engine.
    HrtfSettings settings;
    settings.mode = HrtfMode::Auto;

    const auto event = composeHrtfStatusEvent(settings, HrtfStatus::Unknown);

    EXPECT_EQ(event.requestedMode, HrtfMode::Auto);
    EXPECT_EQ(event.actualStatus,  HrtfStatus::Unknown);
}

// -- AudioEngine HrtfStatusListener wiring --------------------------
//
// The listener fires from applyHrtfSettings() every time — mid-session
// setHrtfMode / setHrtfDataset changes AND the first pass during
// initialize(). On an uninitialized engine (no device open), the
// status field reads as Unknown but the event still fires so the
// Settings UI can reflect pre-init user choices.

TEST(AudioEngineHrtfStatusListener, FiresOnSetHrtfMode_UninitializedEngine_P8)
{
    AudioEngine engine;  // no initialize()
    std::vector<HrtfStatusEvent> events;
    engine.setHrtfStatusListener([&](const HrtfStatusEvent& e)
    {
        events.push_back(e);
    });

    engine.setHrtfMode(HrtfMode::Forced);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].requestedMode, HrtfMode::Forced);
    EXPECT_EQ(events[0].actualStatus,  HrtfStatus::Unknown);
}

TEST(AudioEngineHrtfStatusListener, FiresOnSetHrtfDataset_UninitializedEngine_P8)
{
    AudioEngine engine;
    std::vector<HrtfStatusEvent> events;
    engine.setHrtfStatusListener([&](const HrtfStatusEvent& e)
    {
        events.push_back(e);
    });

    engine.setHrtfDataset("KEMAR");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].requestedDataset, "KEMAR");
    EXPECT_EQ(events[0].actualStatus,     HrtfStatus::Unknown);
}

TEST(AudioEngineHrtfStatusListener, DoesNotFireWhenModeUnchanged_P8)
{
    // setHrtfMode(currentMode) early-returns before applyHrtfSettings
    // runs. No spurious event — the Settings UI only wants to know
    // about *changes*.
    AudioEngine engine;
    std::vector<HrtfStatusEvent> events;
    engine.setHrtfStatusListener([&](const HrtfStatusEvent& e)
    {
        events.push_back(e);
    });

    engine.setHrtfMode(HrtfMode::Auto);  // default is Auto — no change

    EXPECT_TRUE(events.empty());
}

TEST(AudioEngineHrtfStatusListener, FiresOncePerChange_P8)
{
    AudioEngine engine;
    std::vector<HrtfStatusEvent> events;
    engine.setHrtfStatusListener([&](const HrtfStatusEvent& e)
    {
        events.push_back(e);
    });

    engine.setHrtfMode(HrtfMode::Forced);
    engine.setHrtfMode(HrtfMode::Disabled);
    engine.setHrtfDataset("KEMAR");

    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].requestedMode, HrtfMode::Forced);
    EXPECT_EQ(events[1].requestedMode, HrtfMode::Disabled);
    EXPECT_EQ(events[2].requestedDataset, "KEMAR");
}

TEST(AudioEngineHrtfStatusListener, NoCrashWithoutListener_P8)
{
    // Defensive: the engine must not assume a listener is registered.
    AudioEngine engine;
    engine.setHrtfMode(HrtfMode::Forced);  // no listener — must not crash
    SUCCEED();
}

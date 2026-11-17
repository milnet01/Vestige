// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_water_presets.cpp
/// @brief 3D_E-0874 lane-14 — the Inspector's water preset Combo shows the
///        preset each surface holds, not one index shared by every surface.
#include <gtest/gtest.h>

#include "editor/panels/water_presets.h"

using namespace Vestige;

TEST(WaterPresets, EachPresetIsRecognisedOnTheSurfaceItWasAppliedTo)
{
    for (int preset = 1; preset < static_cast<int>(kWaterPresetNames.size()); ++preset)
    {
        WaterSurfaceConfig config;
        applyWaterPreset(preset, config);
        EXPECT_EQ(matchingWaterPreset(config), preset) << kWaterPresetNames[preset];
    }
}

// Two water entities: one set to Ocean Swell, the other left alone. Each
// shows its own preset. The old shared static showed Ocean Swell on both.
TEST(WaterPresets, TwoSurfacesShowTheirOwnPreset)
{
    WaterSurfaceConfig ocean;
    applyWaterPreset(4, ocean);
    const WaterSurfaceConfig untouched;
    EXPECT_EQ(matchingWaterPreset(ocean), 4);
    EXPECT_EQ(matchingWaterPreset(untouched), 0);
}

// A manual edit after picking a preset shows "Custom"; the old static
// kept naming the preset.
TEST(WaterPresets, AManualEditShowsCustom)
{
    WaterSurfaceConfig config;
    applyWaterPreset(2, config);
    config.flowSpeed += 0.01f;
    EXPECT_EQ(matchingWaterPreset(config), 0);

    applyWaterPreset(3, config);
    config.waves[2].direction = 0.0f;
    EXPECT_EQ(matchingWaterPreset(config), 0) << "an edited wave the preset uses";
}

// A wave the preset does not use may hold anything: Still Bath uses one
// wave, so the other three are not part of the match.
TEST(WaterPresets, WavesBeyondThePresetsCountDoNotMatter)
{
    WaterSurfaceConfig config;
    config.waves[3] = {0.5f, 9.0f, 2.0f, 180.0f};
    applyWaterPreset(1, config);
    EXPECT_EQ(matchingWaterPreset(config), 1);
}

TEST(WaterPresets, CustomAndUnknownIndicesChangeNothing)
{
    const WaterSurfaceConfig before;
    for (const int preset : {0, -1, 5})
    {
        WaterSurfaceConfig config;
        applyWaterPreset(preset, config);
        EXPECT_EQ(config.numWaves, before.numWaves) << preset;
        EXPECT_EQ(config.flowSpeed, before.flowSpeed) << preset;
    }
}

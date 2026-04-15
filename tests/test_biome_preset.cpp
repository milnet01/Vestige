// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_biome_preset.cpp
/// @brief Unit tests for BiomePreset and BiomeLibrary.
#include <gtest/gtest.h>

#include "environment/biome_preset.h"

using namespace Vestige;

TEST(BiomePresetTest, SerializeDeserialize)
{
    BiomePreset preset;
    preset.name = "Test Biome";
    preset.groundMaterialPath = "materials/grass.json";
    preset.foliageLayers = {{0, 3.0f}, {1, 1.5f}};
    preset.scatterLayers = {{0, 0.5f}};
    preset.treeLayers = {{0, 0.03f}};

    nlohmann::json j = preset.serialize();
    BiomePreset loaded = BiomePreset::deserialize(j);

    EXPECT_EQ(loaded.name, "Test Biome");
    EXPECT_EQ(loaded.groundMaterialPath, "materials/grass.json");
    EXPECT_EQ(loaded.foliageLayers.size(), 2u);
    EXPECT_FLOAT_EQ(loaded.foliageLayers[0].density, 3.0f);
    EXPECT_EQ(loaded.scatterLayers.size(), 1u);
    EXPECT_EQ(loaded.treeLayers.size(), 1u);
    EXPECT_FLOAT_EQ(loaded.treeLayers[0].density, 0.03f);
}

TEST(BiomeLibraryTest, BuiltInPresets)
{
    BiomeLibrary library;

    EXPECT_GE(library.getPresetCount(), 4);

    auto names = library.getPresetNames();
    EXPECT_EQ(names[0], "Garden");
    EXPECT_EQ(names[1], "Desert");
    EXPECT_EQ(names[2], "Temple Courtyard");
    EXPECT_EQ(names[3], "Cedar Forest");
}

TEST(BiomeLibraryTest, AddUserPreset)
{
    BiomeLibrary library;
    int initialCount = library.getPresetCount();

    BiomePreset custom;
    custom.name = "My Custom Biome";
    custom.foliageLayers = {{0, 5.0f}};
    library.addPreset(custom);

    EXPECT_EQ(library.getPresetCount(), initialCount + 1);
    EXPECT_EQ(library.getPreset(initialCount).name, "My Custom Biome");
}

TEST(BiomeLibraryTest, SerializeUserPresets)
{
    BiomeLibrary library;

    BiomePreset custom;
    custom.name = "User Biome";
    custom.foliageLayers = {{2, 1.0f}};
    library.addPreset(custom);

    nlohmann::json j = library.serializeUserPresets();
    EXPECT_EQ(j.size(), 1u);  // Only user presets, not built-ins
    EXPECT_EQ(j[0]["name"], "User Biome");
}

TEST(BiomeLibraryTest, DeserializeUserPresets)
{
    BiomeLibrary library;
    int initialCount = library.getPresetCount();

    nlohmann::json j = nlohmann::json::array();
    j.push_back({{"name", "Imported Biome"}, {"foliageLayers", {{{"typeId", 0}, {"density", 2.0}}}},
                 {"scatterLayers", nlohmann::json::array()}, {"treeLayers", nlohmann::json::array()}});

    library.deserializeUserPresets(j);
    EXPECT_EQ(library.getPresetCount(), initialCount + 1);
    EXPECT_EQ(library.getPreset(initialCount).name, "Imported Biome");
}

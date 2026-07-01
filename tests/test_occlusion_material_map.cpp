// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_occlusion_material_map.cpp
/// @brief Unit tests for the physics->audio occlusion material map (AX1 S1):
///        every SurfaceMaterial maps to a valid preset, Default->Concrete, and
///        no surface ever maps to Air (a body that blocked a ray is not air).
#include "audio/occlusion_material_map.h"
#include "audio/audio_occlusion.h"
#include "physics/surface_material.h"

#include <gtest/gtest.h>

using namespace Vestige;

// Every one of the dense [0, kSurfaceMaterialCount) enumerators must map to a
// preset, and none may map to Air (Air = "no obstruction", which is never the
// answer for a body that actually blocked a ray).
TEST(OcclusionMaterialMap, EveryMemberMapsAndNeverAir)
{
    for (int i = 0; i < kSurfaceMaterialCount; ++i)
    {
        const auto surface = static_cast<SurfaceMaterial>(i);
        const AudioOcclusionMaterialPreset preset =
            occlusionPresetForSurface(surface);
        EXPECT_NE(preset, AudioOcclusionMaterialPreset::Air)
            << "SurfaceMaterial " << surfaceMaterialLabel(surface)
            << " must not map to Air";
    }
}

// Untagged geometry must occlude like a generic solid wall so a level's plain,
// un-tagged walls still block sound without designer effort (the AX1 gap).
TEST(OcclusionMaterialMap, DefaultMapsToConcrete)
{
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Default),
              AudioOcclusionMaterialPreset::Concrete);
}

// The 1:1 rows (materials both enums name) pass through by identity of meaning.
TEST(OcclusionMaterialMap, DirectRowsPassThrough)
{
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Stone),
              AudioOcclusionMaterialPreset::Stone);
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Wood),
              AudioOcclusionMaterialPreset::Wood);
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Metal),
              AudioOcclusionMaterialPreset::Metal);
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Glass),
              AudioOcclusionMaterialPreset::Glass);
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Water),
              AudioOcclusionMaterialPreset::Water);
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Cloth),
              AudioOcclusionMaterialPreset::Cloth);
}

// The non-1:1 rows: the audio enum lacks Sand/Grass/Dirt, so they map to the
// nearest acoustic wall — Grass is thin/porous (Cloth); Sand/Dirt are dense
// earth (Concrete).
TEST(OcclusionMaterialMap, NonOneToOneRows)
{
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Grass),
              AudioOcclusionMaterialPreset::Cloth);
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Sand),
              AudioOcclusionMaterialPreset::Concrete);
    EXPECT_EQ(occlusionPresetForSurface(SurfaceMaterial::Dirt),
              AudioOcclusionMaterialPreset::Concrete);
}

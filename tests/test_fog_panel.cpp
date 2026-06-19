// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_fog_panel.cpp
/// @brief Non-GL tests for the editor FogPanel (slice 11.10) — open/close
///        toggle, the fog-volume working-set management (add / select /
///        remove with selection-shift semantics), and a parity guard pinning
///        the lifted VolumetricFogParams / GodRayParams defaults to the
///        constants they replaced in the renderer.

#include <gtest/gtest.h>

#include "editor/panels/fog_panel.h"
#include "renderer/volumetric_fog.h"

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-6f;
}

// -- Defaults + open/close --------------------------------------------------

TEST(FogPanel, DefaultsAreClosed)
{
    FogPanel p;
    EXPECT_FALSE(p.isOpen());
    EXPECT_EQ(p.selectedVolume(), -1);
    EXPECT_TRUE(p.volumes().empty());
    EXPECT_STREQ(p.displayName(), "Fog");
}

TEST(FogPanel, SetOpenToggles)
{
    FogPanel p;
    p.setOpen(true);
    EXPECT_TRUE(p.isOpen());
    p.toggleOpen();
    EXPECT_FALSE(p.isOpen());
    p.toggleOpen();
    EXPECT_TRUE(p.isOpen());
}

// -- Fog-volume working set -------------------------------------------------

TEST(FogPanel, AddVolumeReturnsIndexAndSelects)
{
    FogPanel p;
    FogVolume box;
    box.shape = FogVolumeShape::Box;
    EXPECT_EQ(p.addVolume(box), 0);
    EXPECT_EQ(p.volumes().size(), 1u);
    EXPECT_EQ(p.selectedVolume(), 0);

    FogVolume sphere;
    sphere.shape = FogVolumeShape::Sphere;
    EXPECT_EQ(p.addVolume(sphere), 1);
    EXPECT_EQ(p.volumes().size(), 2u);
    EXPECT_EQ(p.selectedVolume(), 1);
    EXPECT_EQ(p.volumes()[1].shape, FogVolumeShape::Sphere);
}

TEST(FogPanel, RemoveVolumeShiftsSelectionDown)
{
    FogPanel p;
    p.addVolume({});
    p.addVolume({});
    p.addVolume({});
    p.selectVolume(2);

    // Remove index 0 — the row that was index 2 is now index 1.
    EXPECT_TRUE(p.removeVolume(0));
    EXPECT_EQ(p.selectedVolume(), 1);
    EXPECT_EQ(p.volumes().size(), 2u);
}

TEST(FogPanel, RemoveSelectedVolumeClearsSelection)
{
    FogPanel p;
    p.addVolume({});
    p.selectVolume(0);
    EXPECT_TRUE(p.removeVolume(0));
    EXPECT_EQ(p.selectedVolume(), -1);
}

TEST(FogPanel, RemoveBelowSelectionKeepsSelection)
{
    FogPanel p;
    p.addVolume({});
    p.addVolume({});
    p.selectVolume(0);

    // Removing a row *after* the selection leaves the selection index intact.
    EXPECT_TRUE(p.removeVolume(1));
    EXPECT_EQ(p.selectedVolume(), 0);
    EXPECT_EQ(p.volumes().size(), 1u);
}

TEST(FogPanel, RemoveOutOfRangeIsNoOp)
{
    FogPanel p;
    p.addVolume({});
    EXPECT_FALSE(p.removeVolume(-1));
    EXPECT_FALSE(p.removeVolume(5));
    EXPECT_EQ(p.volumes().size(), 1u);
}

// -- Parity guard: lifted defaults reproduce the renderer's prior literals --
//
// Slice 11.10 lifted the inlined volumetric-medium / noise / god-ray
// constants out of renderer.cpp into these structs. The lift is only
// behaviour-preserving if the defaults match byte-for-byte; this pins them so
// a future edit to either site can't silently desync the two.

TEST(FogPanel, VolumetricFogParamsDefaultsMatchLiftedLiterals)
{
    VolumetricFogParams v;
    EXPECT_NEAR(v.scattering.x, 0.005f, kEps);
    EXPECT_NEAR(v.scattering.y, 0.005f, kEps);
    EXPECT_NEAR(v.scattering.z, 0.005f, kEps);
    EXPECT_NEAR(v.extinction, 0.005f, kEps);
    EXPECT_NEAR(v.anisotropy, 0.3f, kEps);

    EXPECT_TRUE(v.noise.enabled);
    EXPECT_NEAR(v.noise.frequency, 0.03f, kEps);
    EXPECT_NEAR(v.noise.strength, 0.5f, kEps);
    EXPECT_EQ(v.noise.octaves, 3);
    // windVelocity Z is 0.15 — NOT the FogNoiseParams struct default of 0.1.
    EXPECT_NEAR(v.noise.windVelocity.x, 0.4f, kEps);
    EXPECT_NEAR(v.noise.windVelocity.y, 0.0f, kEps);
    EXPECT_NEAR(v.noise.windVelocity.z, 0.15f, kEps);
}

TEST(FogPanel, GodRayParamsDefaultsMatchLiftedLiterals)
{
    GodRayParams g;
    EXPECT_NEAR(g.intensity, 1.0f, kEps);
    EXPECT_NEAR(g.edgeMargin, 0.3f, kEps);
}

// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_subsurface.cpp
/// @brief GL-free tests for Slice R3 subsurface scattering: the CPU math mirror
///        (engine/renderer/subsurface_math.h) and the Material field contract
///        (defaults, clamps, JSON round-trip). The CPU↔GLSL parity is in
///        test_subsurface_parity.cpp.

#include "renderer/material.h"
#include "renderer/subsurface_math.h"
#include "utils/material_library.h"
#include "resource/resource_manager.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <filesystem>
#include <fstream>

using namespace Vestige;

namespace
{

// ---------------------------------------------------------------------------
// Material field contract (design §10.3, test #1)
// ---------------------------------------------------------------------------

TEST(SubsurfaceMaterial, DefaultsAreFeatureOff)
{
    Material m;
    EXPECT_FLOAT_EQ(m.getSubsurfaceStrength(), 0.0f);   // 0 ⇒ SSS skipped in-shader
    EXPECT_EQ(m.getSubsurfaceColor(), glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(m.getSubsurfaceThickness(), 0.5f);
}

TEST(SubsurfaceMaterial, ScalarsClampColorDoesNot)
{
    Material m;
    m.setSubsurfaceStrength(2.0f);
    EXPECT_FLOAT_EQ(m.getSubsurfaceStrength(), 1.0f);
    m.setSubsurfaceStrength(-1.0f);
    EXPECT_FLOAT_EQ(m.getSubsurfaceStrength(), 0.0f);

    m.setSubsurfaceThickness(5.0f);
    EXPECT_FLOAT_EQ(m.getSubsurfaceThickness(), 1.0f);
    m.setSubsurfaceThickness(-0.5f);
    EXPECT_FLOAT_EQ(m.getSubsurfaceThickness(), 0.0f);

    // Color matches setAlbedo/setEmissive: unclamped (an over-bright tint is legal).
    m.setSubsurfaceColor(glm::vec3(1.5f, 1.0f, 0.0f));
    EXPECT_EQ(m.getSubsurfaceColor(), glm::vec3(1.5f, 1.0f, 0.0f));
}

TEST(SubsurfaceMaterial, JsonRoundTripPreservesFields)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "vestige_sss_roundtrip";
    fs::remove_all(dir);
    fs::create_directories(dir);

    ResourceManager resources;  // no textures on this material ⇒ no GL calls

    Material src;
    src.setType(MaterialType::PBR);
    src.setSubsurfaceStrength(0.7f);
    src.setSubsurfaceColor(glm::vec3(1.5f, 0.4f, 0.3f));  // over-bright tint survives
    src.setSubsurfaceThickness(0.2f);
    ASSERT_TRUE(MaterialLibrary::saveMaterial("sss_mat", src, resources, dir.string()));

    Material dst;
    ASSERT_TRUE(MaterialLibrary::loadMaterial("sss_mat", dst, resources, dir.string()));
    EXPECT_FLOAT_EQ(dst.getSubsurfaceStrength(), 0.7f);
    EXPECT_EQ(dst.getSubsurfaceColor(), glm::vec3(1.5f, 0.4f, 0.3f));
    EXPECT_FLOAT_EQ(dst.getSubsurfaceThickness(), 0.2f);

    fs::remove_all(dir);
}

TEST(SubsurfaceMaterial, OldJsonWithoutKeysLoadsDefaults)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "vestige_sss_olddefaults";
    fs::remove_all(dir);
    fs::create_directories(dir / "materials");

    // A pre-R3 PBR material file with no subsurface keys.
    std::ofstream(dir / "materials" / "old.json") << R"({"type":"PBR","albedo":[0.8,0.8,0.8]})";

    ResourceManager resources;
    Material dst;
    ASSERT_TRUE(MaterialLibrary::loadMaterial("old", dst, resources, dir.string()));
    EXPECT_FLOAT_EQ(dst.getSubsurfaceStrength(), 0.0f);
    EXPECT_EQ(dst.getSubsurfaceColor(), glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(dst.getSubsurfaceThickness(), 0.5f);

    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Front-scatter CPU mirror (design §10.2a / §10.8 #2, #3)
// ---------------------------------------------------------------------------

TEST(SubsurfaceFrontScatter, ZeroAtStrengthZeroForAllNdotL)
{
    for (float ndl = -1.0f; ndl <= 1.0f; ndl += 0.1f)
    {
        glm::vec3 fs = sssFrontScatter(ndl, 0.0f, glm::vec3(1.0f));
        EXPECT_EQ(fs, glm::vec3(0.0f)) << "ndl=" << ndl;
    }
}

TEST(SubsurfaceFrontScatter, LitSideClosedFormZeroOnlyAtPole)
{
    const float strength = 0.8f;
    const float wrap = strength * SSS_MAX_WRAP;
    const glm::vec3 color(1.0f);

    // N·L = 1 (pole): exactly zero.
    EXPECT_NEAR(sssFrontScatter(1.0f, strength, color).x, 0.0f, 1e-6f);

    // N·L ∈ [0,1): strictly positive and equal to wrap·(1−N·L)/(1+wrap).
    for (float ndl = 0.0f; ndl < 1.0f; ndl += 0.1f)
    {
        float expected = wrap * (1.0f - ndl) / (1.0f + wrap);
        glm::vec3 fs = sssFrontScatter(ndl, strength, color);
        EXPECT_NEAR(fs.x, expected, 1e-6f) << "ndl=" << ndl;
        EXPECT_GT(fs.x, 0.0f) << "ndl=" << ndl;
    }
}

TEST(SubsurfaceFrontScatter, ShadowSideBranchDiffersAndDecaysToZeroAtMinusWrap)
{
    const float strength = 1.0f;
    const float wrap = strength * SSS_MAX_WRAP;  // 0.5
    const glm::vec3 color(1.0f);

    // Shadow side N·L ∈ (−wrap, 0): frontScatter = (N·L + wrap)/(1+wrap) (lambert=0).
    for (float ndl = -wrap + 0.05f; ndl < 0.0f; ndl += 0.05f)
    {
        float expected = (ndl + wrap) / (1.0f + wrap);
        EXPECT_NEAR(sssFrontScatter(ndl, strength, color).x, expected, 1e-6f) << "ndl=" << ndl;
    }
    // Decays to 0 at N·L = −wrap and below.
    EXPECT_NEAR(sssFrontScatter(-wrap, strength, color).x, 0.0f, 1e-6f);
    EXPECT_NEAR(sssFrontScatter(-0.9f, strength, color).x, 0.0f, 1e-6f);
}

TEST(SubsurfaceFrontScatter, NeverDarkens)
{
    // wrapped ≥ lambert for every N·L and wrap ⇒ frontScatter ≥ 0 always.
    for (float strength = 0.0f; strength <= 1.0f; strength += 0.25f)
    {
        for (float ndl = -1.0f; ndl <= 1.0f; ndl += 0.1f)
        {
            glm::vec3 fs = sssFrontScatter(ndl, strength, glm::vec3(1.0f));
            EXPECT_GE(fs.x, 0.0f) << "strength=" << strength << " ndl=" << ndl;
        }
    }
}

TEST(SubsurfaceFrontScatter, TintScalesLinearlyWithColor)
{
    glm::vec3 c(0.4f, 0.7f, 1.0f);
    glm::vec3 fs = sssFrontScatter(0.3f, 0.6f, c);
    glm::vec3 white = sssFrontScatter(0.3f, 0.6f, glm::vec3(1.0f));
    EXPECT_NEAR(fs.x, white.x * c.r, 1e-6f);
    EXPECT_NEAR(fs.y, white.y * c.g, 1e-6f);
    EXPECT_NEAR(fs.z, white.z * c.b, 1e-6f);
}

// ---------------------------------------------------------------------------
// Back-scatter CPU mirror (design §10.2b / §10.8 #4)
// ---------------------------------------------------------------------------

TEST(SubsurfaceBackScatter, PeaksWhenViewingTowardBacklitThinFace)
{
    const glm::vec3 N(0.0f, 0.0f, 1.0f);
    const glm::vec3 L(0.0f, 0.0f, 1.0f);  // light in front (surface→light = +z)
    // backLightDir ≈ normalize(L + N*0.2) = +z; viewer looking back through the
    // surface toward the light has V ≈ -z ⇒ dot(V, -backLightDir) ≈ 1.
    const glm::vec3 Vback(0.0f, 0.0f, -1.0f);
    const glm::vec3 color(1.0f);
    const glm::vec3 radiance(1.0f);

    glm::vec3 thin  = sssBackScatter(Vback, L, N, 1.0f, 0.0f, color, radiance);
    glm::vec3 thick = sssBackScatter(Vback, L, N, 1.0f, 1.0f, color, radiance);

    EXPECT_GT(thin.x, 0.5f);                       // strong glow through a thin face
    EXPECT_NEAR(thick.x, 0.0f, 1e-6f);             // thickness=1 ⇒ no transmission

    // strength=0 ⇒ no transmission.
    EXPECT_NEAR(sssBackScatter(Vback, L, N, 0.0f, 0.0f, color, radiance).x, 0.0f, 1e-6f);

    // Viewer on the lit side (V aligned with backLightDir) ⇒ clamp to 0.
    const glm::vec3 Vfront(0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(sssBackScatter(Vfront, L, N, 1.0f, 0.0f, color, radiance).x, 0.0f, 1e-6f);
}

TEST(SubsurfaceBackScatter, ScalesWithOneMinusThickness)
{
    const glm::vec3 N(0.0f, 0.0f, 1.0f);
    const glm::vec3 L(0.0f, 0.0f, 1.0f);
    const glm::vec3 Vback(0.0f, 0.0f, -1.0f);
    const glm::vec3 color(1.0f);
    const glm::vec3 radiance(1.0f);

    glm::vec3 t0   = sssBackScatter(Vback, L, N, 1.0f, 0.0f, color, radiance);
    glm::vec3 tHalf = sssBackScatter(Vback, L, N, 1.0f, 0.5f, color, radiance);
    EXPECT_NEAR(tHalf.x, t0.x * 0.5f, 1e-6f);  // transmission ∝ (1 - thickness)
}

}  // namespace

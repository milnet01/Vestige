// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gi.cpp
/// @brief GL-free tests for Slice R4 dynamic GI (Variant A): the CPU math mirror
///        (engine/renderer/gi_math.h) — slice-coord parity with the fog spec, the
///        read/write coord round-trip, the confidence-weighted EMA, and the
///        reprojected history read. CPU↔GLSL parity + behaviour live in
///        test_gi_gpu.cpp; the inject-budget gate lives in test_fog_benchmark.cpp.

#include "renderer/gi_math.h"
#include "renderer/volumetric_fog.h"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Vestige;

namespace
{

// ---------------------------------------------------------------------------
// Test 1 — GiFroxelReconstructionMatchesFogPath
// ---------------------------------------------------------------------------

// The GI slice-coord helper must equal the fog-spec slice path
// `(viewDepthToFroxelSlice + 0.5) / resZ` over the depth range, so the GI cache is
// addressed identically to the fog volume it co-locates in.
TEST(GiFroxelReconstruction, SliceCoordEqualsFogSpec)
{
    FroxelGridConfig cfg;  // 160×90×64, near 0.5, far 200
    for (int s = 0; s <= 200; ++s)
    {
        // Sweep view depths across [near, far] (and a touch beyond, to hit the clamp).
        float vd = cfg.near + (cfg.far - cfg.near) * (static_cast<float>(s) / 200.0f);
        float gi = giVolumetricSliceCoord(vd, cfg.near, cfg.far);
        float fogSpec = (viewDepthToFroxelSlice(cfg, vd) + 0.5f) / static_cast<float>(cfg.resZ);
        EXPECT_NEAR(gi, fogSpec, 1e-5f) << "view depth " << vd;
    }
}

// For every froxel (i,j,k), the per-fragment READ coord (giSampleCoord at the
// froxel-column screen-UV centre and the slice-centre view depth) must round-trip to
// the texel-centre of (i,j,k) — the same texel the inject WRITES. Guards an
// off-by-half-texel read/write mismatch (design §11.6 test 1).
TEST(GiFroxelReconstruction, ReadCoordRoundTripsToWriteTexel)
{
    FroxelGridConfig cfg;
    const int stepI = 17, stepJ = 9, stepK = 5;  // sparse sweep, hits edges + interior
    for (int k = 0; k < cfg.resZ; k += stepK)
    {
        float viewDepth = froxelSliceToViewDepth(cfg, k);
        for (int j = 0; j < cfg.resY; j += stepJ)
        {
            for (int i = 0; i < cfg.resX; i += stepI)
            {
                glm::vec2 uv = froxelToScreenUV(cfg, i, j);
                glm::vec3 coord = giSampleCoord(uv, viewDepth, cfg);
                EXPECT_NEAR(coord.x, (static_cast<float>(i) + 0.5f) / cfg.resX, 1e-5f);
                EXPECT_NEAR(coord.y, (static_cast<float>(j) + 0.5f) / cfg.resY, 1e-5f);
                EXPECT_NEAR(coord.z, (static_cast<float>(k) + 0.5f) / cfg.resZ, 1e-5f)
                    << "froxel k=" << k;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Test 2 — GiEmaBlendConvergesAndDecays
// ---------------------------------------------------------------------------

// Cold-froxel start takes the injection outright (rgb = injected, a = alpha) — no
// blend against undefined history.
TEST(GiEmaBlend, ColdStartTakesInjectionAtAlpha)
{
    glm::vec3 injected(0.4f, 0.6f, 0.2f);
    glm::vec4 gi = giConfidenceBlend(glm::vec4(0.0f), injected,
                                     /*warm=*/false, /*valid=*/true, GI_ALPHA, GI_DECAY);
    EXPECT_NEAR(gi.r, injected.r, 1e-6f);
    EXPECT_NEAR(gi.g, injected.g, 1e-6f);
    EXPECT_NEAR(gi.b, injected.b, 1e-6f);
    EXPECT_NEAR(gi.a, GI_ALPHA, 1e-6f);
}

// Repeated injection of a constant radiance converges .rgb to it and drives .a → 1.
TEST(GiEmaBlend, RepeatedInjectionConverges)
{
    glm::vec3 injected(0.3f, 0.5f, 0.7f);
    // Cold start, then warm-valid every frame.
    glm::vec4 gi = giConfidenceBlend(glm::vec4(0.0f), injected, false, true, GI_ALPHA, GI_DECAY);
    for (int f = 0; f < 200; ++f)
    {
        gi = giConfidenceBlend(gi, injected, /*warm=*/true, /*valid=*/true, GI_ALPHA, GI_DECAY);
    }
    EXPECT_NEAR(gi.r, injected.r, 1e-3f);
    EXPECT_NEAR(gi.g, injected.g, 1e-3f);
    EXPECT_NEAR(gi.b, injected.b, 1e-3f);
    EXPECT_NEAR(gi.a, 1.0f, 1e-3f);
}

// Ceasing injection holds .rgb but bleeds .a by (1 - decay) per frame, so the
// confidence-weighted read result (.rgb · .a) fades toward zero — pins that .a is
// live, not dead state.
TEST(GiEmaBlend, CeasingInjectionDecaysConfidence)
{
    glm::vec3 radiance(0.8f, 0.8f, 0.8f);
    glm::vec4 gi(radiance, 1.0f);  // fully warmed
    float prevReadLen = glm::length(glm::vec3(gi) * gi.a);
    for (int f = 0; f < 50; ++f)
    {
        gi = giConfidenceBlend(gi, glm::vec3(0.0f), /*warm=*/true, /*valid=*/false,
                               GI_ALPHA, GI_DECAY);
        // Radiance held; confidence strictly shrinks; read result strictly shrinks.
        EXPECT_NEAR(gi.r, radiance.r, 1e-6f);
        float readLen = glm::length(glm::vec3(gi) * gi.a);
        EXPECT_LT(readLen, prevReadLen);
        prevReadLen = readLen;
    }
    EXPECT_NEAR(gi.a, std::pow(1.0f - GI_DECAY, 50.0f), 1e-4f);
}

// Reduce-motion (alpha = 0 AND decay = 0) freezes the OUTPUT (.rgb · .a), not just
// the blend — including a warm-INVALID froxel (the branch that decays .a
// independently of alpha). Asserts the freeze actually freezes (design §11.2).
TEST(GiEmaBlend, ReduceMotionFreezesOutput)
{
    glm::vec4 warm(glm::vec3(0.5f, 0.4f, 0.3f), 0.7f);
    glm::vec3 before = glm::vec3(warm) * warm.a;

    // Warm + valid, frozen: must be byte-stable.
    glm::vec4 a = giConfidenceBlend(warm, glm::vec3(0.9f), true, true, 0.0f, 0.0f);
    EXPECT_EQ(glm::vec3(a) * a.a, before);

    // Warm + INVALID, frozen: the decay branch must also be frozen.
    glm::vec4 b = giConfidenceBlend(warm, glm::vec3(0.0f), true, false, 0.0f, 0.0f);
    EXPECT_EQ(glm::vec3(b) * b.a, before);
}

// ---------------------------------------------------------------------------
// Test 3 — GiHistoryReadIsReprojected (the C1 guard)
// ---------------------------------------------------------------------------

// With a non-identity camera delta between frames, the inject must read history at
// the froxel centre's PREVIOUS-frame froxel coord, not at the froxel's own (i,j,k)
// normalised coord (which is what an index-aliased read would use).
TEST(GiReprojection, ReadsAtPreviousFrameCoordNotIndex)
{
    FroxelGridConfig cfg;
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);

    // Camera dollied + strafed between the previous frame and this one. Both look at a
    // distant target so a deep central froxel stays in both frusta while still
    // reprojecting to a measurably different coord under the parallax.
    glm::vec3 target(0.0f, 1.0f, -50.0f);
    glm::mat4 prevView = glm::lookAt(glm::vec3(0.0f, 1.0f, 0.0f), target, glm::vec3(0, 1, 0));
    glm::mat4 curView  = glm::lookAt(glm::vec3(1.5f, 1.3f, 0.5f), target, glm::vec3(0, 1, 0));
    glm::mat4 invProjection = glm::inverse(proj);
    glm::mat4 invView = glm::inverse(curView);
    glm::mat4 prevViewProj = proj * prevView;

    int i = 80, j = 45, k = 40;  // screen centre, deep slice (~22 m) — visible both frames
    glm::vec3 world = giFroxelCenterWorld(i, j, k, invProjection, invView, cfg);
    GiReprojection r = giReprojectToHistory(world, prevViewProj, prevView, cfg);

    ASSERT_TRUE(r.inFrustum) << "a central froxel should reproject inside the prev frustum";

    // The froxel's own index-normalised coord (what a non-reprojected read aliases to).
    glm::vec2 ownUv = froxelToScreenUV(cfg, i, j);
    float ownZ = (static_cast<float>(k) + 0.5f) / cfg.resZ;
    glm::vec3 ownCoord(ownUv.x, ownUv.y, ownZ);

    EXPECT_GT(glm::length(r.historyUvw - ownCoord), 1e-3f)
        << "reprojected coord must differ from the froxel index coord under camera motion";
}

// A froxel whose reprojection lands outside the previous frustum reads COLD
// (inFrustum == false) — no stale smear on newly-revealed regions.
TEST(GiReprojection, OutOfFrustumReadsCold)
{
    FroxelGridConfig cfg;
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);
    glm::mat4 prevView = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -10.0f),
                                     glm::vec3(0, 1, 0));
    glm::mat4 prevViewProj = proj * prevView;

    // World point far off to the right — well outside the previous frame's frustum.
    glm::vec3 offScreen(1000.0f, 0.0f, -10.0f);
    GiReprojection r = giReprojectToHistory(offScreen, prevViewProj, prevView, cfg);
    EXPECT_FALSE(r.inFrustum);

    // World point behind the previous camera (positive Z in view space) — also cold.
    glm::vec3 behind(0.0f, 0.0f, 10.0f);
    GiReprojection rb = giReprojectToHistory(behind, prevViewProj, prevView, cfg);
    EXPECT_FALSE(rb.inFrustum);
}

}  // namespace

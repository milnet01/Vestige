// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_acoustic_bake.cpp
/// @brief AX3 B2 — image-source bake core: the two design §6.2 verify cases
///        (single-wall image amplitude/delay, sealed-box RT60) plus absorption
///        table, Sabine closed form, and re-bake determinism.

#include <gtest/gtest.h>

#include "audio/acoustic_bake.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Vestige;

namespace
{

/// The six inward-facing walls of an axis-aligned box centred at the origin,
/// all tagged `mat`. Half-extents (hx,hy,hz) → dims 2·h; areas per face.
std::vector<ReflectingFacet> boxFacets(float hx, float hy, float hz, SurfaceMaterial mat)
{
    const float areaX = (2.0f * hy) * (2.0f * hz);  // x walls span y×z
    const float areaY = (2.0f * hx) * (2.0f * hz);  // y walls span x×z
    const float areaZ = (2.0f * hx) * (2.0f * hy);  // z walls span x×y
    return {
        { glm::vec4(-1.0f, 0.0f, 0.0f, hx), areaX, mat },  // x = +hx
        { glm::vec4( 1.0f, 0.0f, 0.0f, hx), areaX, mat },  // x = -hx
        { glm::vec4(0.0f, -1.0f, 0.0f, hy), areaY, mat },  // y = +hy
        { glm::vec4(0.0f,  1.0f, 0.0f, hy), areaY, mat },  // y = -hy
        { glm::vec4(0.0f, 0.0f, -1.0f, hz), areaZ, mat },  // z = +hz
        { glm::vec4(0.0f, 0.0f,  1.0f, hz), areaZ, mat },  // z = -hz
    };
}

} // namespace

// --- Absorption table (design §6.2) ----------------------------------------

TEST(AcousticBake, AbsorptionTableMatchesDesign)
{
    EXPECT_FLOAT_EQ(surfaceMaterialAbsorption(SurfaceMaterial::Default), 0.04f);
    EXPECT_FLOAT_EQ(surfaceMaterialAbsorption(SurfaceMaterial::Stone),   0.03f);
    EXPECT_FLOAT_EQ(surfaceMaterialAbsorption(SurfaceMaterial::Cloth),   0.55f);
    EXPECT_FLOAT_EQ(surfaceMaterialAbsorption(SurfaceMaterial::Glass),   0.03f);
}

// --- Sabine closed form -----------------------------------------------------

TEST(AcousticBake, SabineRt60ClosedForm)
{
    // 4×4×3 m box, all Stone: V=48, ΣS=80, α=0.03 → 0.161·48/(80·0.03) ≈ 3.22 s.
    const auto facets = boxFacets(2.0f, 2.0f, 1.5f, SurfaceMaterial::Stone);
    EXPECT_NEAR(sabineRt60(48.0f, facets), 3.22f, 0.05f);

    // No absorbing surface / zero volume → no tail.
    EXPECT_FLOAT_EQ(sabineRt60(0.0f, facets), 0.0f);
    EXPECT_FLOAT_EQ(sabineRt60(48.0f, {}), 0.0f);
}

// --- Design §6.2 verify #1: single Stone wall image amplitude + delay -------

TEST(AcousticBake, SingleStoneWallImageAmplitudeAndDelay)
{
    // One wall perpendicular to +x at distance d = 2 m from the probe at the
    // origin. Image-source distance = 2d = 4 m.
    const float d = 2.0f;
    const std::vector<ReflectingFacet> facets = {
        { glm::vec4(-1.0f, 0.0f, 0.0f, d), 12.0f, SurfaceMaterial::Stone },
    };

    BakeParams params;  // defaults: order 2, c=343, 48 kHz
    // A single plane encloses no volume → no statistical tail; the IR is just
    // the one image tap.
    const std::vector<float> ir = bakeProbeIr(facets, glm::vec3(0.0f), 0.0f, params);
    ASSERT_FALSE(ir.empty());

    const int expectedSample =
        static_cast<int>(std::lround((2.0 * d / params.speedOfSound) * params.sampleRate));
    const float expectedAmp = std::sqrt(1.0f - 0.03f) / (2.0f * d);  // √0.97/4 ≈ 0.2462

    // The peak is the image tap; find it.
    const auto peakIt = std::max_element(ir.begin(), ir.end());
    const int peakSample = static_cast<int>(std::distance(ir.begin(), peakIt));

    EXPECT_LE(std::abs(peakSample - expectedSample), 1)
        << "image arrived at sample " << peakSample << ", expected " << expectedSample;
    EXPECT_NEAR(*peakIt, expectedAmp, expectedAmp * 0.01f);  // within ±1%
}

// --- Design §6.2 verify #2: sealed box RT60 via Schroeder T30 ---------------

TEST(AcousticBake, SealedBoxRt60WithinTenPercent)
{
    const auto facets = boxFacets(2.0f, 2.0f, 1.5f, SurfaceMaterial::Stone);
    const float sabine = sabineRt60(48.0f, facets);  // ≈ 3.22 s

    // RT60 is a room property, independent of the listening point. Probe placed
    // off-centre (a realistic listener spot; the exact centre piles many
    // reflections into identical delay bins — a symmetry artifact, not a bug).
    const std::vector<float> ir = bakeProbeIr(facets, glm::vec3(0.5f, 0.3f, 0.2f), 48.0f, BakeParams{});
    ASSERT_FALSE(ir.empty());

    const float measured = estimateRt60(ir, 48000);
    EXPECT_NEAR(measured, sabine, sabine * 0.10f)
        << "Schroeder T30 RT60 " << measured << " s vs Sabine " << sabine << " s";
}

// --- Determinism (B3 re-bake contract) --------------------------------------

TEST(AcousticBake, ReBakeIsDeterministic)
{
    const auto facets = boxFacets(2.0f, 2.0f, 1.5f, SurfaceMaterial::Stone);
    const std::vector<float> a = bakeProbeIr(facets, glm::vec3(0.3f, 0.1f, -0.2f), 48.0f, BakeParams{});
    const std::vector<float> b = bakeProbeIr(facets, glm::vec3(0.3f, 0.1f, -0.2f), 48.0f, BakeParams{});
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(a, b);  // bit-identical tail (deterministic seed) + early taps
}
